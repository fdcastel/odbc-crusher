# `DuckDB ODBC` v`1.5.2.0` — Crusher Triage Report

**Generated**: 2026-04-25 23:57
**CI run**: https://github.com/fdcastel/odbc-crusher/actions/runs/24943061694
**Driver source**: https://github.com/duckdb/duckdb-odbc @ `v1.5.2.0`
**Crusher commit**: b32b6ed

## Summary

| Metric | Value |
|---|---|
| Total tests   | 144 |
| Passed        | 109 |
| Failed        | 18 |
| Errors        | 6 |
| Skipped       | 11 |
| Pass rate     | 75.7 % |

## Version provenance

- Manifest version: `1.5.2.0`
- Runtime `actual_version.txt`: `1.5.2.0`
- JSON `driver_info.driver_version`: `03.51.0000`

NOTE: DuckDB v1.5.2.0 returns `"03.51.0000"` (its ODBC compliance level)
from `SQLGetInfo(SQL_DRIVER_VER)`, NOT the driver's own version. The
match still passes because `actual_version.txt` carries `1.5.2.0`
verbatim. No real mismatch.

## Classification breakdown

| Label | Count |
|---|---:|
| `BUG_IN_DRIVER`     | 21 |
| `BUG_IN_CRUSHER`    | 0 |
| `DRIVER_LIMITATION` | 3 |
| `INCONCLUSIVE`      | 0 |

## Findings (overview)

