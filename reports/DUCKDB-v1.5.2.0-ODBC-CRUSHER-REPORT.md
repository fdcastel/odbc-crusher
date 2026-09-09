# `DuckDB ODBC` v`1.5.2.0` — Crusher Triage Report

**Generated**: 2026-09-09 07:17 UTC
**CI run**: https://github.com/fdcastel/odbc-crusher/actions/runs/34323148605
**Driver source**: https://github.com/duckdb/duckdb-odbc @ `v1.5.2.0`
**Crusher commit**: d5f237b

## Run integrity

| Check | Result |
|---|---|
| Report complete | ✅ — `PARTIAL_REPORT=false` |
| Category filter | full run — 23 of 23 categories |
| Discovery phase | ❌ **CRASHED** — `driver_info`, `type_info`, `function_info` and `scalar_functions` all unavailable |
| Driver crashes | `4` categories — Descriptor Tests, Data Type Edge Cases, Parameter Binding Tests, Numeric Struct Tests |
| Version provenance | `UNVERIFIABLE` — `actual_version.txt` is an echo of the manifest, and this driver's `SQLGetInfo(SQL_DRIVER_VER)` is documented unreliable (it returned `''`). Nothing independently confirms the binary is 1.5.2.0 — the pinned download URL is the only assurance. |
| Report schema | `1` |

4 categories crashed; every probe in them was discarded, so the totals below
count only what survived and the pass rate is optimistic by an unknown margin.
The discovery phase crashed *as well* — that is earlier and worse than a
category crash, and it is written up as RC1 below rather than folded into the
count of four.

`PROVENANCE=UNVERIFIABLE` does not mean the wrong binary was tested. The
download URL in `.github/drivers.json` is pinned to the
`v1.5.2.0` release asset, so the binary is almost certainly right; what is
missing is any independent confirmation *from the run itself*. Both halves of
the cross-check are inert for this driver: the version file is an echo of the
manifest, and the driver's runtime version claim was unavailable because
discovery crashed. RC12 shows that `SQL_DBMS_VER`, the fallback the manifest
recommends, is itself broken at this version — so there is currently no
runtime way to identify a DuckDB ODBC build.

## Summary

| Metric | Value |
|---|---|
| Total tests | `155` |
| Passed | `101` |
| Failed | `23` |
| Errors | `8` |
| Skipped | `12` |
| Informational (not scored) | `11` |
| Scored | `144` |
| Pass rate | `70.1` % |

Pass rate is `passed / scored`, where `scored = total − informational`.
Informational probes record what the driver said but have no right answer, so
they are excluded from the denominator rather than counted as passes.

## Classification breakdown

| Label | Findings | Clusters |
|---|---:|---:|
| `BUG_IN_DRIVER` | 28 (+1 run-level) | 12 |
| `BUG_IN_CRUSHER` | 5 | 1 |
| `DRIVER_LIMITATION` | 5 | 1 |
| `INCONCLUSIVE` | 0 | 0 |

The `+1 run-level` is the discovery-phase crash (RC1). It is not an entry in
`findings.json` — crusher records it as a run-level flag — but it is a
finding, and the most severe one here.

## Punch list

1. **Fix the discovery-phase SIGSEGV (RC1)** — a driver that faults inside
   `SQLGetInfo`/`SQLGetTypeInfo`/`SQLGetFunctions` fails against ordinary ODBC
   clients on connect, before any query is issued; every BI tool and driver
   manager makes those calls first.
2. **Fix `SQLBindParameter` treating a null `StrLen_or_IndPtr` as SQL NULL (RC3)**
   — silent data corruption on the most common binding idiom in ODBC; a bound
   `42` was stored and returned as NULL.
3. **Fix the four category SIGSEGVs (RC2)** — descriptors, data-type edges,
   parameter binding and `SQL_NUMERIC_STRUCT` each take the process down, and
   every probe result in those categories was discarded.
4. **Stop advertising escape-sequence support the driver does not have (RC4)** —
   11 findings, one cause: there is no escape parser anywhere in the ODBC layer,
   while `SQLGetInfo` claims 14 string, 5 numeric and 17 datetime functions,
   full outer-join capability and 16 datetime literal forms.
5. **Crusher: give the DuckDB job a file-backed `Database=` (RC5)** — five
   findings are artifacts of the harness handing DuckDB a per-connection
   `:memory:` database; no driver behaviour could make them pass.

## Root causes

### RC1 — The driver faults during discovery (`BUG_IN_DRIVER`, run-level)

- **Function**: `SQLGetInfo` / `SQLGetTypeInfo` / `SQLGetFunctions`
- **Conformance**: Core
- **Affects**: the whole run — `driver_info`, `type_info`, `function_info` and
  `scalar_functions` are absent from the report; `REPORTED_VERSION` is empty
