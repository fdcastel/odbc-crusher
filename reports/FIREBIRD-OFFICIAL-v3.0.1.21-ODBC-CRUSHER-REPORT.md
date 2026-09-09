# `Firebird ODBC Driver 3.0.1.21 (official)` v`3.0.1.21` — Crusher Triage Report

**Generated**: 2026-09-09 15:02 UTC
**CI run**: https://github.com/fdcastel/odbc-crusher/actions/runs/34367166127
**Driver source**: https://github.com/FirebirdSQL/firebird-odbc-driver @ `dee624fac13182f569532a5785e32eb5d27bed98`
**Crusher commit**: 68630c6

## Run integrity

| Check | Result |
|---|---|
| Report complete | ✅ — `PARTIAL_REPORT=false` |
| Category filter | full run (23 of 23 categories) |
| Discovery phase | ✅ completed — `driver_info`, `type_info`, `function_info` and `scalar_functions` all available |
| Driver crashes | `1` category — `Descriptor Tests` |
| Version provenance | `OK` — Observed install '3.0.1.21' matches manifest '3.0.1.21'. |
| Report schema | `1` |

1 category crashed; every probe in it was discarded, so the totals below count
only what survived and the pass rate is optimistic by an unknown margin. The
whole `Descriptor Tests` category — every `SQLGetDescField` / `SQLSetDescField` /
`SQLCopyDesc` probe crusher carries — produced no data at all, so this report
says nothing about descriptor conformance in either direction.

Provenance is clean: the installed `System32\FirebirdODBC.dll` was read back
after the installer ran and its `ProductVersion` resource matches the manifest,
and the download was gated on a pinned sha256. `TAG` is a commit SHA, not a
moving branch.

## Summary

| Metric | Value |
|---|---|
| Total tests | `204` |
| Passed | `146` |
| Failed | `33` |
| Errors | `1` |
| Skipped | `13` |
| Informational (not scored) | `11` |
| Scored | `193` |
| Pass rate | `75.6` % |

Pass rate is `passed / scored`, where `scored = total − informational`.
Informational probes record what the driver said but have no right answer, so
they are excluded from the denominator rather than counted as passes.

## Classification breakdown

| Label | Findings | Clusters |
|---|---:|---:|
| `BUG_IN_DRIVER` | 33 | 9 |
| `BUG_IN_CRUSHER` | 3 | 2 |
| `DRIVER_LIMITATION` | 0 | 0 |
| `INCONCLUSIVE` | 8 | 2 |

## Punch list

1. **Fix numeric-C → character-SQL parameter binding: it stores only the first character of the converted value and returns `SQL_SUCCESS`** — 14 CRITICAL findings, and it is silent data corruption on the single most common bind path an application has.
2. **Find the access violation in the descriptor entry points** — the driver faults hard enough to take a whole category with it, and any client that touches an explicit descriptor can hit it.
3. **Honour `SQL_ATTR_PARAMSET_SIZE` at `SQLExecute`, not at `SQLPrepare`** — the canonical prepare→bind→set-array-size→execute order silently inserts one row of N and reports success.

## Root causes

### RC1 — Numeric C types bound to CHAR/VARCHAR store only the first character (`BUG_IN_DRIVER`, 14 findings)

- **Function**: `SQLBindParameter` / `SQLExecute`
- **Conformance**: Core
- **Affects**: `test_bindparam_tinyint_to_varchar_roundtrip`, `test_bindparam_short_to_varchar_roundtrip`, `test_bindparam_int_to_varchar_roundtrip`, `test_bindparam_bigint_to_varchar_roundtrip`, `test_bindparam_float_to_varchar_roundtrip`, `test_bindparam_double_to_varchar_roundtrip`, `test_bindparam_double_to_varchar_fractional_roundtrip`, `test_bindparam_int_to_char_roundtrip`, `test_bindparam_utinyint_to_varchar_roundtrip`, `test_bindparam_ushort_to_varchar_roundtrip`, `test_bindparam_ulong_to_varchar_roundtrip`, `test_bindparam_ubigint_to_varchar_roundtrip`, `test_bindparam_bigint_to_char_roundtrip`, `test_bindparam_double_to_char_roundtrip` (14 tests)
- **Evidence**: every stored value is the correct text cut to its first character —
  `10` → `'1'`, `1.5` → `'1'`, `10.5` → `'1'`, `2.5` → `'2'`. Rows 1–9 of the
  integer probes pass only because their correct text is already one character
  long, which is why the integer probes report exactly one bad row and the
  fractional-double probe reports all ten. `SQLExecute` returned `SQL_SUCCESS`
  for every one of them and the `COMMIT` succeeded; the rows are present and
  wrong.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcConvert.cpp#L1297-L1373