| # | Test | Status | Sev | Classification | Cite | Hypothesis |
|---|------|--------|-----|----------------|------|------------|
| 1 | test_manual_rollback | FAIL | INFO | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/transaction_api.cpp:41-49` | `Connection::Rollback()` throws when there's no active transaction; driver propagates as `SQL_ERROR` instead of treating no-op rollback as success. |
| 2 | test_rollback_with_open_cursor | FAIL | WARN | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/transaction_api.cpp:41-49` | ROLLBACK while cursor open returns SQL_ERROR — spec says it must succeed and may close cursor implicitly. |
| 3 | Descriptor Tests (DRIVER CRASH) | ERROR | CRIT | `BUG_IN_DRIVER` | crusher harness — SIGSEGV during category | Driver segfaults somewhere in descriptor handling. |
| 4 | test_getdata_col_out_of_range | ERROR | INFO | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/result_api.cpp` (SQLGetData) | Internal exception leaks: "Attempted to access index 998 within vector of size 1". Should return SQLSTATE 07009. |
| 5 | test_getinfo_invalid_type | FAIL | WARN | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/info_api.cpp:903-906` | Default case returns SQL_SUCCESS — must return SQL_ERROR / HY096. |
| 6 | test_setconnattr_invalid_attr | FAIL | WARN | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/attribute_api.cpp:391-395` | Default returns SQL_SUCCESS_WITH_INFO / 01S02 — must return SQL_ERROR / HY092. |
| 7 | Data Type Edge Cases (DRIVER CRASH) | ERROR | CRIT | `BUG_IN_DRIVER` | crusher harness — SIGSEGV | Driver segfaults during data-type edge probes. |
| 8 | Unicode Tests (DRIVER CRASH) | ERROR | CRIT | `BUG_IN_DRIVER` | crusher harness — SIGSEGV | Driver segfaults during Unicode tests (likely SQLWCHAR / utf16_conv path). |
| 9 | Parameter Binding Tests (DRIVER CRASH) | ERROR | CRIT | `BUG_IN_DRIVER` | crusher harness — SIGSEGV | Driver segfaults during parameter-binding tests. |
| 10 | test_row_wise_array_binding | FAIL | INFO | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_driver/parameter_descriptor.cpp` | Driver stores SQL_ATTR_PARAM_BIND_TYPE but doesn't use it for per-row pointer arithmetic — last row's value gets repeated. |
| 11 | test_param_operation_array | FAIL | INFO | `BUG_IN_DRIVER` | parameter_descriptor (param-op array) | SQL_ATTR_PARAM_OPERATION_PTR ignored; status array shows all SUCCESS where the spec says skipped rows must be UNUSED. |
| 12 | test_array_partial_error | FAIL | WARN | `BUG_IN_DRIVER` | parameter_descriptor (per-row error path) | Partial-failure contract not honored: status array stays all-SUCCESS where one row should be ERROR. |
| 13 | test_native_sql_scalar_functions | FAIL | ERR | `DRIVER_LIMITATION` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | `SQLNativeSql` is explicitly stubbed: `SetNotImplemented(hstmt, "SQLNativeSql")`. |
| 14 | test_native_sql_datetime_literals | FAIL | WARN | `DRIVER_LIMITATION` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | Same stub — no escape translation at all. |
| 15 | test_native_sql_call_escape | FAIL | WARN | `DRIVER_LIMITATION` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | Same stub. |
| 16 | test_native_sql_outer_join_escape | FAIL | ERR | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | Same root cause. Bumped to bug because DM normally pre-translates `{oj …}`; the driver's hard-error here means escape execution paths are also broken (see #17–#21). |
| 17 | test_string_scalar_functions | FAIL | WARN | `BUG_IN_DRIVER` | escape parser — none in driver | DuckDB returns NULL instead of executing `{fn UCASE/LCASE/LENGTH/LTRIM/RTRIM/CONCAT}`. Either escape-parser broken or scalar fns not exposed. |
| 18 | test_numeric_scalar_functions | FAIL | WARN | `BUG_IN_DRIVER` | escape parser — none in driver | Same: ABS/FLOOR/CEILING/SQRT/ROUND all return NULL. |
| 19 | test_datetime_scalar_functions | FAIL | WARN | `BUG_IN_DRIVER` | escape parser — none in driver | NOW/YEAR/MONTH/DAYOFWEEK return NULL. |
| 20 | test_datetime_literal_escapes | FAIL | WARN | `BUG_IN_DRIVER` | escape parser — none in driver | `{d '...'}`, `{t '...'}`, `{ts '...'}` all yield NULL. |
| 21 | test_call_escape_translation | FAIL | WARN | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | SQLNativeSql stub means CALL escapes can't be translated. |
| 22 | test_call_escape_format_variants | FAIL | WARN | `BUG_IN_DRIVER` | `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451` | Same root cause. |
| 23 | test_scalar_function_claim_vs_execute | FAIL | WARN | `BUG_IN_DRIVER` | escape parser — none in driver | Driver advertises functions in SQL_*_FUNCTIONS but execution returns NULL — bitmask lies, or escape execution is broken. |
| 24 | Numeric Struct Tests (DRIVER CRASH) | ERROR | CRIT | `BUG_IN_DRIVER` | crusher harness — SIGSEGV | Driver segfaults during SQL_NUMERIC_STRUCT probes. |

## Punch list

1. **Investigate the four SIGSEGV crashes (Descriptor / Data-Type-Edge / Unicode / Parameter-Binding / Numeric-Struct categories).** Five separate category-level crashes is the most user-visible class of bug and almost certainly the highest-impact upstream filing — every crusher run loses an entire suite. Re-run each category in isolation under a debugger and capture the offending probe so a minimal repro can be filed.
2. **Fix the SQLGetInfo / SQLSetConnectAttr error-reporting defaults** (`info_api.cpp:903-906` and `attribute_api.cpp:392-395`). Both return `SQL_SUCCESS`/`SQL_SUCCESS_WITH_INFO` for unknown identifiers. ODBC consumers (DM, frameworks) rely on the `HY096`/`HY092` SQL_ERROR contract to probe driver capabilities — silently accepting invalid IDs is a one-line fix per call-site that closes two findings.
3. **Decide whether SQLNativeSql is supported or not** — currently 7 distinct findings derive from its `SetNotImplemented` stub plus the fact that `{fn XXX}` escapes also fail at execute time, suggesting the DM is supposed to pre-translate them but the driver's escape pipeline is missing entirely. Either implement a minimal `{fn …}` / `{d …}` / `{call …}` translator, or document the gap and ensure unixODBC/Driver Manager can satisfy the contract on the driver's behalf.

## Per-finding detail

### Finding 1 — `test_manual_rollback` (FAIL)