- **Evidence**: `DISCOVERY_CRASHED=true`. Crusher wraps the three discovery
  calls in a single crash guard and they faulted as a group. The fault address
  is unknown: the guard trades the stack for the ability to continue the run.
- **Citation**: crusher-side `src/main.cpp:309` — the guard's own comment
  already records this as previously observed behaviour: *"Wrapped in crash
  guard because some drivers (e.g. DuckDB on Linux) can SIGSEGV during
  SQLGetTypeInfo or SQLGetInfo."*

**Why this classification**: A segmentation fault inside the driver is a defect
regardless of whether the faulting line can be named. This one is worse than a
category crash because it happens on the path every ODBC application takes
before it does anything useful — a driver manager, Excel, Power BI or Tableau
will hit it on connect. It also explains the degraded provenance: the driver's
own capability claims never made it into the report.

**Suggested fix**: Reproduce under a debugger with `SQLGetTypeInfo` over all
types followed by a full `SQLGetInfo` sweep; RC12 shows `SQL_DBMS_VER` already
allocates a nested statement inside `SQLGetInfo` and reads from a buffer it
never fills, which is the shape of bug worth ruling out first.

### RC2 — Four categories killed by SIGSEGV (`BUG_IN_DRIVER`, 4 findings)

- **Function**: N/A (crash entries carry no function)
- **Conformance**: Core
- **Affects**: `Descriptor Tests`, `Data Type Edge Cases`,
  `Parameter Binding Tests`, `Numeric Struct Tests` (4 crash entries, and every
  probe result in those four categories)
- **Evidence**: `Segmentation fault (SIGSEGV)` in each; the category object
  holds only the crash entry, so the probes that had already passed or failed
  in those categories were discarded with it.
- **Citation**: crusher-side `src/tests/descriptor_tests.cpp`,
  `src/tests/datatype_edge_tests.cpp`, `src/tests/param_binding_tests.cpp`,
  `src/tests/numeric_struct_tests.cpp` — the crash guard is per category, so
  the report can name the category but not the address.

**Why this classification**: A crash is always a driver bug. Two of the four
categories have a plausible entry point already visible in source: the
`SQL_C_NUMERIC` conversion at
https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/statement/statement_functions.cpp#L482
indexes `str_val[0]` without checking that the string is non-empty, and the
parameter path is the same code RC3 and RC13 fault-find below. Crusher's own
`src/tests/statement_tests.cpp:154` records a third: *"some drivers (e.g.
DuckDB) crash on SQLDescribeParam when the parameter type cannot be inferred
from a bare `SELECT ?`"*.

**Suggested fix**: Run each of the four categories under ASan; the parameter
and numeric paths share `ParameterDescriptor`, so one fix may close more than
one crash.

### RC3 — A null `StrLen_or_IndPtr` is treated as SQL NULL (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLBindParameter`
- **Conformance**: Core
- **Affects**: `test_parameter_binding` (1 test, severity CRITICAL)
- **Evidence**: `Bound parameter 42 came back as 0 (indicator=-1)`. The probe
  binds `SQL_C_SLONG`/`SQL_INTEGER` with `StrLen_or_IndPtr = nullptr`, which
  the spec defines as *"the driver assumes that all input parameter values are
  non-NULL"*. The driver instead binds SQL NULL.
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/parameter_descriptor.cpp#L243-L252

```cpp
if (sql_data_ptr == nullptr && sql_ind_ptr == nullptr) {
    return SQL_ERROR;
}

auto sql_ind_ptr_val_set = GetSQLDescIndicatorPtr(*apd_record, val_idx);
if (sql_data_ptr == nullptr || sql_ind_ptr == nullptr || *sql_ind_ptr_val_set == SQL_NULL_DATA) {
    Value val_null(nullptr);          // <-- no indicator supplied => NULL
    SetValue(val_null, rec_idx);
    return SQL_SUCCESS;
}
```

**Why this classification**: ODBC 3.8 `SQLBindParameter` makes the indicator
pointer optional for fixed-length C types; a null pointer means "not NULL",
not "NULL". The driver conflates "the application supplied no indicator" with
"the application supplied `SQL_NULL_DATA`", and returns `SQL_SUCCESS` while
doing it — so the application is never told. This is silent data loss on the
most common way to bind an integer.

**Suggested fix**: Split the two conditions — when `sql_ind_ptr == nullptr`,
fall through to the type switch and treat the value as non-NULL; keep the NULL
path for `*sql_ind_ptr_val_set == SQL_NULL_DATA` only.

### RC4 — No ODBC escape-sequence support, but `SQLGetInfo` advertises it (`BUG_IN_DRIVER`, 11 findings)