```cpp
    int len = to->length;                       /* L1306 */
    ...
    if ( p - temp > len - l )                   /* L1358 — silent truncation */
        p = temp + len - l;
    ...
    len = q - string;                           /* L1365 */
    if ( to->isIndicatorSqlDa )
        to->headSqlVarPtr->setSqlLen(len);      /* L1370 — length handed to the engine */
    return SQL_SUCCESS;                         /* never 22001, whatever it cut */
```

**Why this classification**: the conversion caps the produced string at
`to->length` and then unconditionally returns `SQL_SUCCESS` — there is no
`22001` ("String data, right truncated") path anywhere in the macro, so a
parameter value that does not fit is written short and the application is never
told. `to->length` is not the `ColumnSize` the application passed to
`SQLBindParameter`: `sqlBindParameter` stores it (`imprec->length = precision`,
[OdbcStatement.cpp#L2276](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L2276))
and `OdbcDesc::defFromMetaDataIn` then overwrites it from
`metaDataIn->getPrecision()`
([OdbcDesc.cpp#L236-L250](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcDesc.cpp#L236-L250))
before the conversion runs. What is not pinned down from source alone is why
that length lands on 1 for a `VARCHAR(32)` parameter — that needs a debugger on
`defFromMetaDataIn` — but the defect does not depend on knowing: the code path
can truncate a parameter and has no way to say so. The probe is sound; it
inserts, commits, re-reads with `ORDER BY` and reads each value to completion
across as many `SQLGetData` calls as the driver asks for
(`src/tests/test_base.cpp:195-260`), so a short read cannot manufacture this.

**Suggested fix**: post `22001` and return `SQL_ERROR` from the `conv*ToString`
family when `p - temp > len - l` fires, and stop discarding the application's
`ColumnSize` in `defFromMetaDataIn` for parameters that `SQLBindParameter`
already described.

### RC2 — Access violation inside the driver during the descriptor probes (`BUG_IN_DRIVER`, 1 finding)

- **Function**: N/A — the crash guard caught the fault, not a call
- **Conformance**: Core
- **Affects**: `Descriptor Tests (DRIVER CRASH)` (1 entry, standing in for the whole category)
- **Evidence**: `Access violation (0xC0000005)`. The category object holds only
  the crash entry — every descriptor probe result was discarded.
- **Citation**: `src/tests/descriptor_tests.cpp` (crusher-side; the whole category), category `Descriptor Tests`

**Why this classification**: a crash is `BUG_IN_DRIVER`, always. The faulting
address is unknown because the crash guard traded the stack for the ability to
keep running the remaining 22 categories — that is a deliberate trade in the
harness, not missing evidence about the driver. An access violation raised
inside `FirebirdODBC.dll` on a documented ODBC call sequence is a defect
regardless of which line raised it, and it is reachable by any client that uses
explicit descriptors (`SQLGetDescField` / `SQLSetDescField` / `SQLCopyDesc`).

**Suggested fix**: run the `Descriptor Tests` category alone under a debugger
with the crash guard disabled to get the faulting frame; `OdbcDesc.cpp` /
`DescRecord.cpp` are the first place to look, and `OdbcDesc::getDescRecord` on
an unallocated record number is the classic shape.

### RC3 — `SQL_ATTR_PARAMSET_SIZE` is latched at `SQLPrepare`, so array executions silently insert one row (`BUG_IN_DRIVER`, 7 findings)

- **Function**: `SQLSetStmtAttr` / `SQLExecute`
- **Conformance**: Level 1 (six findings), Core (`test_paramset_size_one`)
- **Affects**: `test_column_wise_array_binding`, `test_row_wise_array_binding`, `test_param_status_array`, `test_params_processed_count`, `test_param_operation_array`, `test_paramset_size_one`, `test_param_status_per_row_partial_failure` (7 tests)
- **Evidence**: `SQLExecute` returned 0 with `PARAMSET_SIZE=3` and `COUNT(*) = 1`;
  `SQL_ATTR_PARAMS_PROCESSED_PTR` stayed 0 and the status array kept the
  probe's `0xffff` fill in every one of these probes, i.e. the driver never
  wrote to them at all.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L425-L432

```cpp
else if ( statement->isActiveModify() && applicationParamDescriptor->headArraySize > 1 )
    execute = &OdbcStatement::executeStatementParamArray;
else
    execute = &OdbcStatement::executeStatement;
```

**Why this classification**: the array-capable execute function is chosen inside
`sqlPrepare`, from `headArraySize` as it stands *at prepare time*. The probes
follow the canonical ODBC order — prepare, bind, set `SQL_ATTR_PARAMSET_SIZE`,
execute (`src/tests/array_param_tests.cpp:223` then `:186-209`) — which the
spec explicitly permits: statement attributes may be set any time before
execution. The driver therefore keeps `executeStatement`, inserts the first row
only, and returns `SQL_SUCCESS`. Everything the probes then observe follows from
that: `*rowCountPt` and the status array are only ever written inside
`executeStatementParamArray`
([OdbcStatement.cpp#L2886](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L2886)
and [#L2905](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L2905)),
which never ran. `test_paramset_size_one` fails for the same reason and is worth
separating out: with `PARAMSET_SIZE=1` the array path is never selected *by
design*, yet the spec still requires `SQL_ATTR_PARAMS_PROCESSED_PTR` to be set
to 1 and the one status-array element to be filled — so that Core-level
bookkeeping is missing on the ordinary single-row path too.

**Suggested fix**: pick the execute function at `SQLExecute` time from the
current `headArraySize`, and write `SQL_ATTR_PARAMS_PROCESSED_PTR` and the
status array on the single-row path as well.

### RC4 — `{fn …}` scalar functions the driver advertises do not run (`BUG_IN_DRIVER`, 4 findings)

- **Function**: `SQLExecDirect` escape translation, `SQLGetInfo(SQL_*_FUNCTIONS)`
- **Conformance**: Core
- **Affects**: `test_string_scalar_functions`, `test_datetime_scalar_functions`, `test_system_scalar_functions`, `test_scalar_function_claim_vs_execute` (4 tests)
- **Evidence**: `STRING:6/7 NUMERIC:4/4 TIMEDATE:3/5 SYSTEM:1/2 (broken: LENGTH,
  YEAR, MONTH, DATABASE)`. `{fn LENGTH('test')}` → `-104 Token unknown … (`;
  `{fn YEAR({d '2026-01-15'})}` → `-104 Unexpected end of command` and, in the
  `FROM RDB$DATABASE` variant, `-105 Specified EXTRACT part does not exist`;
  `{fn DATABASE()}` returned empty.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/IscDbc/SupportFunctions.cpp#L71

```cpp
ADD_SUPPORT_FN( STR_FN, SQL_FN_STR_LENGTH,  "LENGTH", "LENGTH", defaultTranslator);   // L71
ADD_SUPPORT_FN( TD_FN,  SQL_FN_TD_MONTH,    "MONTH",  " extract(month from ", bracketfromTranslator); // L125
ADD_SUPPORT_FN( TD_FN,  SQL_FN_TD_YEAR,     "YEAR",   " extract(year from ",  bracketfromTranslator); // L133
ADD_SUPPORT_FN( SYS_FN, SQL_FN_SYS_DBNAME,  "DBNAME", "DBNAME", defaultTranslator);   // L137
```

**Why this classification**: three separate defects, all in one table, and all
of them things the driver *claims* — `test_scalar_function_claim_vs_execute`
reads the `SQL_STRING_FUNCTIONS` / `SQL_TIMEDATE_FUNCTIONS` /
`SQL_SYSTEM_FUNCTIONS` bitmasks back from `SQLGetInfo` and only then executes
what they promise, so this is advertise-then-fail, not an unimplemented option.
(1) `SQL_FN_STR_LENGTH` is translated to a bare `LENGTH(` — Firebird has no
`LENGTH` function; it spells this `CHAR_LENGTH`, which the very next table entry
already maps correctly. (2) `YEAR`/`MONTH` use `bracketfromTranslator`
([#L380-L395](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/IscDbc/SupportFunctions.cpp#L380-L395)),
which scans forward to the first `(` and reuses the escape's own closing paren
as the `extract(...)` terminator, counting neither brackets nor nested escapes —
so a nested `{d '…'}` argument, the exact form the ODBC spec uses, comes out
malformed. (3) The system-function entry registers the ODBC-side name `DBNAME`,
but the scalar function ODBC defines is `DATABASE()` (`SQL_FN_SYS_DBNAME` is the
bitmask's name, not the function's), so `{fn DATABASE()}` matches nothing in the
table, passes through untranslated, and returns empty.

**Suggested fix**: map `SQL_FN_STR_LENGTH` to `CHAR_LENGTH`, rename the
`SQL_FN_SYS_DBNAME` entry's ODBC name to `DATABASE`, and give
`bracketfromTranslator` the bracket/escape counting that `Tokenize`
([#L402-L430](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/IscDbc/SupportFunctions.cpp#L402-L430))
already implements — or clear the corresponding bits in `SQLGetInfo` so clients
stop being told these work.

### RC5 — `SQLGetDiagField` header fields: a 32-bit write into a 64-bit field, and a code that is never set (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLGetDiagField`
- **Conformance**: Core
- **Affects**: `test_diagfield_row_count`, `test_diagfield_dynamic_function` (2 tests)
- **Evidence**: `SQL_DIAG_ROW_COUNT = -4294967296` — that is `0xFFFFFFFF00000000`,
  the probe's own `-1` fill with only its low four bytes overwritten by a zero.
  `SQL_DIAG_DYNAMIC_FUNCTION_CODE = 0` after a `SELECT` (expected 85,
  `SQL_DIAG_SELECT_CURSOR`).
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcObject.cpp#L316-L341

```cpp
case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
    *(SQLINTEGER*)ptr = sqlDiagDynamicFunctionCode;   /* L321 — only ever 0 */
    return SQL_SUCCESS;
...
case SQL_DIAG_ROW_COUNT:
    *(SQLINTEGER*)ptr = sqlDiagRowCount;              /* L340 — 4-byte write, SQLLEN buffer */
    return SQL_SUCCESS;
```

**Why this classification**: `SQL_DIAG_ROW_COUNT` is defined as `SQLLEN`, which
is 8 bytes in this 64-bit build; the driver writes 4 and leaves the caller's
top half untouched, which is exactly the bit pattern the probe observed. That is
a portability bug, not a value bug — the same line is wrong for every caller on
a 64-bit platform, and `SQL_DIAG_CURSOR_ROW_COUNT` at
[#L312](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcObject.cpp#L312)
has it too. `sqlDiagDynamicFunctionCode` is initialised to 0 at
[#L48](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcObject.cpp#L48)
and [#L254](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcObject.cpp#L254)
and assigned nowhere else in the tree, so the field is a stub that always
reports "no statement".

**Suggested fix**: write `SQL_DIAG_ROW_COUNT` and `SQL_DIAG_CURSOR_ROW_COUNT`
through `*(SQLLEN*)ptr`, and set `sqlDiagDynamicFunctionCode` in `sqlPrepare` /
`sqlExecDirect` from the statement type the parser already determined.

### RC6 — Precondition checks that never fire: `SQLCloseCursor` and `SQLSetConnectAttr` accept anything (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLCloseCursor`, `SQLSetConnectAttr`
- **Conformance**: Core
- **Affects**: `test_closecursor_no_cursor`, `test_setconnattr_invalid_attr` (2 tests)
- **Evidence**: `SQLCloseCursor` succeeded with no open cursor (expected `24000`);
  `SQLSetConnectAttr` accepted attribute 99999 (expected `HY092`).
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L2397-L2415 and https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcConnection.cpp#L386-L488

**Why this classification**: `sqlCloseCursor` calls `releaseResultSet()` and
returns `sqlSuccess()` with no test for whether a cursor was open — the spec
requires `SQL_ERROR` / `24000` in that state, and applications that use the
return code to decide whether a cursor needs closing get no signal. The
`switch` in `OdbcConnection::sqlSetConnectAttr` has no `default:` arm at all, so
an unrecognised attribute falls off the end of the statement into the success
return; per the spec an attribute the driver does not recognise must be rejected
with `HY092`. Both are the same shape — a state/argument check that was never
written — which is why they are one cluster.

**Suggested fix**: return `24000` from `sqlCloseCursor` when no result set is
active, and add a `default:` arm returning `HY092` to `sqlSetConnectAttr`.

### RC7 — Every engine error is reported as `HY000` (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLExecDirect`
- **Conformance**: Core
- **Affects**: `test_execdirect_syntax_error` (1 test)
- **Evidence**: a syntax error returned `SQL_ERROR` with `SQLSTATE=HY000`
  (expected `42000`). Every Firebird diagnostic captured anywhere in this run
  shows the same wrapper, e.g. finding 20: `[HY000] (Native: -104) … Dynamic SQL
  Error -SQL error code = -104 -Token unknown`.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L2404-L2410

```cpp
catch ( std::exception &ex )
{
    SQLException &exception = (SQLException&)ex;
    postError ("HY000", exception);      // the same three lines in every entry point
    return SQL_ERROR;
}
```

**Why this classification**: the driver catches `SQLException` at each entry
point and posts a fixed `HY000`, discarding the Firebird error code it is
holding — the native code `-104` (dynamic SQL / syntax) maps cleanly onto
`42000`, and `-105`, `-204`, `-803` and friends onto their own SQLSTATEs. An
application cannot branch on "syntax error" versus "connection lost" versus
"constraint violation" when all three arrive as `HY000`; the SQLSTATE is the
only portable signal ODBC gives it. This is one finding but a driver-wide
pattern, which is why it is worth its own root cause.

**Suggested fix**: add a Firebird-code → SQLSTATE map at the `postError` call
sites (or inside `postError`), defaulting to `HY000` only for codes it has no
mapping for.

### RC8 — `SQL_ATTR_ASYNC_ENABLE` is accepted and then ignored (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)`
- **Conformance**: Level 2
- **Affects**: `test_async_capability` (1 test)
- **Evidence**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE, ON)` returned success but
  `SQLGetStmtAttr` still reports OFF.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/OdbcStatement.cpp#L3379-L3382

```cpp
case SQL_ATTR_ASYNC_ENABLE:            // set: stored…
    asyncEnable = (intptr_t) ptr == SQL_ASYNC_ENABLE_ON;
    break;
...
case SQL_ATTR_ASYNC_ENABLE:            // get (L2490): …and ignored
    value = 0;                         // driver doesn't do async
```

**Why this classification**: not a `DRIVER_LIMITATION` — the driver's own
comment says it does not do async, and declining is spec-legal, but declining
means returning `SQL_ERROR` with `HYC00` ("Optional feature not implemented")
from `SQLSetStmtAttr`. Returning `SQL_SUCCESS` and then reporting OFF tells the
application the setting took effect when it did not, which is the one answer the
spec does not allow. An application that checks the return code and proceeds to
poll for `SQL_STILL_EXECUTING` is now written against a contract the driver
never honoured.

**Suggested fix**: return `SQL_ERROR` / `HYC00` for
`SQL_ATTR_ASYNC_ENABLE = SQL_ASYNC_ENABLE_ON` and drop the write-only
`asyncEnable` member.

### RC9 — `SQLGetInfoW` truncation reports bytes written, not bytes available (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLGetInfo` (wide entry point)
- **Conformance**: Core
- **Affects**: `test_string_truncation_wchar` (1 test)
- **Evidence**: truncation returned `01004` correctly, but reported 26 bytes
  needed for a 28-byte buffer whose full value is 56 bytes.
- **Citation**: https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/MainUnicode.cpp#L556-L565

```cpp
SQLRETURN ret = ((OdbcConnection*) hDbc)->sqlGetInfo( infoType, infoValue,
                                                bufferLength, stringLength );
*stringLength *= 2;                    /* L563 */
return ret;
```

**Why this classification**: on truncation the spec requires `*StringLengthPtr`
to receive the total number of bytes *available to return*, excluding the null
terminator — that is what lets a caller resize its buffer and ask again. 26 is
the byte count that fitted (13 wide characters, doubled at L563), so a caller
that reallocates to the reported size gets the same truncated answer forever.
Reporting less than the buffer it was given is also self-contradictory: `01004`
means "there was more", and 26 < 28 says there was not.

**Suggested fix**: have the ANSI `sqlGetInfo` return the full available length
regardless of what fitted, and convert that length for the wide entry point
rather than doubling whatever came back.

### RC10 — The CALL-escape probe asserts translation of procedures that do not exist (`BUG_IN_CRUSHER`, 1 finding)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Affects**: `test_call_escape_format_variants` (1 test)
- **Evidence**: 0/7 CALL variants translated — `{CALL proc}`, `{CALL proc()}`,
  `{CALL proc(?)}`, `{CALL proc(?,?)}`, `{?=CALL func}`, `{?=CALL func(?)}`,
  `{?=CALL func(?,?)}`.
- **Citation**: `src/tests/escape_sequence_tests.cpp:1044-1085` (variant list at `:1056-1065`, verdict at `:1073-1077`)

**Why this classification**: the probe hard-codes the identifiers `proc` and
`func`, which exist in no database, and counts any non-translation *including an
error return* as a failure (`auto translated = call_native_sql(v); if
(translated && !translated->empty() && translated->find('{') ==
std::string::npos) ++passed_count;`). Counter-example, from this driver: Firebird
has two different native spellings for a procedure call — `execute procedure p`
for a non-selectable procedure and `select * from p` for a selectable one — so
`getNativeSql` must look the procedure up before it can choose, and throws
`Unknown procedure 'PROC'` when it cannot
([IscDbc/IscConnection.cpp#L1642-L1656](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/IscDbc/IscConnection.cpp#L1642-L1656)).
A driver with complete and correct CALL-escape handling therefore scores 0/7
here, which makes the probe unable to distinguish "does not translate CALL" from
"was asked about a procedure that does not exist". Note the driver is not
blameless in the surrounding area — it reports that refusal as `HY000` rather
than `42000` (RC7) — but the 0/7 verdict this probe produced is not evidence of
a translation defect.

**Suggested fix**: create a real procedure in the fixture (the probe file
already has the `MOCK_INOUT` convention for this) and assert against its name,
or accept a driver-reported error as `SKIP_INCONCLUSIVE` rather than counting it
as "not translated".

### RC11 — The WVARCHAR round-trip cells hard-code `NVARCHAR(20)` DDL (`BUG_IN_CRUSHER`, 2 findings)

- **Function**: `SQLBindParameter`
- **Conformance**: Core
- **Affects**: `test_bindparam_int_to_wvarchar_roundtrip`, `test_bindparam_bigint_to_wvarchar_roundtrip` (2 tests)
- **Evidence**: `Could not CREATE TABLE ODBC_TEST_ROUNDTRIP_WCHAR (ID INTEGER,
  VAL NVARCHAR(20))` — `-104 Token unknown … (`. Firebird parses `NVARCHAR` as a
  domain name and then chokes on the `(`.
- **Citation**: `src/tests/param_binding_tests.cpp:757-763` and `:833` — the file's own comment says "NVARCHAR maps to SQL_WVARCHAR **in the mock-driver DDL parser**"

**Why this classification**: the probe picks its DDL from a constant tuned to the
mock driver rather than from the driver under test. Counter-example: this driver
does support wide character columns and says so in `SQLGetTypeInfo`, where
`SQL_WVARCHAR` is reported with the type name `VARCHAR(x) CHARACTER SET
UNICODE_FSS`
([IscDbc/TypesResultSet.cpp#L113](https://github.com/FirebirdSQL/firebird-odbc-driver/blob/dee624fac13182f569532a5785e32eb5d27bed98/IscDbc/TypesResultSet.cpp#L113))
— the exact string the probe needed was already in the report it wrote. Two
`CRITICAL` cells of the bind matrix were therefore never exercised on this
driver. The probe does degrade to `SKIP_INCONCLUSIVE` rather than `FAIL`, so it
reports no false defect; the cost is coverage, not a wrong verdict.

**Suggested fix**: read `TYPE_NAME` for `SQL_WVARCHAR` from `SQLGetTypeInfo` and
build the DDL from it, falling back to the current constant only when the driver
reports no wide type at all.

### RC12 — The CI database has no tables, so the catalog probes had nothing to inspect (`INCONCLUSIVE`, 4 findings)

- **Function**: `SQLTables`, `SQLStatistics`, `SQLTablePrivileges`, `SQLGetInfo(SQL_OJ_CAPABILITIES)`
- **Conformance**: Core / Level 1 / Level 2
- **Affects**: `test_count_star_result_metadata`, `test_statistics_result`, `test_privileges_result`, `test_outer_join_escape` (4 tests)
- **Evidence**: all four report the same thing — "SQLTables returned no tables" /
  "SQLTables reported no tables, so there is nothing to ask about" / "SQLTables
  reported no table to join".
- **Citation**: `src/tests/test_base.cpp:467-498` (`discover_tables`, which passes `NULL` for catalog, schema and table — the correct call) and `.github/workflows/stress-test.yml:421`

**Why this classification**: cannot tell from source whether `SQLTables` is
faulty, because there is nothing for it to return. The workflow creates the
Firebird database with `New-FirebirdDatabase -Database '/tmp/crusher.fdb'` and
never seeds it, and every probe that does create a table drops it again in the
same probe — so by the time the catalog categories run, the database holds no
user table. `SQLTables` returning zero rows is the correct answer to the
question that was asked. The probes were right to report
`SKIP_INCONCLUSIVE`; what is missing is a fixture, not a diagnosis. Resolving it
needs one persistent table (with an index and a foreign key, to give
`SQLStatistics` and `SQLTablePrivileges` something to describe) created before
crusher runs.

**Suggested fix**: seed `crusher.fdb` with a small permanent fixture table in
the workflow's database-creation step; four probes and part of a fifth become
real assertions for free.

### RC13 — The stored-procedure escape probes need a procedure the Firebird fixture does not have (`INCONCLUSIVE`, 4 findings)

- **Function**: `SQLProcedures`, `SQLBindParameter` (`SQL_PARAM_INPUT` / `OUTPUT` / `INPUT_OUTPUT`)
- **Conformance**: Core
- **Affects**: `test_call_escape_in_parameter`, `test_call_escape_out_parameter`, `test_function_call_escape_return_value`, `test_call_escape_inout_parameter` (4 tests)
- **Evidence**: "Test procedure `MOCK_INOUT` not visible via SQLProcedures" (×3)
  and "Test function `MOCK_FN` not visible via SQLProcedures".
- **Citation**: `src/tests/escape_sequence_tests.cpp:1088-1095` — the probes'
  own header says `MOCK_INOUT` is registered by the mock driver and that "Real
  drivers rarely have a procedure of that exact name; the probes
  SKIP_INCONCLUSIVE when SQLProcedures reports MOCK_INOUT is absent"

**Why this classification**: this is the designed behaviour of the probes, and
the outcome is honest — nothing was learned about this driver's `SQL_PARAM_OUTPUT`
or `SQL_PARAM_INPUT_OUTPUT` handling, in either direction. Two of the four are
`ERROR`-severity gaps in Core coverage, so they matter. Resolving it needs the
same thing as RC12: a fixture, here a `MOCK_INOUT` procedure and a `MOCK_FN`
function created in the Firebird database before the run.

**Suggested fix**: add `MOCK_INOUT` (IN INTEGER, OUT INTEGER, INOUT VARCHAR) and
`MOCK_FN` to the workflow's database seeding, alongside the RC12 fixture table.

## All findings

| # | Test | Status | Severity | Root cause | Classification |
|---|---|---|---|---|---|
| 1 | `test_count_star_result_metadata` | SKIP_INCONCLUSIVE | INFO | RC12 | `INCONCLUSIVE` |
| 2 | `test_async_capability` | FAIL | ERROR | RC8 | `BUG_IN_DRIVER` |
| 3 | `Descriptor Tests (DRIVER CRASH)` | ERROR | CRITICAL | RC2 | `BUG_IN_DRIVER` |
| 4 | `test_execdirect_syntax_error` | FAIL | WARNING | RC7 | `BUG_IN_DRIVER` |
| 5 | `test_setconnattr_invalid_attr` | FAIL | WARNING | RC6 | `BUG_IN_DRIVER` |
| 6 | `test_closecursor_no_cursor` | FAIL | WARNING | RC6 | `BUG_IN_DRIVER` |
| 7 | `test_string_truncation_wchar` | FAIL | WARNING | RC9 | `BUG_IN_DRIVER` |
| 8 | `test_statistics_result` | SKIP_INCONCLUSIVE | INFO | RC12 | `INCONCLUSIVE` |
| 9 | `test_privileges_result` | SKIP_INCONCLUSIVE | INFO | RC12 | `INCONCLUSIVE` |
| 10 | `test_diagfield_row_count` | FAIL | INFO | RC5 | `BUG_IN_DRIVER` |
| 11 | `test_diagfield_dynamic_function` | FAIL | WARNING | RC5 | `BUG_IN_DRIVER` |
| 12 | `test_bindparam_tinyint_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 13 | `test_bindparam_short_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 14 | `test_bindparam_int_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 15 | `test_bindparam_bigint_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 16 | `test_bindparam_float_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 17 | `test_bindparam_double_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 18 | `test_bindparam_double_to_varchar_fractional_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 19 | `test_bindparam_int_to_char_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 20 | `test_bindparam_int_to_wvarchar_roundtrip` | SKIP_INCONCLUSIVE | CRITICAL | RC11 | `BUG_IN_CRUSHER` |
| 21 | `test_bindparam_utinyint_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 22 | `test_bindparam_ushort_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 23 | `test_bindparam_ulong_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 24 | `test_bindparam_ubigint_to_varchar_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 25 | `test_bindparam_bigint_to_char_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 26 | `test_bindparam_double_to_char_roundtrip` | FAIL | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 27 | `test_bindparam_bigint_to_wvarchar_roundtrip` | SKIP_INCONCLUSIVE | CRITICAL | RC11 | `BUG_IN_CRUSHER` |
| 28 | `test_column_wise_array_binding` | FAIL | CRITICAL | RC3 | `BUG_IN_DRIVER` |
| 29 | `test_row_wise_array_binding` | FAIL | INFO | RC3 | `BUG_IN_DRIVER` |
| 30 | `test_param_status_array` | FAIL | WARNING | RC3 | `BUG_IN_DRIVER` |
| 31 | `test_params_processed_count` | FAIL | WARNING | RC3 | `BUG_IN_DRIVER` |
| 32 | `test_param_operation_array` | FAIL | INFO | RC3 | `BUG_IN_DRIVER` |
| 33 | `test_paramset_size_one` | FAIL | INFO | RC3 | `BUG_IN_DRIVER` |
| 34 | `test_param_status_per_row_partial_failure` | FAIL | WARNING | RC3 | `BUG_IN_DRIVER` |
| 35 | `test_string_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 36 | `test_datetime_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 37 | `test_system_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 38 | `test_outer_join_escape` | SKIP_INCONCLUSIVE | INFO | RC12 | `INCONCLUSIVE` |
| 39 | `test_call_escape_format_variants` | FAIL | WARNING | RC10 | `BUG_IN_CRUSHER` |
| 40 | `test_call_escape_in_parameter` | SKIP_INCONCLUSIVE | INFO | RC13 | `INCONCLUSIVE` |
| 41 | `test_call_escape_out_parameter` | SKIP_INCONCLUSIVE | ERROR | RC13 | `INCONCLUSIVE` |
| 42 | `test_function_call_escape_return_value` | SKIP_INCONCLUSIVE | ERROR | RC13 | `INCONCLUSIVE` |
| 43 | `test_call_escape_inout_parameter` | SKIP_INCONCLUSIVE | ERROR | RC13 | `INCONCLUSIVE` |
| 44 | `test_scalar_function_claim_vs_execute` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |

## Caveats

- One category (`Descriptor Tests`) crashed and every probe result in it was
  discarded, so the 204 totals and the 75.6 % pass rate describe 22 categories,
  not 23. Descriptor conformance is untested in either direction.
- RC1's exact mechanism is pinned to the conversion macro and its missing
  `22001` path, but the reason the truncation length lands on 1 for a
  `VARCHAR(32)` parameter needs a debugger on `OdbcDesc::defFromMetaDataIn`; the
  classification does not depend on it.
- Eight findings (RC12, RC13) are `INCONCLUSIVE` because the CI database is
  created empty — they are blocked on a fixture, not on analysis.