- **Function**: `SQLEndTran(SQL_ROLLBACK)`
- **Conformance**: Core
- **Expected**: Can manually rollback a transaction
- **Actual**: SQLEndTran(ROLLBACK) failed
- **Suggestion (from probe)**: (none in JSON)

**Driver source citation**: `./tmp/triage/duckdb/src/src/odbc_api/transaction_api.cpp:41-49`
```cpp
case SQL_ROLLBACK:
    try {
        dbc->conn->Rollback();
        return SQL_SUCCESS;
    } catch (std::exception &ex) {
        duckdb::ErrorData parsed_error(ex);
        return duckdb::SetDiagnosticRecord(dbc, SQL_ERROR, "SQLEndTran", parsed_error.RawMessage(),
                                           SQLStateType::ST_HY115, dbc->GetDataSourceName());
    }
```

**Classification**: `BUG_IN_DRIVER` — DuckDB's `Connection::Rollback()` raises an exception when there is no active transaction (typical case after the probe's `INSERT … rollback path`), and the driver propagates it as `SQL_ERROR` HY115. ODBC spec (3.8) treats a redundant rollback as a no-op — drivers should return SQL_SUCCESS and clear any pending statement state. The probe's flow (turn off autocommit, INSERT, ROLLBACK) is exactly the canonical pattern.

**Suggested next step**: Wrap `dbc->conn->Rollback()` so the "no active transaction" exception class is converted to SQL_SUCCESS (or 01000 SUCCESS_WITH_INFO). Confirm with a unit test that round-trips autocommit-off → INSERT → ROLLBACK.

### Finding 2 — `test_rollback_with_open_cursor` (FAIL)

- **Function**: `SQLEndTran(SQL_ROLLBACK)`
- **Conformance**: Core
- **Expected**: ROLLBACK while a cursor is mid-fetch closes the cursor, undoes the row, and leaves the connection usable for the next statement
- **Actual**: fetch_rc=0 rollback_rc=-1 (rollback failed)
- **Suggestion (from probe)**: ROLLBACK with an open cursor must succeed; drivers may close the cursor implicitly but must not return SQL_ERROR.

**Classification**: `BUG_IN_DRIVER` — Same code path as Finding 1 (`transaction_api.cpp:41-49`). DuckDB throws when a cursor is mid-fetch; spec mandates SQL_SUCCESS with an implicit close.

**Suggested next step**: Before calling `dbc->conn->Rollback()`, walk every owned `OdbcHandleStmt` and call its CloseCursor; then attempt rollback. If still throwing, swallow the "no active txn" subclass.

### Finding 3 — `Descriptor Tests (DRIVER CRASH)` (ERROR)

- **Function**: N/A (whole category)
- **Conformance**: Core / CRITICAL severity
- **Diagnostic**: "The ODBC driver crashed during this test category. Some tests may have been lost. This is a driver bug."

**Classification**: `BUG_IN_DRIVER` — SIGSEGV is by definition a driver bug; the crusher harness re-launches into the next category.

**Suggested next step**: Run the descriptor probes (`src/tests/descriptor_tests.cpp`) one at a time against a debug build of the driver to find the offending API. `descriptor_api.cpp` is the obvious starting file.

### Finding 4 — `test_getdata_col_out_of_range` (ERROR)

- **Function**: `SQLGetData`
- **Conformance**: Core
- **Expected**: SQL_ERROR with SQLSTATE 07009 for column > num_cols
- **Actual**: Internal DuckDB exception leaked: `{"exception_type":"INTERNAL","exception_message":"Attempted to access index 998 within vector of size 1", …}`

**Classification**: `BUG_IN_DRIVER` — The driver indexes its column vector without bounds-checking and lets the underlying DuckDB internal exception leak as a JSON blob to the application.

**Suggested next step**: In the SQLGetData implementation (`./tmp/triage/duckdb/src/src/odbc_api/result_api.cpp`), validate `column_number` against the active result set's column count before vector access; return `SetDiagnosticRecord(... SQL_ERROR, "SQLGetData", "Invalid descriptor index", SQLStateType::ST_07009 ...)`.

