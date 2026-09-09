# `Firebird ODBC Driver 3.5.1-rc2 (master + 13 PRs)` v`3.5.1-rc2` — Crusher Triage Report

**Generated**: 2026-09-09 15:02 UTC
**CI run**: https://github.com/fdcastel/odbc-crusher/actions/runs/34367182766
**Driver source**: https://github.com/fdcastel/firebird-odbc-driver @ `v3.5.1-rc2`
**Crusher commit**: 68630c6

## Run integrity

| Check | Result |
|---|---|
| Report complete | ✅ complete — `PARTIAL_REPORT=false` |
| Category filter | full run — 23 of 23 categories |
| Discovery phase | ✅ completed — `driver_info`, `type_info`, `function_info` and `scalar_functions` all present |
| Driver crashes | `0` categories — none |
| Version provenance | `OK` — Observed install '3.5.1.0' matches manifest '3.5.1-rc2'. |
| Report schema | `1` |

No categories crashed and no probe results were discarded, so the totals below
are the whole run.

One qualification on provenance: `actual_version.txt` is a real observation of
the installed DLL's `ProductVersion`, but it reads `3.5.1.0` because the MSI
drops the prerelease suffix. The version resource therefore cannot tell rc1
from rc2 — the sha256 gate is what does, and this comparison only confirms the
3.5.1 line.

A second qualification matters more for reading the numbers: **8 of the 23
findings are skips caused by missing database fixtures, not by driver
behaviour** (RC11, RC12). Those probes measured nothing at all. The pass rate
is not optimistic in the crash sense, but the coverage behind it is thinner
than 197 scored tests suggests.

## Summary

| Metric | Value |
|---|---|
| Total tests | `208` |
| Passed | `171` |
| Failed | `13` |
| Errors | `0` |
| Skipped | `13` |
| Informational (not scored) | `11` |
| Scored | `197` |
| Pass rate | `86.8` % |

Pass rate is `passed / scored`, where `scored = total − informational`.
Informational probes record what the driver said but have no right answer, so
they are excluded from the denominator rather than counted as passes.

## Classification breakdown

| Label | Findings | Clusters |
|---|---:|---:|
| `BUG_IN_DRIVER` | 12 | 8 |
| `BUG_IN_CRUSHER` | 3 | 2 |
| `DRIVER_LIMITATION` | 0 | 0 |
| `INCONCLUSIVE` | 8 | 2 |

## Punch list

1. **Fix the diagnostic header-field writes in `OdbcObject::sqlGetDiagField` (RC4)** — `*(SQLINTEGER*)ptr` stores 4 bytes through a pointer the application supplied as an 8-byte `SQLLEN`, leaving half the caller's variable stale; this is a memory-correctness defect on every 64-bit build, the fix is a cast, and the proof is arithmetic rather than inferential.
2. **Complete the `{fn …}` name table and translate `{d}`/`{t}`/`{ts}` into real Firebird literals (RC1, RC2)** — four of the thirteen failures are scalar functions the driver advertises in `SQL_*_FUNCTIONS` and then cannot execute.
3. **Give the CI database a persistent table and the `MOCK_INOUT` / `MOCK_FN` procedures (RC11, RC12)** — 8 of 23 findings are probes that skipped without touching the driver, including all three `SQL_PARAM_OUTPUT` / `SQL_PARAM_INPUT_OUTPUT` direction checks.
4. **Route the two `SQL_WVARCHAR` round-trip probes through `RoundTripTableGuard::create_first_working` (RC10)** — both `CRITICAL` cells currently skip on any engine without `NVARCHAR`, which includes Firebird.

## Root causes

### RC1 — `{fn …}` name table misses functions the driver advertises (`BUG_IN_DRIVER`, 3 findings)