- **Function**: `SQLNativeSql`, `SQLExecDirect`, `SQLGetInfo(SQL_*_FUNCTIONS / SQL_OJ_CAPABILITIES / SQL_DATETIME_LITERALS)`
- **Conformance**: Core (Level 1 for `test_outer_join_escape`, Level 2 for `test_interval_literal_escape`)
- **Affects**: `test_native_sql_scalar_functions`, `test_native_sql_datetime_literals`,
  `test_native_sql_outer_join_escape`, `test_call_escape_format_variants`,
  `test_string_scalar_functions`, `test_numeric_scalar_functions`,
  `test_datetime_scalar_functions`, `test_datetime_literal_escapes`,
  `test_outer_join_escape`, `test_interval_literal_escape`,
  `test_scalar_function_claim_vs_execute` (11 tests)
- **Evidence**: two symptom families with one cause.
  * `SQLNativeSql` returns `HYC00 "SQLNativeSql is not implemented"` — 0/7 CALL
    variants and 0/3 datetime literals translated.
  * Nothing translates escapes at execution time either, so `{fn UCASE('hello')}`
    reaches the DuckDB parser verbatim and comes back
    `42000 syntax error at or near "UCASE"`. `test_scalar_function_claim_vs_execute`
    puts the number on it: `STRING:0/7 NUMERIC:0/5 TIMEDATE:0/3`, with
    UCASE, LCASE, LENGTH, LTRIM, RTRIM, CONCAT, SUBSTRING, ABS, FLOOR, CEILING,
    SQRT, ROUND, NOW, YEAR and MONTH all claimed and all broken.