### Finding 5 — `test_getinfo_invalid_type` (FAIL)

- **Function**: `SQLGetInfo`
- **Conformance**: Core
- **Expected**: SQL_ERROR with SQLSTATE HY096 for invalid info type
- **Actual**: SQLGetInfo accepted invalid info type 65535
- **Suggestion (from probe)**: Driver should return HY096 for unrecognized information type

**Driver source citation**: `./tmp/triage/duckdb/src/src/odbc_api/info_api.cpp:903-906`
```cpp
// return SQL_SUCCESS, but with a record message
std::string msg = "Unrecognized attribute: " + std::to_string(info_type);
return duckdb::SetDiagnosticRecord(dbc, SQL_SUCCESS, "SQLGetInfo", msg, SQLStateType::ST_HY092,
                                   dbc->GetDataSourceName());
```

**Classification**: `BUG_IN_DRIVER` — Code explicitly returns SQL_SUCCESS for unknown info types ("but with a record message" comment confirms intent). Spec requires SQL_ERROR + HY096 (HY092 in the comment is also wrong — the correct code for a bad SQLGetInfo type is HY096).

**Suggested next step**: Change the default branch to return `SQL_ERROR` with `SQLStateType::ST_HY096`. Update the comment.

### Finding 6 — `test_setconnattr_invalid_attr` (FAIL)

- **Function**: `SQLSetConnectAttr`
- **Conformance**: Core
- **Expected**: SQL_ERROR with SQLSTATE HY092 for invalid attribute
- **Actual**: SQLSetConnectAttr accepted invalid attribute 99999
- **Suggestion (from probe)**: Driver should return HY092 for unrecognized attributes

**Driver source citation**: `./tmp/triage/duckdb/src/src/odbc_api/attribute_api.cpp:391-395`
```cpp
default:
    return duckdb::SetDiagnosticRecord(dbc, SQL_SUCCESS_WITH_INFO, "SQLSetConnectAttr",
                                       "Option value changed:" + std::to_string(attribute), SQLStateType::ST_01S02,
                                       dbc->GetDataSourceName());
```

**Classification**: `BUG_IN_DRIVER` — Default branch silently returns SQL_SUCCESS_WITH_INFO + 01S02 ("option value changed") for any unknown attribute id. The spec demands SQL_ERROR + HY092 ("invalid attribute identifier") so DM/applications can probe capability.

**Suggested next step**: Replace the default branch with `SQL_ERROR` + `ST_HY092`. The 01S02 path is reserved for the case where an attribute is recognised but the requested value was substituted — not for unknown attributes.

### Finding 7 — `Data Type Edge Cases (DRIVER CRASH)` (ERROR)

- **Function**: N/A (category)
- **Conformance**: Core / CRITICAL

**Classification**: `BUG_IN_DRIVER` — SIGSEGV during the data-type edge probes.

**Suggested next step**: Bisect `src/tests/data_type_tests.cpp` (or whatever name the category maps to) one probe at a time against a debug build to identify the offending input (likely an extreme NUMERIC, BLOB, or INTERVAL value).

### Finding 8 — `Unicode Tests (DRIVER CRASH)` (ERROR)

- **Function**: N/A (category)
- **Conformance**: Core / CRITICAL

**Classification**: `BUG_IN_DRIVER` — SIGSEGV in the Unicode category. The DuckDB driver's wide-char path goes through `widechar/utf16_conv.cpp`; suspicious surface for null/short buffers.

**Suggested next step**: Run probes individually with logging on `SQLNativeSqlW`/`SQLDriverConnectW` paths. Look at `./tmp/triage/duckdb/src/src/odbc_driver/widechar/` for short-buffer / null-pointer guards.

### Finding 9 — `Parameter Binding Tests (DRIVER CRASH)` (ERROR)

- **Function**: N/A (category)
- **Conformance**: Core / CRITICAL

**Classification**: `BUG_IN_DRIVER` — SIGSEGV during parameter-binding tests, likely related to the row-wise binding logic problem documented in Finding 10.