- **Function**: `SQLExecDirect` (escape translation), `SQLGetInfo(SQL_*_FUNCTIONS)`
- **Conformance**: Core
- **Affects**: `test_string_scalar_functions`, `test_system_scalar_functions`, `test_scalar_function_claim_vs_execute` (3 tests)
- **Evidence**: `SELECT {fn LENGTH('test')}` reaches the engine as `SELECT LENGTH(…)` and dies with `-104 Token unknown — line 1, column 14`, column 14 being the `(` immediately after an untranslated `LENGTH`. `{fn DATABASE()}` returns empty for the same reason. `test_scalar_function_claim_vs_execute` confirms both are *claimed* in the bitmasks: `STRING:6/7 NUMERIC:4/4 TIMEDATE:3/5 SYSTEM:1/2 (broken: LENGTH, YEAR, MONTH, DATABASE)`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/IscDbc/SupportFunctions.cpp#L71 and https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/IscDbc/SupportFunctions.cpp#L137

```cpp
// SupportFunctions.cpp — column 3 is the ODBC name searched for,
// column 4 the Firebird replacement.
ADD_SUPPORT_FN( STR_FN, SQL_FN_STR_LENGTH,   "LENGTH", "LENGTH", defaultTranslator);
ADD_SUPPORT_FN( SYS_FN, SQL_FN_SYS_DBNAME,   "DBNAME", "DBNAME", defaultTranslator);
```

**Why this classification**: `LENGTH` is mapped to itself, but Firebird has no
`LENGTH()` — the SQL-standard spelling is `CHAR_LENGTH`, which is registered
two lines above. `SQL_FN_SYS_DBNAME` is the bitmask constant; the *function
name* the ODBC spec defines for it is `DATABASE`, not `DBNAME`, so
`translateNativeFunction`'s lookup (`SupportFunctions.cpp:146-165`) never
matches and the escape passes through verbatim. Both bits are set in the
`SQL_*_FUNCTIONS` masks the driver returns, so this is advertised capability
that fails at execute time, not an undeclared gap.

**Suggested fix**: change the `LENGTH` replacement to `CHAR_LENGTH` and register
`SQL_FN_SYS_DBNAME` under the ODBC name `DATABASE` (mapping to
`RDB$GET_CONTEXT('SYSTEM','DB_NAME')`); alternatively clear the two bits from
the bitmask so the driver stops claiming them.

### RC2 — `{d '…'}` is stripped to a bare string literal instead of a DATE literal (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLExecDirect` (escape translation)
- **Conformance**: Core
- **Affects**: `test_datetime_scalar_functions` (1 test; also the `YEAR`/`MONTH` half of `test_scalar_function_claim_vs_execute`)
- **Evidence**: `SELECT {fn YEAR({d '2026-01-15'})} FROM RDB$DATABASE` fails with `-105 Specified EXTRACT part does not exist in input datatype`. `YEAR`, `MONTH` and `DAYOFWEEK` *are* mapped correctly (to ` extract(year from `, etc., `SupportFunctions.cpp:119-133`); the argument they receive is the problem — `EXTRACT` is being handed a `VARCHAR`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/IscDbc/IscConnection.cpp#L1707-L1731

```cpp
// getNativeSql(): every non-{fn escape is handled by advancing past the
// keyword and copying the body through — the {d,t,ts} branch emits the
// quoted string with no DATE/TIME/TIMESTAMP keyword and no CAST.
else if ( !strncasecmp ( ptOut, "{TS", 3 ) )      ptIn += 2; // 'ts'
else if ( !strncasecmp ( ptOut, "{D", 2 ) || !strncasecmp ( ptOut, "{T", 2 ) )
    ptIn += 1; // 'd', 't'
```

**Why this classification**: the escape processor walks brace groups
right-to-left, so `{d '2026-01-15'}` is rewritten first, to ` '2026-01-15'`.
Firebird will implicitly convert that string in most contexts, which is why the
`{d}` escape passes standalone — but `EXTRACT` requires a real date operand and
rejects it. ODBC Appendix C requires the date escape to be translated to the
DBMS's date literal, not to a character literal that happens to convert most of
the time.

**Suggested fix**: emit `DATE '…'` / `TIME '…'` / `TIMESTAMP '…'` (or an
explicit `CAST(… AS DATE)`) in that branch rather than dropping the escape
keyword.