- **Citations**:
  * stub — https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/empty_api_stubs.cpp#L451 (helper at [#L433-L436](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/empty_api_stubs.cpp#L433-L436))
  * claimed as supported — https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/api_info.cpp#L29 (`SQL_API_SQLNATIVESQL` in `ODBC3_EXTRA_SUPPORTED_FUNCTIONS`)
  * capability claims — https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L832-L840 (string), [#L684](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L684) (numeric), [#L861-L869](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L861-L869) (timedate), [#L704-L708](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L704-L708) (outer join), [#L383-L394](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L383-L394) (datetime literals)

```cpp
// info_api.cpp:832 — claimed
case SQL_STRING_FUNCTIONS: {
    SQLUINTEGER mask = SQL_FN_STR_ASCII | SQL_FN_STR_BIT_LENGTH | SQL_FN_STR_CONCAT | SQL_FN_STR_LCASE |
                       SQL_FN_STR_LEFT | SQL_FN_STR_LENGTH | SQL_FN_STR_LOCATE | SQL_FN_STR_LTRIM | ...
```

**Why this classification**: An unimplemented optional feature is a limitation;
one the driver advertises and then fails is a bug. Every bit in these masks is
hardcoded and nothing in `src/odbc_api/` or `src/odbc_driver/` parses `{fn`,
`{d`, `{ts`, `{oj` or `{CALL` (a grep for escape handling across the ODBC layer
returns nothing). `SQLGetFunctions` reports `SQLNativeSql` as supported while
the entry point is a `SetNotImplemented` stub — an application that asks first
and calls second still gets `HYC00`.

**Suggested fix**: Either implement the escape parser in `SQLNativeSql` and run
every statement through it before handing it to DuckDB, or — much cheaper —
zero the four capability masks and drop `SQL_API_SQLNATIVESQL` from
`ODBC3_EXTRA_SUPPORTED_FUNCTIONS` so clients can route around the gap instead
of generating SQL the driver rejects.

### RC5 — The CI connection string gives every connection a private in-memory database (`BUG_IN_CRUSHER`, 5 findings)

- **Function**: `SQLDisconnect`/`SQLConnect`, `SQLTables`, `SQLColumns`
- **Conformance**: Core / Level 1
- **Affects**: `test_reconnected_handle_is_usable`,
  `test_disconnect_with_open_transaction`, `test_uncommitted_row_isolation`,
  `test_columns_catalog`, `test_count_star_result_metadata` (5 tests)
- **Evidence**: three of the five fail with the same diagnostic shape —
  `[42000] Catalog Error: Table with name ODBC_CRUSHER_REUSE_CONN does not exist!`
  (also `..._DISCONNECT_TX`, `..._ISOLATION`). Each of those probes creates its
  table on the *primary* connection and then works on a *sibling* connection.
  The other two report an empty catalog (`SQLTables returned no tables`).
- **Citations**: crusher-side `.github/drivers.json` →
  `drivers.duckdb.conn_string = "Driver={DuckDB Driver};"` (no `Database=`),
  and `src/tests/connection_tests.cpp:262`,
  `src/tests/transaction_tests.cpp:668`, `src/tests/transaction_tests.cpp:778`.
  Driver-side confirmation:
  https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/connect/connect.cpp#L148
  and
  https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/connect/connect.cpp#L187

```cpp
std::string database = GetOptionFromConfigMap("database", IN_MEMORY_PATH);   // L148
...
bool cache_instance = database != IN_MEMORY_PATH;                            // L187
dbc->env->db = instance_cache.GetOrCreateInstance(database, config, cache_instance, ...);
```

**Why this classification**: The probes are testing a contract the configured
data source cannot offer. With no `Database=` the driver opens `:memory:`, and
`cache_instance` is deliberately **false** for in-memory paths — so every
`SQLConnect` gets its own database object. Counter-example: the sibling
connection in `test_reconnected_handle_is_usable` would fail to see the
primary's table even against a perfectly conformant driver, because the two
handles are attached to different databases; and after `SQLDisconnect` the
database is gone entirely, so "does the reconnected handle still work" cannot
be asked this way. The two metadata probes see nothing for the same reason: a
fresh scratch database has no user tables. No driver behaviour changes any of
these outcomes.

**Suggested fix**: In `.github/drivers.json`, point the DuckDB job at a
file-backed database (`Driver={DuckDB Driver};Database=/tmp/crusher.duckdb;`).
Independently, the multi-connection probes should self-check — create on the
primary, look for it from the sibling, and `SKIP_UNSUPPORTED` when the two
handles do not share a catalog — so this reads as "not applicable" rather than
as three ERRORs against any driver with a per-connection data source.

### RC6 — `SQLEndTran(SQL_ROLLBACK)` fails, and every failure is reported as HY115 (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLEndTran(SQL_ROLLBACK)`
- **Conformance**: Core
- **Affects**: `test_manual_rollback`, `test_rollback_with_open_cursor` (2 tests)
- **Evidence**: `SQLEndTran(SQL_ROLLBACK) rc=-1 [HY115]` after autocommit was
  turned off and a row inserted; and `fetch_rc=0 rollback_rc=-1` when a cursor
  is mid-fetch.
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/transaction_api.cpp#L29-L52

```cpp
case SQL_COMMIT:
    // it needs to materialize the result set because ODBC can still fetch after a commit
    if (dbc->MaterializeResult() != SQL_SUCCESS) { ... }
    ...
case SQL_ROLLBACK:
    try {
        dbc->conn->Rollback();                       // no MaterializeResult() here
        return SQL_SUCCESS;
    } catch (std::exception &ex) {
        duckdb::ErrorData parsed_error(ex);
        return duckdb::SetDiagnosticRecord(dbc, SQL_ERROR, "SQLEndTran", parsed_error.RawMessage(),
                                           SQLStateType::ST_HY115, dbc->GetDataSourceName());
    }
```

**Why this classification**: Two defects, one site. First, the SQLSTATE is
wrong for any cause: the driver's own table defines `HY115` as *"SQLEndTran is
not allowed for an environment that contains a connection with asynchronous
function execution enabled"*
([include/odbc_diagnostic.hpp#L120](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/include/odbc_diagnostic.hpp#L120)),
and crusher never enables async execution — every rollback exception is funnelled
into that one state, so the application's error handling is misinformed.
Second, the asymmetry with the `SQL_COMMIT` branch is the likely cause of the
failure itself: commit materializes pending results first, rollback does not,
which matches `test_rollback_with_open_cursor` failing with a cursor mid-fetch
and matches crusher's existing note that an open cursor invalidates a DuckDB
transaction (`src/tests/param_binding_tests.cpp:1158`).

**Suggested fix**: Materialize or close pending results in the `SQL_ROLLBACK`
branch as the `SQL_COMMIT` branch does, and map the caught exception through
the same state-selection logic used elsewhere instead of hardcoding `HY115`.

### RC7 — Core statement attributes are not readable (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLGetStmtAttr`
- **Conformance**: Core
- **Affects**: `test_statement_attributes` (1 test)
- **Evidence**: `3/5 statement attributes queryable; unreadable: Max rows
  (HY092), No scan (HY092)`
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/attribute_api.cpp#L541-L562

```cpp
case SQL_ATTR_MAX_ROWS:
case SQL_ATTR_METADATA_ID:
case SQL_ATTR_NOSCAN:
...
default:
    return duckdb::SetDiagnosticRecord(hstmt, SQL_ERROR, "SQLGetStmtAttr",
                                       "Unsupported attribute type:" + std::to_string(attribute),
                                       SQLStateType::ST_HY092, ...);
```

**Why this classification**: `SQL_ATTR_MAX_ROWS` and `SQL_ATTR_NOSCAN` are
Core-level statement attributes — not optional — and a Core-conformant driver
must at minimum return the default value. The grouping shows the omission is
deliberate rather than accidental, but "deliberately not implemented" is only a
`DRIVER_LIMITATION` when the spec makes the feature optional, and here it does
not.

**Suggested fix**: Return the stored defaults (`0` for `SQL_ATTR_MAX_ROWS`,
`SQL_NOSCAN_OFF` for `SQL_ATTR_NOSCAN`) rather than `HY092`, even if the
setters remain no-ops.

### RC8 — Invalid arguments are accepted instead of rejected (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLGetInfo`, `SQLSetConnectAttr`
- **Conformance**: Core
- **Affects**: `test_getinfo_invalid_type`, `test_setconnattr_invalid_attr` (2 tests)
- **Evidence**: `SQLGetInfo accepted invalid info type 65535`;
  `SQLSetConnectAttr accepted invalid attribute 99999`.
- **Citations**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L896-L906
  and https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/attribute_api.cpp#L391-L394

```cpp
// info_api.cpp — the comment states the intent plainly
// return SQL_SUCCESS, but with a record message
std::string msg = "Unrecognized attribute: " + std::to_string(info_type);
return duckdb::SetDiagnosticRecord(dbc, SQL_SUCCESS, "SQLGetInfo", msg, SQLStateType::ST_HY092, ...);
```

**Why this classification**: The spec requires `SQL_ERROR` with `HY096` for an
out-of-range `InfoType` and `SQL_ERROR` with `HY092` for an invalid connection
attribute. Returning `SQL_SUCCESS` (info) and `SQL_SUCCESS_WITH_INFO` with
`01S02 "Option value changed"` (attribute) means `SQL_SUCCEEDED()` is true and
the caller's output buffer was never written — a well-written application
proceeds to read uninitialised memory. `SQLSetStmtAttr` has the same
`01S02` catch-all at
[attribute_api.cpp#L714-L717](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/attribute_api.cpp#L714-L717),
which is what lets RC13's ignored attribute look like a success.

**Suggested fix**: Return `SQL_ERROR` from both default branches, with `HY096`
for `SQLGetInfo` and `HY092` for the attribute setters.

### RC9 — Correct rejection, wrong SQLSTATE (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLFetchScroll`
- **Conformance**: Level 1
- **Affects**: `test_fetchscroll_first_forward_only` (1 test)
- **Evidence**: `SQLFetchScroll(SQL_FETCH_FIRST) returned -1 (rejected, but
  with 24000 rather than HY106)`
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/common/odbc_fetch.cpp#L221-L226

```cpp
if (cursor_type == SQL_CURSOR_FORWARD_ONLY && fetch_orientation != SQL_FETCH_NEXT) {
    return SetDiagnosticRecord(hstmt, SQL_ERROR, "FetchNextChunk",
                               "Incorrect fetch orientation for cursor type: SQL_CURSOR_FORWARD_ONLY.",
                               SQLStateType::ST_24000, ...);
}
```

**Why this classification**: The behaviour is right and only the label is
wrong. `24000` is "Invalid cursor state", which tells the application the
cursor is in the wrong state; the correct state for a scroll orientation the
cursor type does not support is `HY106 "Fetch type out of range"`. Applications
that branch on SQLSTATE to decide whether to reopen the cursor or fall back to
forward-only fetching will take the wrong branch.

**Suggested fix**: Change this one `ST_24000` to `ST_HY106`.

### RC10 — An out-of-range column index throws a C++ exception across the ODBC boundary (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLGetData`
- **Conformance**: Core
- **Affects**: `test_getdata_col_out_of_range` (1 test)
- **Evidence**: `Unhandled N6duckdb17InternalExceptionE: {"exception_type":"INTERNAL","exception_message":"Attempted to access index 998 within vector of size 1", ...}` — the exception unwound out of the driver and into crusher.
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/statement/statement_functions.cpp#L394-L406

```cpp
Value val;
if (col_or_param_num > 0) {
    // Prevent underflow
    col_or_param_num--;
}
hstmt->odbc_fetcher->GetValue(col_or_param_num, val);   // no upper bound check
```

**Why this classification**: The spec requires `SQL_ERROR` with `07009
"Invalid descriptor index"` when `Col_or_Param_Num` exceeds the number of
result columns. The driver guards the lower bound ("Prevent underflow") and not
the upper one, so a bad index reaches DuckDB's internals and raises
`InternalException` — which then crosses a C ABI boundary, where behaviour is
undefined and no ODBC application can catch it. The same file already emits
`07009` correctly at
[#L1004](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/statement/statement_functions.cpp#L1004),
so the check exists elsewhere and is simply missing here. Note that crusher's
canned diagnostic on this row says *"This is a defect in odbc-crusher"* — that
text is wrong for this case and should not be read as the classification; the
non-ODBC exception originated in the driver.

**Suggested fix**: Bounds-check `col_or_param_num` against the result column
count before `GetValue` and return `07009`; separately, wrap the ODBC entry
points in a `catch (...)` so no C++ exception can ever escape the library.

### RC11 — Diagnostic header fields return the wrong value (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLGetDiagField`
- **Conformance**: Core
- **Affects**: `test_diagfield_row_count`, `test_diagfield_dynamic_function` (2 tests)
- **Evidence**: `SQL_DIAG_ROW_COUNT = 0; SQLRowCount = -1`;
  `SQL_DIAG_DYNAMIC_FUNCTION_CODE = 0 (expected 85, SQL_DIAG_SELECT_CURSOR)`
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/diagnostic_api.cpp#L115-L122

```cpp
case SQL_DIAG_ROW_COUNT: {
    // this field is available only for statement handles
    if (hdl->type != duckdb::OdbcHandleType::STMT) {
        return SQL_ERROR;
    }
    duckdb::Store<SQLLEN>(hdl->odbc_diagnostic->header.sql_diag_return_code, (duckdb::data_ptr_t)diag_info_ptr);
    return SQL_SUCCESS;                    // ^^^ returns the return code, not the row count
}
```

**Why this classification**: `SQL_DIAG_ROW_COUNT` copies
`sql_diag_return_code` — a copy-paste error one identifier wide; the header has
a `sql_diag_row_count` field that is never read here. `SQL_DIAG_DYNAMIC_FUNCTION_CODE`
is read correctly but the field is never assigned anywhere in the driver: it is
initialised to `SQL_DIAG_UNKNOWN_STATEMENT` at
[include/odbc_diagnostic.hpp#L185](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/include/odbc_diagnostic.hpp#L185)
and keeps that value for the life of the handle, so it can only ever report
`0`.

**Suggested fix**: Store `sql_diag_row_count` in the `SQL_DIAG_ROW_COUNT` case,
and set `sql_diag_dynamic_function_code` when a statement is executed (the
`MAP_DYNAMIC_FUNCTION` table in `odbc_diagnostic.cpp` already exists to
translate it).

### RC12 — `SQL_DBMS_VER` returns a zero-filled buffer (`BUG_IN_DRIVER`, 1 finding)

- **Function**: `SQLGetInfo`
- **Conformance**: Core
- **Affects**: `test_getinfo_wchar_strings` (1 test)
- **Evidence**: `3/4 string info types returned valid SQLWCHAR* [SQL_DBMS_VER: failed (ret=-2); ]`
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/info_api.cpp#L401-L433

```cpp
std::vector<SQLCHAR> buf;
buf.resize(64);
SQLLEN len_out;
ret = SQLGetData(stmt, 1, SQL_C_CHAR, info_value_ptr, buffer_length, &len_out);   // writes the caller's buffer
duckdb::FreeHandle(SQL_HANDLE_STMT, stmt);
if (!SQL_SUCCEEDED(ret)) { return ret; }
std::string version(reinterpret_cast<char *>(buf.data()), std::min(buf.size(), static_cast<size_t>(len_out)));
return WriteStringInfo(connection_handle, version, ...);                          // then overwrites it with NULs
```

**Why this classification**: The fetched version goes straight into the
caller's buffer, but the string that is finally written comes from `buf`, which
was resized to 64 zero bytes and never filled — so a successful call returns
`len_out` NUL bytes, and the real version is discarded. In the wide path the
ANSI bytes are also written into a `SQLWCHAR` buffer before being overwritten.
This case is also the only `SQLGetInfo` branch that allocates and executes a
nested statement, which makes it a candidate for RC1. It matters beyond this
probe: the driver manifest's provenance note recommends `SQL_DBMS_VER` as the
reliable runtime version source, and at this version it is not one.

**Suggested fix**: Fetch into `buf` (`SQLGetData(stmt, 1, SQL_C_CHAR, buf.data(), buf.size(), &len_out)`)
and let `WriteStringInfo` do the single write into the caller's buffer.

### RC13 — Array-parameter attributes are accepted and ignored (`BUG_IN_DRIVER`, 2 findings)

- **Function**: `SQLSetStmtAttr` / `SQLBindParameter` / `SQLExecute`
- **Conformance**: Level 1
- **Affects**: `test_row_wise_array_binding`, `test_param_operation_array` (2 tests)
- **Evidence**: row-wise binding wrote `{9991, 9991}` instead of `{9991, 9992}`
  — both rows read the same address; and `SQL_ATTR_PARAM_OPERATION_PTR` with
  rows marked `SQL_PARAM_IGNORE` gave `params_processed=4; status: [SUCCESS,
  SUCCESS, SUCCESS, SUCCESS]` — no row was skipped.
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/parameter_descriptor.cpp#L426-L431

```cpp
SQLPOINTER ParameterDescriptor::GetSQLDescDataPtr(DescRecord &apd_record) {   // note: no row index parameter
    if (cur_apd->header.sql_desc_bind_offset_ptr) {
        return (uint8_t *)apd_record.sql_desc_data_ptr + *cur_apd->header.sql_desc_bind_offset_ptr;
    }
    return apd_record.sql_desc_data_ptr;
}
```

**Why this classification**: `SQL_ATTR_PARAM_BIND_TYPE` is stored
([#L602-L607](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/attribute_api.cpp#L602-L607))
and read back
([#L459-L465](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/attribute_api.cpp#L459-L465)),
so the application is told row-wise binding is in effect, but the only
consumers of `sql_desc_bind_type` for pointer arithmetic are in the *row*
descriptor path (`odbc_fetch.cpp`); the parameter path never uses it — the
author's own TODO at
[#L260](https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_driver/parameter_descriptor.cpp#L260)
says as much: *"TODO need to check it param_value_ptr is an array of parameters
and get the right parameter using the index (now it's working for all supported
tests)"*. `SQL_ATTR_PARAM_OPERATION_PTR` has no case at all in `SQLSetStmtAttr`,
so it lands in the `01S02` catch-all from RC8 and is silently dropped. Accepting
an attribute and then ignoring it writes wrong data with no error — the same
failure mode as RC3.

**Suggested fix**: Give `GetSQLDescDataPtr` a set index and offset by
`sql_desc_bind_type` when it is not `SQL_BIND_BY_COLUMN`; reject
`SQL_ATTR_PARAM_OPERATION_PTR` with `HY092` until the ignore-mask is honoured.

### RC14 — No stored-procedure catalog (`DRIVER_LIMITATION`, 5 findings)

- **Function**: `SQLProcedures`, `SQLProcedureColumns`, `SQLBindParameter` (OUT/INOUT)
- **Conformance**: Core
- **Affects**: `test_sqlprocedurecolumns_smoke`, `test_call_escape_in_parameter`,
  `test_call_escape_out_parameter`, `test_function_call_escape_return_value`,
  `test_call_escape_inout_parameter` (5 tests)
- **Evidence**: `rows=0 IN=0 OUT=0 INOUT=0 RESULT=0 RETURN=0`, and four probes
  reporting `Test procedure MOCK_INOUT / function MOCK_FN not visible via
  SQLProcedures`.
- **Citation**: https://github.com/duckdb/duckdb-odbc/blob/v1.5.2.0/src/odbc_api/empty_api_stubs.cpp#L125-L232

```sql
-- ProceduresInternal / ProcedureColumnsInternal: correct column shape, no rows
SELECT CAST('' AS VARCHAR) AS "PROCEDURE_CAT", ... WHERE 1 < 0
```

**Why this classification**: This is the documented-gap case. The functions live
in a file called `empty_api_stubs.cpp` under a header comment that reads *"If
implemented, move to `src/odbc_api/metadata_api.cpp`"*, and they return a
well-formed empty result set with the full ODBC column list rather than an
error — which is exactly what the spec prescribes for a data source with no
procedures, and DuckDB has none. The four `{CALL ...}` probes correctly report
`SKIP_INCONCLUSIVE`: they need a registered procedure to bind against and there
is nothing to bind to. Unlike RC4, nothing here is advertised and then broken —
`SQLProcedures` does exactly what it says.

**Suggested fix**: None for the driver. Crusher could skip the `{CALL}`
direction probes when `SQLProcedures` returns zero rows, so they read as "not
applicable" rather than as five findings.

## All findings

| # | Test | Status | Severity | Root cause | Classification |
|---|---|---|---|---|---|
| — | Discovery phase (run-level) | ERROR | CRITICAL | RC1 | `BUG_IN_DRIVER` |
| 1 | `test_reconnected_handle_is_usable` | ERROR | ERROR | RC5 | `BUG_IN_CRUSHER` |
| 2 | `test_parameter_binding` | FAIL | CRITICAL | RC3 | `BUG_IN_DRIVER` |
| 3 | `test_columns_catalog` | SKIP_INCONCLUSIVE | INFO | RC5 | `BUG_IN_CRUSHER` |
| 4 | `test_count_star_result_metadata` | SKIP_INCONCLUSIVE | INFO | RC5 | `BUG_IN_CRUSHER` |
| 5 | `test_sqlprocedurecolumns_smoke` | SKIP_INCONCLUSIVE | INFO | RC14 | `DRIVER_LIMITATION` |
| 6 | `test_manual_rollback` | FAIL | INFO | RC6 | `BUG_IN_DRIVER` |
| 7 | `test_rollback_with_open_cursor` | FAIL | WARNING | RC6 | `BUG_IN_DRIVER` |
| 8 | `test_disconnect_with_open_transaction` | ERROR | ERROR | RC5 | `BUG_IN_CRUSHER` |
| 9 | `test_uncommitted_row_isolation` | ERROR | ERROR | RC5 | `BUG_IN_CRUSHER` |
| 10 | `test_statement_attributes` | FAIL | ERROR | RC7 | `BUG_IN_DRIVER` |
| 11 | `Descriptor Tests (DRIVER CRASH)` | ERROR | CRITICAL | RC2 | `BUG_IN_DRIVER` |
| 12 | `test_getdata_col_out_of_range` | ERROR | ERROR | RC10 | `BUG_IN_DRIVER` |
| 13 | `test_getinfo_invalid_type` | FAIL | WARNING | RC8 | `BUG_IN_DRIVER` |
| 14 | `test_setconnattr_invalid_attr` | FAIL | WARNING | RC8 | `BUG_IN_DRIVER` |
| 15 | `Data Type Edge Cases (DRIVER CRASH)` | ERROR | CRITICAL | RC2 | `BUG_IN_DRIVER` |
| 16 | `test_getinfo_wchar_strings` | FAIL | WARNING | RC12 | `BUG_IN_DRIVER` |
| 17 | `test_diagfield_row_count` | FAIL | INFO | RC11 | `BUG_IN_DRIVER` |
| 18 | `test_diagfield_dynamic_function` | FAIL | WARNING | RC11 | `BUG_IN_DRIVER` |
| 19 | `test_fetchscroll_first_forward_only` | FAIL | WARNING | RC9 | `BUG_IN_DRIVER` |
| 20 | `Parameter Binding Tests (DRIVER CRASH)` | ERROR | CRITICAL | RC2 | `BUG_IN_DRIVER` |
| 21 | `test_row_wise_array_binding` | FAIL | INFO | RC13 | `BUG_IN_DRIVER` |
| 22 | `test_param_operation_array` | FAIL | INFO | RC13 | `BUG_IN_DRIVER` |
| 23 | `test_native_sql_scalar_functions` | FAIL | ERROR | RC4 | `BUG_IN_DRIVER` |
| 24 | `test_native_sql_datetime_literals` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 25 | `test_native_sql_outer_join_escape` | FAIL | ERROR | RC4 | `BUG_IN_DRIVER` |
| 26 | `test_string_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 27 | `test_numeric_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 28 | `test_datetime_scalar_functions` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 29 | `test_datetime_literal_escapes` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 30 | `test_outer_join_escape` | FAIL | ERROR | RC4 | `BUG_IN_DRIVER` |
| 31 | `test_interval_literal_escape` | FAIL | ERROR | RC4 | `BUG_IN_DRIVER` |
| 32 | `test_call_escape_format_variants` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 33 | `test_call_escape_in_parameter` | SKIP_INCONCLUSIVE | INFO | RC14 | `DRIVER_LIMITATION` |
| 34 | `test_call_escape_out_parameter` | SKIP_INCONCLUSIVE | ERROR | RC14 | `DRIVER_LIMITATION` |
| 35 | `test_function_call_escape_return_value` | SKIP_INCONCLUSIVE | ERROR | RC14 | `DRIVER_LIMITATION` |
| 36 | `test_call_escape_inout_parameter` | SKIP_INCONCLUSIVE | ERROR | RC14 | `DRIVER_LIMITATION` |
| 37 | `test_scalar_function_claim_vs_execute` | FAIL | WARNING | RC4 | `BUG_IN_DRIVER` |
| 38 | `Numeric Struct Tests (DRIVER CRASH)` | ERROR | CRITICAL | RC2 | `BUG_IN_DRIVER` |

## Caveats

- Four categories and the discovery phase crashed. Everything those probes
  would have reported is missing, so the 70.1 % pass rate covers only what
  survived and the real figure is lower by an unknown margin.
- RC1 and RC2 name categories, not addresses: the crash guard that let the run
  continue also discarded the stack. Every crash needs a debugger or ASan run
  to localise.
- RC6's underlying rollback failure is inferred from the commit/rollback
  asymmetry in source; the probes recorded `rc` and SQLSTATE but not the
  driver's message text, so the exception's own words were not available.
  Capturing the diagnostic message on `SQLEndTran` failure would settle it.
- RC5's five findings say nothing about the driver. They will keep recurring
  until the DuckDB job is given a file-backed `Database=`.