**Suggested next step**: Coordinate with Finding 10 — both point at `parameter_descriptor.cpp`. The crash probably happens when a probe deliberately passes a string with `SQL_ATTR_PARAM_BIND_TYPE` — broken pointer arithmetic plus indirection through a string buffer dereferences garbage.

### Finding 10 — `test_row_wise_array_binding` (FAIL)

- **Function**: `SQLSetStmtAttr/SQLBindParameter/SQLExecute`
- **Conformance**: Level 1
- **Expected**: Row-wise array binding with struct layout executes successfully
- **Actual**: Row-wise binding inserts wrong data: got {9991, 9991} instead of {9991, 9992}. Driver stores SQL_ATTR_PARAM_BIND_TYPE but does not use it for pointer arithmetic.
- **Suggestion (from probe)**: The driver's parameter value reader must offset by SQL_ATTR_PARAM_BIND_TYPE bytes per row, not by column size. Executing with string parameters in this state would cause memory corruption and a process crash.

**Classification**: `BUG_IN_DRIVER` — The probe's diagnostic is precise and matches what `parameter_descriptor.cpp` would do if it indexed `param_value_array[col_size * row]` instead of `param_value_array[bind_type * row]`. The probe author flagged the memory-corruption risk, which lines up with the SIGSEGV in Finding 9.

**Suggested next step**: Inspect the parameter-value reader in `./tmp/triage/duckdb/src/src/odbc_driver/parameter_descriptor.cpp`; per-row offset must come from `SQL_ATTR_PARAM_BIND_TYPE` (the struct stride). Add a regression test for the {9991, 9992} pattern.

### Finding 11 — `test_param_operation_array` (FAIL)

- **Function**: `SQLSetStmtAttr/SQLExecute`
- **Conformance**: Level 1
- **Expected**: SQL_ATTR_PARAM_OPERATION_PTR skips rows marked SQL_PARAM_IGNORE, status=SQL_PARAM_UNUSED
- **Actual**: Execute returned 0; params_processed=4; status: [SUCCESS, SUCCESS, SUCCESS, SUCCESS]
- **Suggestion (from probe)**: Ignored rows must have status SQL_PARAM_UNUSED, executed rows must have status SQL_PARAM_SUCCESS

**Classification**: `BUG_IN_DRIVER` — Driver ignores SQL_ATTR_PARAM_OPERATION_PTR entirely (executes every row, marks every row SUCCESS). This is a Level 1 conformance gap with a clear contract from ODBC 3.x.

**Suggested next step**: In the array-execution loop, read `SQL_ATTR_PARAM_OPERATION_PTR[row]` — if it equals `SQL_PARAM_IGNORE`, skip execution and set the status array entry to `SQL_PARAM_UNUSED`.

### Finding 12 — `test_array_partial_error` (FAIL)

- **Function**: `SQLSetStmtAttr/SQLExecute`
- **Conformance**: Level 1
- **Expected**: Partial failure in array execution returns SQL_SUCCESS_WITH_INFO with mixed status
- **Actual**: Execute returned 0; processed=3; status: [SUCCESS, SUCCESS, SUCCESS]
- **Suggestion (from probe)**: Expected mix of SQL_PARAM_SUCCESS and SQL_PARAM_UNUSED in status array

**Classification**: `BUG_IN_DRIVER` — Same code path as Finding 11. The driver doesn't perform per-row error tracking during array execute.

**Suggested next step**: Implement per-row error capture: catch the row-level exception, mark its status entry SQL_PARAM_ERROR, return SQL_SUCCESS_WITH_INFO from the call.

### Finding 13 — `test_native_sql_scalar_functions` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core / ERROR severity
- **Expected**: SQLNativeSql translates {fn UCASE('hello')} to native SQL
- **Actual**: SQLNativeSql returned error

**Driver source citation**: `./tmp/triage/duckdb/src/src/odbc_api/empty_api_stubs.cpp:451`
```cpp
return SetNotImplemented(hstmt, "SQLNativeSql");
```