### RC3 — `SQLNativeSql` refuses `{CALL …}` for a procedure the catalog does not contain (`BUG_IN_CRUSHER`, 1 finding)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Affects**: `test_call_escape_format_variants` (1 test)
- **Evidence**: `0/7 CALL variants translated`, for all seven spec forms.
- **Citation**: `src/tests/escape_sequence_tests.cpp:1057-1064` (the variant list), with the driver-side behaviour at https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/IscDbc/IscConnection.cpp#L1640-L1646

```cpp
// src/tests/escape_sequence_tests.cpp:1057
static const char* variants[] = {
    "{CALL proc}", "{CALL proc()}", "{CALL proc(?)}", "{CALL proc(?,?)}",
    "{?=CALL func}", "{?=CALL func(?)}", "{?=CALL func(?,?)}",
};
```

**Why this classification**: the probe asks the driver to translate calls to
procedures literally named `proc` and `func`, which exist in no database. The
Firebird driver cannot translate a `{CALL}` escape without resolving the name —
it needs the parameter count to choose between `execute procedure` and
`select * from`, and throws `Unknown procedure 'PROC'` when the lookup fails,
which surfaces as `SQL_ERROR` and is scored "not translated". ODBC lists
`42000` among `SQLNativeSql`'s valid SQLSTATEs, so refusing an unresolvable
call is spec-legal, and the assertion as written can never pass on this driver
no matter how correct it becomes. The counter-example is in the same file: the
four sibling probes immediately below use `find_named_procedure`
(`escape_sequence_tests.cpp:1105-1128`) to discover a real procedure and skip
when there is none.

**Suggested fix**: build the seven variants around a procedure discovered via
`find_named_procedure` (skipping when none exists), or accept a `42000`/`HY000`
"unknown procedure" as evidence the driver parsed the escape.

### RC4 — Diagnostic header fields are 32-bit writes into 64-bit slots, and are never populated (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLGetDiagField`
- **Conformance**: Core
- **Affects**: `test_diagfield_row_count`, `test_diagfield_dynamic_function` (2 tests)
- **Evidence**: `SQL_DIAG_ROW_COUNT = -4294967296; SQLRowCount = -1`, and `SQL_DIAG_DYNAMIC_FUNCTION_CODE = 0 (expected 85, SQL_DIAG_SELECT_CURSOR)`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcObject.cpp#L329-L349 (field declarations at https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcObject.h#L85-L89)

```cpp
case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
    *(SQLINTEGER*)ptr = sqlDiagDynamicFunctionCode;   // always 0
    return SQL_SUCCESS;
...
case SQL_DIAG_ROW_COUNT:
    *(SQLINTEGER*)ptr = sqlDiagRowCount;              // always 0, and 4 bytes wide
    return SQL_SUCCESS;
```

**Why this classification**: two independent defects, one root. First, the
width: `SQL_DIAG_ROW_COUNT` is an `SQLLEN`, 8 bytes on a 64-bit build, and the
driver stores 4. The probe pre-initialises its variable to `-1`
(`src/tests/diagnostic_depth_tests.cpp:165`), the driver overwrites the low
word with `0`, and `0xFFFFFFFF00000000` is exactly the `-4294967296` reported —
so the value is not merely wrong, four bytes of the caller's variable were left
untouched. Second, the wiring: `sqlDiagRowCount` and `sqlDiagDynamicFunctionCode`
are assigned `0` at construction and reset (`OdbcObject.cpp:48`, `:51`, `:254`,
`:257`) and nowhere else in the tree, so both fields are constants.
`SQL_DIAG_DYNAMIC_FUNCTION_CODE = 0` is `SQL_DIAG_UNKNOWN_STATEMENT` — "I do
not know what this statement was" — reported for a `SELECT` the driver had just
parsed and executed.

**Suggested fix**: store through `*(SQLLEN*)ptr` for `SQL_DIAG_ROW_COUNT`, and
set both fields at the end of statement execution from the statement's own row
count and statement kind.

### RC5 — Every engine exception is reported as `HY000` (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLExecDirect`
- **Conformance**: Core
- **Affects**: `test_execdirect_syntax_error` (1 test)
- **Evidence**: `SQL_ERROR but SQLSTATE=HY000 (expected 42000)`. The same substitution is visible in the diagnostics of RC1, RC2 and RC10, where genuine `-104 Token unknown` syntax errors also arrive as `HY000`.
- **Citation**: `postError ("HY000", ex)` appears 60 times in https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcStatement.cpp ; the ISO table at https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcError.cpp#L375 defines `42000 "Syntax error or access violation"` but nothing in the driver ever posts it.

**Why this classification**: the SQLSTATE is chosen by the catch site, not
derived from the Firebird error code, so a syntax error and an out-of-memory
condition are indistinguishable to the application. `HY000` is the general-error
fallback; ODBC requires `42000` for invalid SQL, and callers that branch on
SQLSTATE (every driver manager and most ORMs) cannot classify anything this
driver reports.

**Suggested fix**: derive the SQLSTATE from the Firebird `isc` status vector at
`postError` time — the `CODE_ISO` table in `OdbcError.cpp` already holds the
mappings; only the lookup is missing.

### RC6 — Spec-mandated error checks are absent (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLSetConnectAttr`, `SQLCloseCursor`
- **Conformance**: Core
- **Affects**: `test_setconnattr_invalid_attr`, `test_closecursor_no_cursor` (2 tests)
- **Evidence**: `SQLSetConnectAttr accepted invalid attribute 99999`; `SQLCloseCursor succeeded with no open cursor`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcConnection.cpp#L485-L488 and https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcStatement.cpp#L2510-L2531

```cpp
// OdbcConnection::sqlSetConnectAttr — the switch has no `default:` label,
// so an unrecognised attribute falls straight out of it.
	}

	return sqlSuccess();
}
```

**Why this classification**: `SQLSetConnectAttr` must return `HY092` for an
attribute it does not recognise — the driver has the string
(`OdbcConnection.cpp:508`) but only uses it inside one specific case.
`SQLCloseCursor` calls `releaseResultSet()` unconditionally and returns
`sqlSuccess()`; ODBC requires `24000` when no cursor is open, and applications
use precisely that return to decide whether a cursor needs closing.

**Suggested fix**: add a `default:` arm returning `HY092` to the
`sqlSetConnectAttr` switch, and a cursor-state test at the top of
`sqlCloseCursor` returning `24000`.

### RC7 — `SQL_ATTR_ASYNC_ENABLE` is accepted on set and hard-coded OFF on get (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)`
- **Conformance**: Level 2
- **Affects**: `test_async_capability` (1 test)
- **Evidence**: `SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE, ON) returned success but SQLGetStmtAttr still reports OFF`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcStatement.cpp#L3610-L3613 vs https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcStatement.cpp#L2612-L2615

```cpp
// set — stores the request and returns success
case SQL_ATTR_ASYNC_ENABLE:			// 4
    asyncEnable = (intptr_t) ptr == SQL_ASYNC_ENABLE_ON;
// get — ignores the stored value
case SQL_ATTR_ASYNC_ENABLE:
    value = 0;							// driver doesn't do async
```

**Why this classification**: the driver knows it does not implement async — the
getter says so in a comment — but the setter reports unqualified success. ODBC
gives a driver two legal responses to an unsupported attribute value: reject it
(`HY092`/`HYC00`), or substitute and warn with `01S02`. Returning `SQL_SUCCESS`
and then silently doing something else is the one option not available, and an
application that trusts the return will wait on completion notifications that
never arrive.

**Suggested fix**: in the setter, return `SQL_ERROR` with `HYC00` when the value
is `SQL_ASYNC_ENABLE_ON` — or accept it, return `SQL_SUCCESS_WITH_INFO` with
`01S02`, and make the getter report the substituted value.