**Classification**: `DRIVER_LIMITATION` — `SQLNativeSql` lives in `empty_api_stubs.cpp` and is wired to `SetNotImplemented`. This is a documented "not implemented" case — spec-legal because SQLNativeSql is optional, and the DM normally pre-translates `{fn …}` escapes before reaching the driver. The probe runs SQLNativeSql directly instead of routing through the DM, so the failure is the documented-stub path.

**Suggested next step**: Either implement a minimal escape parser that copies the input to the output and unwraps simple `{fn xxx(args)}` brackets, or document this gap in the driver README so consumers route through the DM.

### Finding 14 — `test_native_sql_datetime_literals` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Expected**: SQLNativeSql translates {d '...'}, {t '...'}, {ts '...'} to native SQL
- **Actual**: 0/3 translated. date literal not translated; time literal not translated; timestamp literal not translated;

**Classification**: `DRIVER_LIMITATION` — Same root cause as Finding 13 (`empty_api_stubs.cpp:451`).

**Suggested next step**: Same as Finding 13 — minimal escape parser or documented gap.

### Finding 15 — `test_native_sql_call_escape` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Expected**: SQLNativeSql translates {CALL proc(?)} and {?=CALL func(?)} escape sequences
- **Actual**: CALL escape not translated; ?=CALL escape not translated;

**Classification**: `DRIVER_LIMITATION` — Same root cause; the SQLNativeSql stub returns NotImplemented for every escape category.

**Suggested next step**: Same as Finding 13.

### Finding 16 — `test_native_sql_outer_join_escape` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core / ERROR severity
- **Expected**: SQLNativeSql strips {oj …} wrapper from a LEFT OUTER JOIN clause
- **Actual**: SQLNativeSql returned error

**Classification**: `BUG_IN_DRIVER` — While the proximate cause is the SQLNativeSql stub (Findings 13–15 → DRIVER_LIMITATION), `{oj …}` is normally stripped by the DM. The probe found that even when the DM passes `{oj …}` through to the driver's execute path, DuckDB does not recognise it (Findings 17–20 below show every `{fn …}` escape returns NULL at execute time too). That second-order failure means the driver's SQL parser has no escape support whatsoever, which is a bug rather than a documented stub.

**Suggested next step**: Verify whether the unixODBC DM strips `{oj …}` before reaching the driver; if so, the driver only needs SQLNativeSql (Finding 13). If not, add an escape pre-processor in the prepare/execute pipeline.

### Finding 17 — `test_string_scalar_functions` (FAIL)

- **Function**: `SQLExecDirect + SQLGetData`
- **Conformance**: Core
- **Expected**: String scalar functions via {fn ...} escape produce correct results
- **Actual**: 0/6 string functions passed. Failures: UCASE='NULL' (expected 'HELLO'); LCASE='NULL' (expected 'hello'); LENGTH='NULL' (expected '4'); LTRIM='NULL' (expected 'hi'); RTRIM='NULL' (expected 'hi'); CONCAT='NULL' (expected 'ab');

**Classification**: `BUG_IN_DRIVER` — All six functions return NULL rather than executing. Either the driver isn't unwrapping `{fn …}` escapes before passing to the parser, or it forwards them to DuckDB which then evaluates them as `NULL`. Either way, the driver's escape pipeline is broken.

**Suggested next step**: Add `{fn …}` unwrapping in the prepare path before forwarding to DuckDB's parser. Validate UCASE/LCASE/LENGTH first.

### Finding 18 — `test_numeric_scalar_functions` (FAIL)

- **Function**: `SQLExecDirect + SQLGetData`
- **Conformance**: Core
- **Expected**: Numeric scalar functions via {fn ...} escape produce correct results
- **Actual**: 0/5 numeric functions passed. Failures: ABS=NULL; FLOOR=NULL; CEILING=NULL; SQRT=NULL; ROUND=NULL;

**Classification**: `BUG_IN_DRIVER` — Same root cause as Finding 17.

**Suggested next step**: Same as Finding 17.

### Finding 19 — `test_datetime_scalar_functions` (FAIL)

- **Function**: `SQLExecDirect + SQLGetData`
- **Conformance**: Core
- **Expected**: Date/time scalar functions via {fn ...} produce non-empty results
- **Actual**: 0/4 datetime functions passed. Failures: NOW=NULL; YEAR=NULL; MONTH=NULL; DAYOFWEEK=NULL;