### RC8 — Truncated `SQLGetInfo` reports the written length, not the length available (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLGetInfo` (`SQLGetInfoW`)
- **Conformance**: Core
- **Affects**: `test_string_truncation_wchar` (1 test)
- **Evidence**: `Truncation returned 01004, needed 26 bytes, buffer was 28 bytes (full=56)`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/MainUnicode.cpp#L552-L558

```cpp
ConvertingString<> InfoValue( bufferLength, (SQLWCHAR *)infoValue, stringLength );
InfoValue.setConnection( (OdbcConnection*)hDbc );

return ((OdbcConnection*) hDbc)->sqlGetInfo( infoType, (SQLPOINTER)(SQLCHAR*)InfoValue,
                                             InfoValue.getLength(), stringLength );
```

**Why this classification**: the wide wrapper hands the ANSI `sqlGetInfo` a byte
budget derived from the caller's buffer, and the length it propagates back is
the length of what was written after conversion — 13 characters, 26 bytes — not
the 56 bytes the full value needs. ODBC defines `*StringLengthPtr` as "the total
number of bytes available to return", which is what makes the two-call
allocate-then-fetch idiom work. A reported length *smaller* than the buffer
that just overflowed is self-contradictory: an application that resizes to 26
bytes and calls again gets truncated again, forever.

**Suggested fix**: carry the untruncated source length through the
`ConvertingString` conversion and report `full_chars * sizeof(SQLWCHAR)` when
`01004` is posted, independently of how much was copied.

### RC9 — `PARAMSET_SIZE=1` never writes the params-processed or param-status outputs (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLSetStmtAttr` / `SQLExecute`
- **Conformance**: Core
- **Affects**: `test_paramset_size_one` (1 test)
- **Evidence**: `Execute returned 0; processed=0; status=65535`. `0xFFFF` is the probe's own sentinel (`src/tests/array_param_tests.cpp:927`), so the status array was never touched; `SQLExecute` nonetheless returned `SQL_SUCCESS`.
- **Citation**: https://github.com/fdcastel/firebird-odbc-driver/blob/v3.5.1-rc2/OdbcStatement.cpp#L444-L445

```cpp
else if ( statement->isActiveModify() && applicationParamDescriptor->headArraySize > 1 )
    execute = &OdbcStatement::executeStatementParamArray;
```

**Why this classification**: `executeStatementParamArray`
(`OdbcStatement.cpp:3052-3057`) is the only code that writes
`headRowsProcessedPtr` and `headArrayStatusPtr`, and the dispatcher selects it
only when the parameter-set size exceeds 1. Both pointers *are* stored when the
application sets them (`OdbcStatement.cpp:3514`, `:3660`), so the driver accepts
the contract and then does not honour it on the default path. ODBC does not
special-case a set size of one: with `SQL_ATTR_PARAMS_PROCESSED_PTR` set, the
driver must report 1 processed and write `SQL_PARAM_SUCCESS` into element 0.

**Suggested fix**: write `*headRowsProcessedPtr = 1` and
`headArrayStatusPtr[0] = SQL_PARAM_SUCCESS` on the single-execute path too, or
route `headArraySize == 1` through `executeStatementParamArray`.

### RC10 — Two `CRITICAL` round-trip probes hardcode `NVARCHAR` DDL (`BUG_IN_CRUSHER`, 2 findings)

- **Function**: `SQLBindParameter`
- **Conformance**: Core
- **Affects**: `test_bindparam_int_to_wvarchar_roundtrip`, `test_bindparam_bigint_to_wvarchar_roundtrip` (2 tests)
- **Evidence**: `Could not CREATE TABLE ODBC_TEST_ROUNDTRIP_WCHAR (ID INTEGER, VAL NVARCHAR(20))` → `-104 Token unknown — line 1, column 61 — (`. Firebird spells this type `NCHAR VARYING` or `NATIONAL CHARACTER VARYING`; it has no `NVARCHAR`, so the parser reads the word as an identifier and stops at the `(`.
- **Citation**: `src/tests/param_binding_tests.cpp:763` and `src/tests/param_binding_tests.cpp:833`

```cpp
// param_binding_tests.cpp:763 — one DDL spelling, no fallback
    "ODBC_TEST_ROUNDTRIP_WCHAR", "NVARCHAR(20)", 20, false);
```

**Why this classification**: these are the only two probes covering the numeric
C → `SQL_WVARCHAR` conversion cells, both rated `CRITICAL`, and on Firebird
neither ever reaches `SQLBindParameter` — the run reports a skip and the cell
goes unmeasured. The counter-example is `src/tests/unicode_tests.cpp:601`, which
tests the same engine over the same connection and gets its table because it
uses the helper built for exactly this: `RoundTripTableGuard::create_first_working(conn_, table, {"NVARCHAR(64)", "VARCHAR(64)"})`.
`src/tests/test_base.hpp:788` documents that helper as "the recurring shape is
try `NVARCHAR(64)`, fall back to `VARCHAR(64)`". The driver did nothing wrong
here; it rejected DDL that is not Firebird SQL.

**Suggested fix**: route `run_int_to_string_roundtrip`'s table creation through
`RoundTripTableGuard::create_first_working` with
`{"NVARCHAR(20)", "NCHAR VARYING(20)", "VARCHAR(20)"}`, and keep the skip only
for engines where every variant fails.

### RC11 — No table in the CI database, so every table-dependent catalog probe skipped (`INCONCLUSIVE`, 4 findings)

- **Function**: `SQLTables` (via `TestBase::discover_tables`)
- **Conformance**: Core / Level 1
- **Affects**: `test_count_star_result_metadata`, `test_statistics_result`, `test_privileges_result`, `test_outer_join_escape` (4 tests, across 3 categories)
- **Evidence**: all four report a variant of "SQLTables reported no tables, so there is nothing to ask about". Note that `test_outer_join_escape` *did* get its capability bitmask back (`LEFT RIGHT FULL NESTED NOT_ORDERED INNER ALL_COMPARISON_OPS`) and failed only on the join target.
- **Citation**: `src/tests/test_base.cpp:468-499`

**Why this classification**: `discover_tables` calls `SQLTables` with every
filter `NULL` and creates nothing; the crusher fixtures that do exist
(`ODBC_TEST_ROUNDTRIP*`) are created and dropped inside the probes that need
them, so at the time these four ran the database plausibly held no user table
at all. That makes "the database was empty" and "`SQLTables` under-reports"
observationally identical from this run, and nothing in the artifact
distinguishes them. Resolving it needs one persistent table in the CI database,
not a source reading.

**Suggested fix**: have the CI setup create a small permanent table (and index,
for `SQLStatistics`) before the run; if the probes still report nothing, the
same four findings become a `BUG_IN_DRIVER` cluster with no further work.

### RC12 — `MOCK_INOUT` / `MOCK_FN` absent, so all procedure-parameter-direction probes skipped (`INCONCLUSIVE`, 4 findings)

- **Function**: `SQLBindParameter` (`SQL_PARAM_INPUT` / `OUTPUT` / `INPUT_OUTPUT`), `SQLPrepare`/`SQLExecute`
- **Conformance**: Core
- **Affects**: `test_call_escape_in_parameter`, `test_call_escape_out_parameter`, `test_function_call_escape_return_value`, `test_call_escape_inout_parameter` (4 tests, three of them `ERROR` severity)
- **Evidence**: `Test procedure MOCK_INOUT not visible via SQLProcedures` (×3) and `Test function MOCK_FN not visible via SQLProcedures`.
- **Citation**: `src/tests/escape_sequence_tests.cpp:1105-1128` (`find_named_procedure`) — the probes skip by design when the catalog has no match.

**Why this classification**: the skip is correct behaviour, and there is no
driver evidence either way — the driver was never asked to bind an output
parameter. This is the largest unmeasured area in the run: nothing else in the
suite exercises `SQL_PARAM_OUTPUT` or `SQL_PARAM_INPUT_OUTPUT` against this
driver, and `IscCallableStatement.cpp` is therefore entirely untested here.