**Classification**: `BUG_IN_DRIVER` — Same root cause as Finding 17.

**Suggested next step**: Same as Finding 17.

### Finding 20 — `test_datetime_literal_escapes` (FAIL)

- **Function**: `SQLExecDirect + SQLGetData`
- **Conformance**: Core
- **Expected**: Date/time/timestamp literal escapes return correct temporal values
- **Actual**: 0/3 datetime literal escapes passed. Failures: Date: 'NULL'; Time: 'NULL'; Timestamp: 'NULL';

**Classification**: `BUG_IN_DRIVER` — Same root cause as Finding 17 ({d '...'}, {t '...'}, {ts '...'} not unwrapped).

**Suggested next step**: Same as Finding 17.

### Finding 21 — `test_call_escape_translation` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Expected**: SQLNativeSql translates {CALL proc(?,?)} and {?=CALL func(?)} escape syntax
- **Actual**: CALL not translated; ?=CALL not translated
- **Suggestion (from probe)**: The driver's escape parser should translate CALL escape sequences to native syntax

**Classification**: `BUG_IN_DRIVER` — Already covered by Finding 13's stub. Listed separately because the report enumerates it.

**Suggested next step**: Implement minimal CALL escape translation in SQLNativeSql; covers Findings 13–16 and 21–22.

### Finding 22 — `test_call_escape_format_variants` (FAIL)

- **Function**: `SQLNativeSql`
- **Conformance**: Core
- **Expected**: All 5 CALL escape format variants from ODBC spec are translated
- **Actual**: 0/5 CALL variants translated. Failures: '{CALL proc}' not translated; '{CALL proc()}' not translated; '{CALL proc(?,?)}' not translated; '{?=CALL func(?,?)}' not translated; '{?=CALL func}' not translated;

**Classification**: `BUG_IN_DRIVER` — Same root cause as Finding 21.

**Suggested next step**: Same as Finding 21.

### Finding 23 — `test_scalar_function_claim_vs_execute` (FAIL)

- **Function**: `SQLGetInfo(SQL_*_FUNCTIONS) + SQLExecDirect`
- **Conformance**: Core
- **Expected**: Every scalar function the driver claims in SQL_*_FUNCTIONS actually runs without error
- **Actual**: STRING:0/7 NUMERIC:0/5 TIMEDATE:0/3 SYSTEM:0/0 (broken: UCASE, LCASE, LENGTH, LTRIM, RTRIM, CONCAT, SUBSTRING, ABS, FLOOR, CEILING, SQRT, ROUND, NOW, YEAR, MONTH)
- **Suggestion (from probe)**: Each function listed in SQL_*_FUNCTIONS must execute without error. The 'broken' list shows functions claimed but rejected at execute time — fix the bitmask, or fix the function support.

**Classification**: `BUG_IN_DRIVER` — DuckDB advertises scalar function bits in SQL_STRING/NUMERIC/TIMEDATE_FUNCTIONS but every function in those bitmasks fails at execute time (NULL result). Two valid fixes: clear the bitmask, or repair the escape pipeline (Findings 17–20). Either way, the driver currently lies about its capabilities, which is a contract violation.

**Suggested next step**: Either gate the SQL_*_FUNCTIONS bitmask to only the functions that survive `{fn X(args)}` execution, or fix the escape unwrapping so the advertised set works.

### Finding 24 — `Numeric Struct Tests (DRIVER CRASH)` (ERROR)

- **Function**: N/A (category)
- **Conformance**: Core / CRITICAL

**Classification**: `BUG_IN_DRIVER` — SIGSEGV during SQL_NUMERIC_STRUCT probes. DuckDB's NUMERIC handling is a perennial driver pain point (precision/scale plus 16-byte mantissa).

**Suggested next step**: Bisect `src/tests/numeric_struct_tests.cpp` against the driver. Probable culprit: SQL_C_NUMERIC binding paths in `parameter_descriptor.cpp`/`row_descriptor.cpp` mishandle the 16-byte sign-magnitude buffer.