**Suggested fix**: register the two fixtures the suggestions name —
`MOCK_INOUT(IN n INTEGER, OUT m INTEGER, INOUT s VARCHAR(64))` with `m := n*2`
and `s := UPPER(s)`, and `MOCK_FN(a INTEGER, b INTEGER) RETURNS INTEGER`
returning `a*10 + b` — in the CI database setup.

## All findings

| # | Test | Status | Severity | Root cause | Classification |
|---|---|---|---|---|---|
| 1 | `test_count_star_result_metadata` | SKIP_INCONCLUSIVE | INFO | RC11 | `INCONCLUSIVE` |
| 2 | `test_async_capability` | FAIL | ERROR | RC7 | `BUG_IN_DRIVER` |
| 3 | `test_execdirect_syntax_error` | FAIL | WARNING | RC5 | `BUG_IN_DRIVER` |
| 4 | `test_setconnattr_invalid_attr` | FAIL | WARNING | RC6 | `BUG_IN_DRIVER` |
| 5 | `test_closecursor_no_cursor` | FAIL | WARNING | RC6 | `BUG_IN_DRIVER` |
| 6 | `test_string_truncation_wchar` | FAIL | WARNING | RC8 | `BUG_IN_DRIVER` |
| 7 | `test_statistics_result` | SKIP_INCONCLUSIVE | INFO | RC11 | `INCONCLUSIVE` |
| 8 | `test_privileges_result` | SKIP_INCONCLUSIVE | INFO | RC11 | `INCONCLUSIVE` |
| 9 | `test_diagfield_row_count` | FAIL | INFO | RC4 | `BUG_IN_DRIVER` |
| 10 | `test_diagfield_dynamic_function` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 11 | `test_bindparam_int_to_wvarchar_roundtrip` | SKIP_INCONCLUSIVE | CRITICAL | RC10 | `BUG_IN_CRUSHER` |
| 12 | `test_bindparam_bigint_to_wvarchar_roundtrip` | SKIP_INCONCLUSIVE | CRITICAL | RC10 | `BUG_IN_CRUSHER` |
| 13 | `test_paramset_size_one` | FAIL | INFO | RC9 | `BUG_IN_DRIVER` |
| 14 | `test_string_scalar_functions` | FAIL | WARNING | RC1 | `BUG_IN_DRIVER` |
| 15 | `test_datetime_scalar_functions` | FAIL | WARNING | RC2 | `BUG_IN_DRIVER` |
| 16 | `test_system_scalar_functions` | FAIL | WARNING | RC1 | `BUG_IN_DRIVER` |
| 17 | `test_outer_join_escape` | SKIP_INCONCLUSIVE | INFO | RC11 | `INCONCLUSIVE` |
| 18 | `test_call_escape_format_variants` | FAIL | WARNING | RC3 | `BUG_IN_CRUSHER` |
| 19 | `test_call_escape_in_parameter` | SKIP_INCONCLUSIVE | INFO | RC12 | `INCONCLUSIVE` |
| 20 | `test_call_escape_out_parameter` | SKIP_INCONCLUSIVE | ERROR | RC12 | `INCONCLUSIVE` |
| 21 | `test_function_call_escape_return_value` | SKIP_INCONCLUSIVE | ERROR | RC12 | `INCONCLUSIVE` |
| 22 | `test_call_escape_inout_parameter` | SKIP_INCONCLUSIVE | ERROR | RC12 | `INCONCLUSIVE` |
| 23 | `test_scalar_function_claim_vs_execute` | FAIL | WARNING | RC1 (+RC2) | `BUG_IN_DRIVER` |

Finding 23 spans two clusters: its `broken:` list names `LENGTH` and `DATABASE`
(RC1) alongside `YEAR` and `MONTH` (RC2). It is counted once, under RC1.

## Caveats

- `test_scalar_function_claim_vs_execute` (23) is counted under RC1 but also
  evidences RC2; the classification counts are per finding, not per defect.
- RC8's citation is the wide-entry-point wrapper. The underlying
  `ConvertingString` conversion was not read line by line — the reported length
  is demonstrably the post-conversion written length rather than the available
  length, but the exact statement that computes it was not located within
  budget.
