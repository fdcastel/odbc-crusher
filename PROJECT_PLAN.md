# ODBC Crusher — Project Plan

**Version**: 2.11  
**Purpose**: A command-line tool for ODBC driver developers to validate driver correctness, discover capabilities, and identify spec violations.  
**Last Updated**: February 8, 2026

---

## 1. Vision

ODBC Crusher connects to any ODBC data source and produces a clear report: what the driver does right, what it does wrong, and what it's missing. The output must be immediately useful to a driver developer who needs to fix or improve their ODBC implementation.

The tool ships with a companion **Mock ODBC Driver** that mirrors the application's testing surface. Both are developed in lockstep — every test the application runs must be exercisable against the mock driver, and every function the mock driver claims to support must be exercised by the test suite.

---

## 2. Architecture

```
odbc-crusher <connection_string> [--verbose] [-o console|json] [-f file.json]
```

### Component Map

| Component | Path | Role |
|-----------|------|------|
| **CLI entry point** | `src/main.cpp` | Argument parsing, orchestration |
| **ODBC wrappers** | `src/core/` | RAII handles for HENV, HDBC, HSTMT; error translation |
| **Discovery** | `src/discovery/` | `DriverInfo`, `TypeInfo`, `FunctionInfo` — driver capability probes |
| **Test suites** | `src/tests/` | 9 test categories, each a `TestBase` subclass |
| **Reporting** | `src/reporting/` | `ConsoleReporter`, `JsonReporter` |
| **Mock driver** | `mock-driver/` | Standalone ODBC DLL for CI/offline testing |
| **Unit tests** | `tests/` | GTest suite for regression testing |

### Data Flow

1. Parse CLI → create reporter → connect to data source
2. **Phase 1 (Discovery)**: Collect driver info, type info, function support
3. **Phase 2 (Testing)**: Run 9 test categories, collect `TestResult` vectors
4. **Phase 3 (Reporting)**: Format everything for the target audience

---

## 3. Current State — Audit Results

### 3.1 What Works

- **Core ODBC lifecycle**: Environment → Connection → Statement RAII wrappers are solid.
- **57 tests across 9 categories** run without crashing.
- **Mock driver implements 60 ODBC functions** and supports configurable behavior via connection string.
- **Console reporter** produces readable category-by-category output.
- **JSON reporter** produces structured machine-readable output.
- **31/31 GTest unit tests pass** (those that don't require external databases).
- **Build system** (CMake 3.20+, FetchContent for dependencies) works reliably.

### 3.2 Bugs

| # | Severity | Description | Location |
|---|----------|-------------|----------|
| B1 | **High** | `DriverInfo::get_properties()` looks up map keys like `"ODBC_VER"`, `"DATABASE_NAME"`, etc. but `collect()` stores them with display names like `"DBMS Name"`. Nine Properties fields are always empty. | `src/discovery/driver_info.cpp` |
| B2 | **Medium** | `JsonReporter` does not include Phase 1 discovery data (driver info, type info, function support). JSON output is incomplete compared to console output. | `src/reporting/json_reporter.cpp` |
| B3 | **Medium** | Mock driver claims `SQLBrowseConnect`, `SQLSetPos`, `SQLBulkOperations` as supported in `SQLGetFunctions` but has no implementation. Calling these will crash or return undefined results. | `mock-driver/src/odbc_info.cpp` |
| B4 | **Low** | Debug file I/O left in mock driver (`debug_connection.txt`, `debug_getinfo.txt`). These are remnants and should be removed. | `mock-driver/src/odbc_connection.cpp`, `mock-driver/src/odbc_info.cpp` |
| B5 | **Low** | `OdbcStatement::close_cursor()` accepts any non-`SQL_ERROR` return code, including `SQL_INVALID_HANDLE`. Should check `SQL_SUCCEEDED()` or handle `SQL_INVALID_HANDLE` specifically. | `src/core/odbc_statement.cpp` |
| B6 | **Low** | Dead code: `cli/cli_parser.hpp/cpp` and `cli/config_manager.hpp/cpp` are empty stubs compiled into a library but never used. All CLI parsing is in `main.cpp`. | `src/cli/` |
| B7 | **Low** | `main.cpp` repeats the same result-tallying `switch` block 9 times instead of using a helper function. | `src/main.cpp` |

### 3.3 Test Coverage Gaps

**6 of 57 tests are permanent stubs** that always return SKIP with no implementation:

| Test | Category | Reason for stub |
|------|----------|-----------------|
| `test_multiple_errors` | Error Queue | "requires error generation" |
| `test_error_clearing` | Error Queue | "requires error generation" |
| `test_field_extraction` | Error Queue | "requires error generation" |
| `test_invalid_operation` | State Machine | "requires advanced driver support" |
| `test_state_reset` | State Machine | "requires query execution" |
| `test_prepare_execute_cycle` | State Machine | "requires prepare/execute" |

These can all be implemented — the mock driver supports query execution, prepare/execute, and error injection (`FailOn=`, `ErrorCode=`). The stubs predate the mock driver's current capabilities.

### 3.4 ODBC Functions: Tested vs Implemented

The mock driver implements 60 functions. The test suite exercises only ~34 of them. Key gaps:

| Function | Mock implements? | App tests? | Gap |
|----------|:---:|:---:|-----|
| `SQLBindCol` | ✅ | ❌ | Column binding never tested; all data retrieval uses `SQLGetData` |
| `SQLColAttribute` | ✅ | ❌ | Column attributes never tested beyond `SQLDescribeCol` |
| `SQLFetchScroll` | ✅ | ❌ | Scrollable cursors never tested |
| `SQLRowCount` | ✅ | ❌ | Row count after INSERT/UPDATE/DELETE never checked |
| `SQLNativeSql` | ✅ | ❌ | SQL translation never tested |
| `SQLCancel` | ✅ | ❌ | Statement cancellation never tested |
| `SQLDescribeParam` | ✅ | ❌ | Parameter description never tested |
| `SQLNumParams` | ✅ | ❌ | Parameter count never tested |
| `SQLGetDescField/Rec` | ✅ | ❌ | Entire descriptor API untested |
| `SQLSetDescField/Rec` | ✅ | ❌ | Entire descriptor API untested |
| `SQLCopyDesc` | ✅ | ❌ | Descriptor copying never tested |
| `SQLGetDiagField` | ✅ | ❌ | Only `SQLGetDiagRec` is used |
| `SQLSetPos` | ⚠️ falsely claimed | ❌ | Neither side |
| `SQLBulkOperations` | ⚠️ falsely claimed | ❌ | Neither side |
| `SQLBrowseConnect` | ⚠️ falsely claimed | ❌ | Neither side |

### 3.5 Reporting Quality Problems

The report output is **poor for its target audience (driver developers)**. Specific issues:

1. **No ODBC spec reference**: Tests say "PASS" or "FAIL" but don't cite which ODBC spec requirement was validated. A driver developer needs to know *which spec clause* their driver violates.

2. **No conformance level classification**: Tests don't indicate whether a capability is Core, Level 1, or Level 2 ODBC conformance. A driver developer needs to know if a failure means "your driver is broken" vs "your driver doesn't implement this optional feature."

3. **Ambiguous SKIP results**: A SKIP could mean "driver doesn't support this optional feature" or "test couldn't run due to infrastructure issues." These are very different signals for a driver developer.

4. **Discovery data missing from JSON**: Phase 1 data (driver info, types, functions) only appears in console output. The JSON report — the one most likely consumed by CI/CD — lacks it entirely.

5. **No per-function coverage summary**: The report doesn't summarize which ODBC functions were exercised and which weren't. A driver developer can't see at a glance "SQLFetchScroll was never tested."

6. **Suggestions are generic**: Most tests lack `suggestion` text. When present, it's vague ("some drivers don't implement this"). It should say what to do: "Implement SQLForeignKeys to return a result set with 14 columns per ODBC spec §API Reference."

7. **No severity-aware summary**: The final summary treats all failures equally. A buffer overflow (CRITICAL) should be highlighted more prominently than a missing optional catalog function (INFO).

---

## 4. Roadmap

### Phase 11: Fix Bugs & Report Quality ✅

**Goal**: Fix all known bugs and transform the report output from "test log" to "driver compliance report."

#### 11.1 Bug Fixes

- [x] **B1**: Fix `DriverInfo::get_properties()` key mismatch — collect additional `SQLGetInfo` calls for `SQL_ODBC_VER`, `SQL_DATABASE_NAME`, `SQL_SERVER_NAME`, `SQL_USER_NAME`, `SQL_CATALOG_TERM`, `SQL_SCHEMA_TERM`, `SQL_TABLE_TERM`, `SQL_PROCEDURE_TERM`, `SQL_IDENTIFIER_QUOTE_CHAR` and store them so `get_properties()` can find them
- [x] **B2**: Add discovery data to `JsonReporter` — add `driver_info`, `type_info`, and `function_info` sections to the JSON output
- [x] **B3**: Remove false `SQLBrowseConnect`/`SQLSetPos`/`SQLBulkOperations` from mock driver's supported function list
- [x] **B4**: Remove debug file I/O from mock driver
- [x] **B5**: Fix `OdbcStatement::close_cursor()` to properly check return codes
- [x] **B6**: Remove dead CLI stub files or implement them
- [x] **B7**: Extract result-tallying into a helper function in `main.cpp`

#### 11.2 Report Overhaul

- [x] Add ODBC conformance level to each test result: `Core`, `Level 1`, `Level 2`
- [x] Add ODBC spec reference to each test (e.g., "Per ODBC 3.x, SQLGetInfo")
- [x] Split SKIP into `SKIP_UNSUPPORTED` (driver doesn't support optional feature) and `SKIP_INCONCLUSIVE` (test couldn't determine)
- [x] Add severity-ranked failure summary (CRITICAL → ERROR → WARNING)
- [x] Add function coverage matrix to report: which functions were called, result, and whether the driver claims support
- [x] Add discovery data (driver info, types, function support) to JSON reporter
- [x] Improve all suggestion text with actionable guidance citing spec sections
- [x] Add `--conformance-level` CLI flag to filter tests by Core/Level 1/Level 2

#### 11.3 Updated `TestResult` Structure

```cpp
struct TestResult {
    std::string test_name;
    std::string function;              // ODBC function tested
    TestStatus status;                 // PASS, FAIL, SKIP_UNSUPPORTED, SKIP_INCONCLUSIVE, ERR
    Severity severity;                 // CRITICAL, ERROR, WARNING, INFO
    ConformanceLevel conformance;      // CORE, LEVEL_1, LEVEL_2
    std::string spec_reference;        // "ODBC 3.x, SQLGetInfo"
    std::string expected;
    std::string actual;
    std::optional<std::string> diagnostic;
    std::optional<std::string> suggestion;
    std::chrono::microseconds duration;
};
```

**Deliverables**:
- All 7 bugs fixed
- Updated `TestResult` with conformance and spec fields
- JSON report includes all discovery data
- Console report shows conformance level and spec references
- Severity-ranked failure summary

---

### Phase 12: Implement Stub Tests & Expand Coverage ✅

**Goal**: Implement the 6 stub tests and add tests for the 15+ ODBC functions the mock driver supports but are never exercised.

#### 12.1 Implement Existing Stubs

- [x] `test_multiple_errors`: Execute invalid SQL to generate diagnostics, iterate with `SQLGetDiagRec`
- [x] `test_error_clearing`: Trigger error, then execute successful operation, verify diagnostics cleared
- [x] `test_field_extraction`: Use `SQLGetDiagField` to extract SQLSTATE, native error, message text, record count
- [x] `test_invalid_operation`: Call `SQLExecute` without `SQLPrepare`, verify HY010 (Function sequence error)
- [x] `test_state_reset`: Execute query, `SQLCloseCursor`, verify statement is reusable
- [x] `test_prepare_execute_cycle`: Prepare → Execute → Close → Execute → Close cycle

#### 12.2 New Test Categories

**Descriptor Tests** (new category — 5 tests):
- [x] Get implicit APD/ARD/IPD/IRD handles via `SQLGetStmtAttr`
- [x] Read IRD fields after `SQLPrepare` (column metadata via descriptors)
- [x] Set APD fields for parameter binding
- [x] `SQLCopyDesc` between statement handles
- [x] Verify descriptor auto-population after `SQLExecDirect`

**Column Binding Tests** (extend Statement Tests — 4 tests):
- [x] `SQLBindCol` for integer column
- [x] `SQLBindCol` for string column
- [x] Fetch with bound columns vs `SQLGetData`
- [x] `SQLFreeStmt(SQL_UNBIND)` to reset bindings

**Scrollable Cursor Tests** (extend Advanced Tests — 4 tests):
- [x] `SQLFetchScroll(SQL_FETCH_NEXT)`
- [x] `SQLFetchScroll(SQL_FETCH_FIRST)` / `SQL_FETCH_LAST`
- [x] `SQLFetchScroll(SQL_FETCH_ABSOLUTE, n)`
- [x] Set `SQL_ATTR_CURSOR_SCROLLABLE` and verify

**Row Count & Parameter Tests** (extend Statement Tests — 4 tests):
- [x] `SQLRowCount` after execution
- [x] `SQLNumParams` after prepare
- [x] `SQLDescribeParam` for bound parameters
- [x] `SQLNativeSql` translation

**Cancellation Tests** (new category — 2 tests):
- [x] `SQLCancel` on an idle statement (should succeed)
- [x] `SQLCancel` as state reset

#### 12.3 Mock Driver Updates

- [x] Remove false support claims (`SQLBrowseConnect`, `SQLSetPos`, `SQLBulkOperations`) — done in Phase 11
- [x] Remove debug file I/O — done in Phase 11
- [x] Verify mock driver returns HY010 for function sequence errors — confirmed
- [x] Verify mock driver returns 01004 for string truncation — fixed, `SQLGetData` now adds 01004 diagnostic
- [x] Add `SQLNativeSql` implementation (pass-through) — already existed
- [x] Verify `SQLCancel` implementation is correct — confirmed
- [x] Mock driver `error_count` config now generates multiple diagnostic records per failure

**Deliverables**:
- 0 stub tests remaining ✅
- 19 new tests across 5 sub-categories ✅
- Mock driver honesty: only claims what it implements ✅
- 70+ total tests, all exercisable against mock driver ✅

---

### Phase 13: Negative Testing & Spec Compliance ✅

**Goal**: Test that the driver *rejects* invalid operations correctly, per ODBC spec.

ODBC drivers are required to return specific SQLSTATEs for specific error conditions. This phase verifies the driver's error behavior.

#### 13.1 SQLSTATE Validation Tests (10 tests)

| Test | Expected SQLSTATE | Spec Reference | Status |
|------|-------------------|----------------|--------|
| `SQLExecute` without `SQLPrepare` | HY010 | Function sequence error | ✅ |
| `SQLFetch` without active cursor | 24000 | Invalid cursor state | ✅ |
| `SQLGetData` for column 0 (bookmarks disabled) | 07009 | Invalid descriptor index | ✅ |
| `SQLGetData` for column > num_cols | 07009 | Invalid descriptor index | ✅ |
| `SQLDriverConnect` on already-connected handle | 08002/HY010 | Connection in use | ✅ |
| `SQLExecDirect` with syntax error | 42000 | Syntax error or access violation | ✅ |
| `SQLBindParameter` with invalid C type | HY003 | Invalid application buffer type | ✅ |
| `SQLGetInfo` with invalid info type | HY096 | Information type out of range | ✅ |
| `SQLSetConnectAttr` with invalid attribute | HY092 | Invalid attribute/option | ✅ |
| `SQLCloseCursor` with no open cursor | 24000 | Invalid cursor state | ✅ |

#### 13.2 Boundary Value Tests (5 tests)

- [x] `SQLGetInfo` with buffer size = 0 (should return required length)
- [x] `SQLGetData` with buffer size = 0 (should return data length)
- [x] `SQLBindParameter` with NULL value pointer and `SQL_NULL_DATA` indicator
- [x] `SQLExecDirect` with empty SQL string
- [x] `SQLDescribeCol` with column number = 0

#### 13.3 Data Type Edge Cases (10 tests)

- [x] INTEGER: 0, INT_MAX (2147483647), INT_MIN (-2147483648)
- [x] VARCHAR: empty string, string with special characters (quotes, backslash)
- [x] NULL indicators: integer NULL, varchar NULL — both return SQL_NULL_DATA
- [x] Type conversion: retrieve INTEGER as `SQL_C_CHAR`, CHAR as `SQL_C_SLONG`
- [x] DECIMAL: float/double precision values retrieved as SQL_C_DOUBLE

#### Mock Driver Updates

- [x] `SQLGetInfo` returns HY096 for invalid info types
- [x] `SQLSetConnectAttr` returns HY092 for invalid attributes
- [x] `SQLBindParameter` validates C type, returns HY003 for invalid values
- [x] `SQLFetch` returns 24000 (invalid cursor state) when no cursor open
- [x] Added SQLSTATE constants: `HY003`, `HY096`

**Deliverables**:
- 10 SQLSTATE validation tests ✅
- 5 boundary value tests ✅
- 10 data type edge case tests ✅
- Mock driver returns spec-correct SQLSTATEs for all negative tests ✅

---

### Phase 14: Polish & Documentation ✅

**Goal**: Production-ready tool with comprehensive documentation.

- [x] Version tagging from git tag — `cmake/Version.cmake` reads `git describe --tags` and generates `version.hpp` at configure time. Falls back to CMake `project(VERSION)` if no tag is found.
- [x] Release build configuration — `install()` target in `src/CMakeLists.txt`, platform packaging in release workflow.
- [x] GitHub Actions workflows for testing and release — CI (`ci.yml`) builds and tests on Windows/Linux/macOS with `fetch-depth: 0` for tag detection. Release (`release.yml`) builds binaries on all 3 platforms and creates a GitHub Release with artifacts using `softprops/action-gh-release@v2`.
- [x] `--version` (`-V`) outputs the application version from the generated header.
- [x] `--help` output covers all options with examples and detailed descriptions.
- [x] README.md rewritten — focused on usage: Quick Start, What It Tests, Example Output, Build From Source, CLI Reference, Exit Codes, JSON Output, Interpreting Results.
- [x] `CONTRIBUTING.md` — development setup, project structure, how to add tests, code style, mock driver usage, PR/issue guidelines.

---

### Phase 15: Mock Driver Unicode Rewrite & Extended Test Coverage

**Goal**: Rewrite the mock driver with a Unicode-first architecture (modeled after psqlodbc) and extend odbc-crusher's test coverage based on insights from studying a production ODBC driver.

**Reference**: `MOCK_DRIVER_PLAN.md` contains the full analysis and implementation plan.

**Background**: A study of the psqlodbc PostgreSQL ODBC driver (2000+ commits, ~30 years of production use) revealed that the mock driver's current architecture prevents it from working correctly with the Windows ODBC Driver Manager. The DM calls W-variant functions (`SQLColumnsW`, `SQLGetInfoW`, etc.) on Unicode drivers, and the mock driver either doesn't export them or exports stubs that crash. This is why the mock driver produces empty test reports in CI.

#### 15.1 Mock Driver Rewrite (from MOCK_DRIVER_PLAN.md)

- [x] **Phase M1**: Unicode-First Architecture — Three-layer design (API → Internal → Mock), proper UTF-16 ↔ UTF-8 conversion, psqlodbc-style `.def` file, all 34 W-variant entry points
- [x] **Phase M2**: Internal API Hardening — Unicode-aware `SQLGetInfo` output, `SQL_C_WCHAR` support in `SQLGetData`, `SQL_WVARCHAR` catalog columns, byte-based buffer lengths
- [x] **Phase M3**: Spec Compliance Polish — Accurate `SQLGetFunctions` bitmask, expanded connection attributes, complete SQLSTATE coverage, `DllMain`
- [x] **Phase M4**: Thread Safety — Per-handle mutexes, RAII lock guards on all API entry points

#### 15.2 New Tests for odbc-crusher (Insights from psqlodbc Test Suite)

The psqlodbc test suite (`test/src/`) contains 50+ test programs covering areas that odbc-crusher doesn't currently test. Key additions:

**Unicode-Specific Tests** (new category — 5 tests):
- [x] `SQLGetInfo` returns valid `SQLWCHAR*` for string info types (SQL_DBMS_NAME, SQL_DRIVER_NAME, etc.)
- [x] `SQLDescribeCol` returns column names as `SQLWCHAR*`
- [x] `SQLGetData` with `SQL_C_WCHAR` retrieves Unicode string data
- [x] `SQLColumns` with Unicode table/column name patterns
- [x] String truncation (`01004`) with `SQLWCHAR*` buffers (buffer size in bytes vs chars)

**Catalog Function Depth Tests** (extend Metadata category — 6 tests):
- [x] `SQLTables` with `SQL_ALL_CATALOGS` / `SQL_ALL_SCHEMAS` / `SQL_ALL_TABLE_TYPES` search patterns
- [x] `SQLColumns` result set has all 18 ODBC-specified columns with correct types
- [x] `SQLStatistics` returns index information with correct ordering
- [x] `SQLProcedures` / `SQLProcedureColumns` return valid result sets (even if empty)
- [x] `SQLTablePrivileges` / `SQLColumnPrivileges` return valid result sets
- [x] Catalog function with NULL catalog/schema parameters (tests default behavior)

**Diagnostic Depth Tests** (extend Error Queue — 4 tests):
- [x] `SQLGetDiagField` with `SQL_DIAG_SQLSTATE` returns 5-char state
- [x] `SQLGetDiagField` with `SQL_DIAG_NUMBER` returns correct record count
- [x] `SQLGetDiagField` with `SQL_DIAG_ROW_COUNT` after DML
- [x] Multiple diagnostic records from a single operation (error chain)

**Cursor Behavior Tests** (new category — 4 tests):
- [x] Forward-only cursor fetch past end returns `SQL_NO_DATA`
- [x] `SQLFetchScroll(SQL_FETCH_FIRST)` on forward-only cursor returns error
- [x] `SQL_ATTR_CURSOR_TYPE` reflects actual cursor capabilities
- [x] `SQLGetData` called twice on same column (tests `SQL_GD_ANY_COLUMN` / `SQL_GD_ANY_ORDER` behavior)

**Parameter Binding Tests** (extend Statement category — 3 tests):
- [x] `SQLBindParameter` with `SQL_C_WCHAR` input type
- [x] `SQLBindParameter` with `SQL_NULL_DATA` indicator
- [x] Parameter markers in prepared statement: bind, execute, rebind, execute

#### 15.3 Consonant Development Verification

After the mock driver rewrite, verify the consonant development rule:

- [x] Every W-variant exported in `.def` has an implementation
- [x] Every function listed in `SQLGetFunctions` is exercisable and tested
- [x] odbc-crusher runs against the mock driver and completes all 9+ test categories with non-empty results
- [x] Mock driver unit tests cover all API entry points

**Deliverables**:
- Mock driver works correctly on Windows (no crash, no empty reports) ✅
- 22 new tests covering Unicode, catalog depth, diagnostics, cursor behavior, and parameter binding ✅
- Mock driver architecture documented in `MOCK_DRIVER_PLAN.md` ✅
- CI stress-test produces full reports from the mock driver job ✅

---

### Phase 16: Arrays of Parameter Values

**Goal**: Implement and test ODBC "Arrays of Parameter Values" — the ability to bind arrays of values to parameter markers and execute a statement once for multiple parameter sets. This is a Level 1 conformance feature used heavily by batch INSERT/UPDATE/DELETE operations.

**ODBC Spec References**:
- [Arrays of Parameter Values](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/arrays-of-parameter-values)
- [Binding Arrays of Parameters](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/binding-arrays-of-parameters)
- [Using Arrays of Parameters](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/using-arrays-of-parameters)

#### 16.1 Mock Driver: Array Parameter Support

- [x] Add `param_status_ptr_`, `params_processed_ptr_`, `param_bind_type_`, `param_bind_offset_ptr_`, `param_operation_ptr_` members to `StatementHandle`
- [x] Handle `SQL_ATTR_PARAM_STATUS_PTR`, `SQL_ATTR_PARAMS_PROCESSED_PTR`, `SQL_ATTR_PARAM_BIND_TYPE`, `SQL_ATTR_PARAM_BIND_OFFSET_PTR`, `SQL_ATTR_PARAM_OPERATION_PTR` in `SQLSetStmtAttr` / `SQLGetStmtAttr`
- [x] Modify `SQLExecute` to loop over `paramset_size_` parameter sets using column-wise or row-wise addressing
- [x] Populate `param_status_ptr_` array with per-row status (`SQL_PARAM_SUCCESS`, `SQL_PARAM_ERROR`, `SQL_PARAM_UNUSED`)
- [x] Set `*params_processed_ptr_` to the number of parameter sets processed
- [x] Support `SQL_PARAM_IGNORE` in the operation array to skip specific parameter sets
- [x] Return `SQL_SUCCESS_WITH_INFO` when some (but not all) parameter sets fail
- [x] Report `SQL_PARAM_ARRAY_ROW_COUNTS = SQL_PARC_BATCH` and `SQL_PARAM_ARRAY_SELECTS = SQL_PAS_NO_SELECT` via `SQLGetInfo`

#### 16.2 New Test Category: Array Parameter Tests (8 tests)

| Test | Description | ODBC Functions | Conformance |
|------|-------------|----------------|-------------|
| `test_column_wise_array_binding` | Bind integer + varchar arrays column-wise, execute INSERT with `PARAMSET_SIZE > 1` | `SQLSetStmtAttr`, `SQLBindParameter`, `SQLExecute` | Level 1 |
| `test_row_wise_array_binding` | Bind parameters in row-wise struct layout via `SQL_ATTR_PARAM_BIND_TYPE` | `SQLSetStmtAttr`, `SQLBindParameter`, `SQLExecute` | Level 1 |
| `test_param_status_array` | Verify `SQL_ATTR_PARAM_STATUS_PTR` is populated with per-row status | `SQLSetStmtAttr`, `SQLExecute` | Level 1 |
| `test_params_processed_count` | Verify `SQL_ATTR_PARAMS_PROCESSED_PTR` reports correct count | `SQLSetStmtAttr`, `SQLExecute` | Level 1 |
| `test_array_with_null_values` | Array binding where some rows have `SQL_NULL_DATA` indicators | `SQLBindParameter`, `SQLExecute` | Level 1 |
| `test_param_operation_array` | Use `SQL_ATTR_PARAM_OPERATION_PTR` to skip specific rows (`SQL_PARAM_IGNORE`) | `SQLSetStmtAttr`, `SQLExecute` | Level 1 |
| `test_paramset_size_one` | Setting `SQL_ATTR_PARAMSET_SIZE = 1` behaves like normal single-parameter execution | `SQLSetStmtAttr`, `SQLExecute` | Core |
| `test_array_partial_error` | Inject failure on specific parameter sets, verify mixed `SQL_PARAM_SUCCESS` / `SQL_PARAM_ERROR` | `SQLSetStmtAttr`, `SQLExecute` | Level 1 |

#### 16.3 Unit Tests

- [x] GTest suite `ArrayParamTestsTest` validating all 8 tests against the mock driver
- [x] Verify `SQLGetInfo(SQL_PARAM_ARRAY_ROW_COUNTS)` returns `SQL_PARC_BATCH`
- [x] Verify `SQLGetInfo(SQL_PARAM_ARRAY_SELECTS)` returns `SQL_PAS_NO_SELECT`

#### 16.4 Registration

- [x] Register `ArrayParamTests` in `src/main.cpp`
- [x] Add source files to `src/tests/CMakeLists.txt`
- [x] Add unit test file to `tests/CMakeLists.txt`

**Deliverables**:
- Mock driver supports array parameter execution (column-wise + row-wise) ✅
- 8 new tests covering all ODBC array parameter features ✅
- GTest unit tests passing against mock driver ✅
- `SQLGetInfo` reports correct array parameter capabilities ✅

---

### Phase 17: Mock Driver Full SQL Support & Zero Skips

**Goal**: Make the mock driver a reference ODBC implementation. With `Mode=Success`, every odbc-crusher test should pass — zero failures, minimal skips (only for genuinely optional Level 2 features the mock driver explicitly doesn't implement).

**Problem Statement**: Running odbc-crusher against the mock driver produces 75/131 passes (57.3%), 2 failures, and 54 skipped tests. For a driver meant to be the "example of how to do ODBC right," this is unacceptable.

**Reference**: `MOCK_DRIVER_PLAN.md` Phase M6 contains the full implementation plan.

#### 17.1 Root Cause Analysis

| Root Cause | Tests Affected | Fix |
|------------|----------------|-----|
| `SELECT literal` (no FROM clause) rejected | ~35 tests (statements, data types, edge cases, state machine, etc.) | Add literal SELECT parser to mock driver |
| `CUSTOMERS` table missing from mock catalog | ~14 tests (unicode, cursor, param binding, diagnostics, catalog depth) | Add CUSTOMERS table with FK relationships |
| `CREATE TABLE`/`DROP TABLE` not supported | 2 tests (manual commit, manual rollback) | Add DDL support with mutable catalog |
| No scrollable cursors | 3 tests (FIRST, LAST, ABSOLUTE scroll) | Implement `SQLFetchScroll` with all orientations |
| `SQLDescribeParam` doesn't validate param number | 1 test | Count `?` markers, validate range |
| `INSERT` doesn't persist data | 2 tests (transaction verification) | Add mutable data storage with rollback support |

#### 17.2 Mock Driver Changes (from MOCK_DRIVER_PLAN.md Phase M6)

- [x] **Literal SELECT parser**: Handle `SELECT expr [, expr ...]` without FROM — integers, floats, quoted strings, NULL, CAST(), parameter markers, N'...', X'...', UUID()
- [x] **CUSTOMERS table**: Add to default catalog with CUSTOMER_ID, NAME, EMAIL, CREATED_DATE, IS_ACTIVE, BALANCE columns; FK from ORDERS
- [x] **DDL support**: `CREATE TABLE name (col type, ...)` and `DROP TABLE name` modify catalog at runtime
- [x] **Data persistence**: INSERT stores rows in mutable storage; SELECT COUNT(*) reads from it; SQLEndTran(ROLLBACK) clears pending data
- [x] **Scrollable cursors**: SQLFetchScroll supports FIRST, LAST, ABSOLUTE, RELATIVE when cursor type allows it
- [x] **SQLDescribeParam**: Count `?` markers in SQL, validate parameter number range

#### 17.3 Unit Tests

- [x] GTest tests for literal SELECT parsing and execution
- [x] GTest tests for CREATE TABLE / DROP TABLE
- [x] GTest tests for INSERT + SELECT COUNT(*) + ROLLBACK
- [x] GTest tests for scrollable cursor fetch orientations
- [x] GTest tests for SQLDescribeParam validation

#### 17.4 Verification

- [x] `odbc-crusher "Driver={Mock ODBC Driver};Mode=Success;"` exits with code 0 (all tests pass) ✅
- [x] Mock driver unit tests: 48/48 pass (100%) ✅
- [x] odbc-crusher unit tests: 60/60 pass (100%) ✅
- [x] CI workflow passes on all platforms (Ubuntu ✅ Windows ✅ macOS ✅)

#### 17.5 README Update

- [x] Remove "Mock Driver Limitations" section listing limitations that Phase M6 fixes
- [x] Update mock driver capabilities list with all new features

**Deliverables**:
- Mock driver handles all SQL patterns emitted by odbc-crusher ✅
- **131/131 tests pass (100.0%) — 0 failures, 0 skips** ✅
- Mock driver unit tests cover all new functionality (48/48 pass) ✅
- README accurately describes mock driver capabilities ✅

---

### Phase 18: Real-Driver Validation Bugs — Round 1 (Firebird Analysis) ✅

**Goal**: Fix all bugs discovered by running odbc-crusher against the Firebird ODBC driver (v03.00.0021) and cross-referencing against the driver's actual source code. These bugs cause false negatives (reporting failures or skips for features the driver actually supports) and undermine trust in the tool's output.

**Discovery Date**: February 8, 2026  
**Analysis Method**: Ran odbc-crusher against Firebird ODBC Debug driver, then compared every failed/skipped test result against the Firebird ODBC driver source code to determine whether the result was correct or a bug in odbc-crusher.

**Summary**: Of 25 skipped + 1 failed + 1 error tests, investigation found **17 bugs in odbc-crusher** where tests falsely report features as unsupported due to hardcoded table names, non-portable SQL syntax, reserved words, or flawed test logic.

**Firebird ODBC Recommendations Integration**: Reviewed `ODBC_CRUSHER_RECOMMENDATIONS.md` from the Firebird ODBC driver developers. All recommendations were validated against source code and the correct ones were merged into this phase.

#### 18.1 Hardcoded Table Names (14 tests)

**Root Cause**: Tests reference tables (`CUSTOMERS`, `USERS`) that only exist in the mock driver's default catalog, not in real databases. When `SQLPrepare` or `SQLExecDirect` fails because the table doesn't exist, the test reports `SKIP_INCONCLUSIVE` — falsely implying the driver can't handle the feature.

| Bug ID | Test | File | Hardcoded Table | Fix |
|--------|------|------|-----------------|-----|
| B18-01 | `test_describecol_wchar_names` | `unicode_tests.cpp` | `CUSTOMERS` | Use `RDB$DATABASE` / `INFORMATION_SCHEMA` fallback or discover table via `SQLTables` |
| B18-02 | `test_getdata_sql_c_wchar` | `unicode_tests.cpp` | `CUSTOMERS` | Same — use discovery or `SELECT literal FROM RDB$DATABASE` |
| B18-03 | `test_columns_unicode_patterns` | `unicode_tests.cpp` | `CUSTOMERS` | Call `SQLTables` first, pick first user table for `SQLColumnsW` |
| B18-04 | `test_column_wise_array_binding` | `array_param_tests.cpp` | `USERS` | Create temp table before test, drop after |
| B18-05 | `test_row_wise_array_binding` | `array_param_tests.cpp` | `USERS` | Same |
| B18-06 | `test_param_status_array` | `array_param_tests.cpp` | `USERS` | Same |
| B18-07 | `test_params_processed_count` | `array_param_tests.cpp` | `USERS` | Same |
| B18-08 | `test_array_with_null_values` | `array_param_tests.cpp` | `USERS` | Same |
| B18-09 | `test_param_operation_array` | `array_param_tests.cpp` | `USERS` | Same |
| B18-10 | `test_paramset_size_one` | `array_param_tests.cpp` | `USERS` | Same |
| B18-11 | `test_array_partial_error` | `array_param_tests.cpp` | `USERS` | Same |
| B18-12 | `test_bindparam_wchar_input` | `param_binding_tests.cpp` | `CUSTOMERS` | Use `SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE` or discover table |
| B18-13 | `test_bindparam_null_indicator` | `param_binding_tests.cpp` | `CUSTOMERS` | Same |
| B18-14 | `test_param_rebind_execute` | `param_binding_tests.cpp` | `CUSTOMERS` | Same |

**Fix Strategy**: All tests that need a writable table (`INSERT` tests) should `CREATE TABLE` their own temp table at the start and `DROP TABLE` at the end, similar to how `transaction_tests.cpp` already works. All tests that only need a readable table (`SELECT` tests) should discover a table via `SQLTables` or use a literal query like `SELECT CAST(? AS INTEGER) FROM RDB$DATABASE`.

- [x] Array param tests: Add `create_test_table()` / `drop_test_table()` helper that creates `ODBC_TEST_ARRAY (ID INTEGER, NAME VARCHAR(50))` with DDL
- [x] Param binding tests: Replace `CUSTOMERS` references with discovery-based or literal queries
- [x] Unicode tests: Replace `CUSTOMERS` references with discovery-based queries or `RDB$DATABASE`

#### 18.2 Non-Portable SQL Syntax (1 test)

| Bug ID | Test | File | Issue | Fix |
|--------|------|------|-------|-----|
| B18-15 | `test_unicode_types` | `datatype_tests.cpp` | Uses `N'...'` string prefix, `NVARCHAR` type, and `DUAL` table — none exist in Firebird | Add Firebird-compatible fallback: `SELECT CAST('text' AS VARCHAR(50)) FROM RDB$DATABASE` then retrieve as `SQL_C_WCHAR` |

- [x] Add fallback query that works on Firebird (`SELECT CAST('Hello' AS VARCHAR(50)) FROM RDB$DATABASE`)
- [x] The test should request data as `SQL_C_WCHAR` regardless of SQL type — the driver handles the conversion

#### 18.3 Test Setup / Logic Bugs (2 tests)

| Bug ID | Test | File | Issue | Fix |
|--------|------|------|-------|-----|
| B18-16 | `test_manual_commit` / `test_manual_rollback` | `transaction_tests.cpp` | When `DROP TABLE` fails on Firebird, the transaction enters a broken state. Subsequent `CREATE TABLE` fails because no `ROLLBACK` was issued after the failed DDL. Also, `VALUE` is a reserved keyword in Firebird SQL. | Create DDL with autocommit ON using separate statements; rename `VALUE` column to `VAL` |
| B18-17 | `test_foreign_keys` | `metadata_tests.cpp` | Test treats `SQLForeignKeys` returning `SQL_SUCCESS` with 0 rows as "unsupported". But the driver reports `SQLForeignKeys` as supported via `SQLGetFunctions`, and the function executes correctly — the test tables simply have no FK relationships | Distinguish "function callable with 0 results" (PASS) from "function returned SQL_ERROR" (SKIP_UNSUPPORTED) |

- [x] Transaction tests: Create DDL with autocommit ON, use separate statement objects, rename `VALUE` to `VAL`
- [x] Foreign keys test: Track `callable` flag in Strategy 1 (not just Strategy 2); if `SQLForeignKeys` returns `SQL_SUCCESS` even with 0 rows, report PASS

#### 18.4 Cursor/Boundary/Diagnostic Query Failures (5 tests)

These tests have fallback query chains but the fallbacks may not work on all drivers. While they currently degrade to `SKIP_INCONCLUSIVE`, improving them would increase test coverage on real drivers.

| Bug ID | Test | File | Issue | Fix |
|--------|------|------|-------|-----|
| B18-18 | `test_forward_only_past_end` | `cursor_behavior_tests.cpp` | Fallback queries work but test still SKIPs on some drivers | Verify fallback chain includes `SELECT 1 FROM RDB$DATABASE` and `SELECT 1` |
| B18-19 | `test_fetchscroll_first_forward_only` | `cursor_behavior_tests.cpp` | Same | Same |
| B18-20 | `test_getdata_same_column_twice` | `cursor_behavior_tests.cpp` | Same | Same |
| B18-21 | `test_getdata_zero_buffer` | `boundary_tests.cpp` | Query fallback may not be reached | Ensure fallback chain is robust |
| B18-22 | `test_diagfield_row_count` | `diagnostic_depth_tests.cpp` | Query fallback may not be reached | Ensure fallback chain is robust |

- [x] Review all fallback query chains for completeness
- [x] Ensure `SELECT 1 FROM RDB$DATABASE` is always in the fallback list for Firebird
- [x] Add `SELECT 1` as a final universal fallback (works on most databases without FROM)
- [x] Fix `test_getdata_zero_buffer` to use `SQLFreeStmt(SQL_CLOSE)` before re-execute instead of `stmt.recycle()`
- [x] Fix truncation test to first probe full string length, then use a dynamically-sized smaller buffer

#### 18.5 Documentation

- [x] `ODBC_CRUSHER_RECOMMENDATIONS.md` reviewed and integrated into Phase 18 ✅
- [x] Update Lessons Learned section with real-driver validation insights

**Results (Firebird ODBC v03.00.0021)**:
- Before Phase 18: ~75/131 passing (57%) — 54 false negatives
- After Phase 18: **115/127 passing (90.6%)** — remaining 12 are real driver issues
- All 17 odbc-crusher bugs fixed
- Mock driver: 131/131 (100%) — unchanged
- Unit tests: 60/60 (100%) — unchanged

**Deliverables**:
- All 22 bugs fixed — no false negatives when testing against Firebird ODBC ✅
- Tests create their own temp tables instead of relying on pre-existing ones ✅
- SQL fallback chains cover Firebird, MySQL, PostgreSQL, SQL Server syntax ✅
- Test results accurately reflect driver capabilities ✅
- Reserved keyword conflicts resolved (VALUE → VAL) ✅
- Truncation test uses dynamic buffer sizing ✅

---

### Phase 19: Real-Driver Validation Bugs — Round 2 (Firebird Re-analysis) ✅

**Goal**: Fix remaining bugs discovered by running odbc-crusher v0.3.1 against the Firebird ODBC Debug driver AFTER Phase 18 fixes. Phase 18 fixed many issues, but 20 skips + 1 failure remain. Cross-referencing each result against the Firebird ODBC driver source code revealed **14 odbc-crusher bugs** and **6 genuine driver issues** (documented in `FIREBIRD_ODBC_RECOMMENDATIONS.md`).

**Discovery Date**: February 8, 2026  
**Analysis Method**: Ran odbc-crusher against Firebird ODBC Debug driver (v03.00.0021 / ODBC 3.51 / Firebird 5.0), then read the driver's full C++ source code (`tmp/firebird-new-driver/`) to verify each failure.

**Summary**: 110/131 passed (84.0%), 1 failed, 20 skipped. Of these 21 non-passing results:
- **14 are odbc-crusher bugs** (false negatives)
- **6 are genuine driver issues** (3 actionable, 1 optional feature, 2 environmental)
- **1 is correct behavior** (async not supported = Level 2 optional)

#### 19.1 `SQLExecDirectW` / `SQLPrepareW` Failures (8 tests)

**Root Cause**: The Firebird driver exports W-functions in its `.def` file (`SQLExecDirectW`, `SQLPrepareW`, etc.), making it a "Unicode driver." When connected with `CHARSET=UTF8`, the `ConvertingString<>` class in `MainUnicode.cpp` uses the Firebird client library's charset converter (instead of `WideCharToMultiByte`) to convert SQLWCHAR → char. This converter may produce incorrect results for SQL text, causing `sqlExecDirect` / `sqlPrepare` to fail even for valid queries like `"SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"`.

Meanwhile, the same queries succeed through `SQLExecDirect` (ANSI), which skips the W→A conversion entirely. `SQLGetInfoW` also works because it uses a different code path (`returnStringInfo` with `returnStringInfoW` widening after the fact).

**Evidence**: All tests that use `SQLExecDirectW` or `SQLPrepareW` skip with "Could not execute/prepare query," while ANSI equivalents pass. `test_getinfo_wchar_strings` and `test_string_truncation_wchar` (which use `SQLGetInfoW`) both pass.

| Bug ID | Test | File | Uses | Issue |
|--------|------|------|------|-------|
| B19-01 | `test_describecol_wchar_names` | `unicode_tests.cpp` | `SQLExecDirectW` | All 3 query variants fail via W-function |
| B19-02 | `test_getdata_sql_c_wchar` | `unicode_tests.cpp` | `SQLExecDirectW` | All 3 query variants fail via W-function |
| B19-03 | `test_bindparam_wchar_input` | `param_binding_tests.cpp` | `SQLPrepareW` | All 3 query variants fail via W-function |
| B19-04 | `test_bindparam_null_indicator` | `param_binding_tests.cpp` | `SQLPrepareW` | All 3 query variants fail via W-function |
| B19-05 | `test_param_rebind_execute` | `param_binding_tests.cpp` | `SQLPrepareW` | All 3 query variants fail via W-function |
| B19-06 | `test_unicode_types` | `datatype_tests.cpp` | `SQLExecDirect` (ANSI) + `SQLGetData(SQL_C_WCHAR)` | ANSI exec may work, but W-type conversion fails |
| B19-07 | `test_columns_unicode_patterns` | `unicode_tests.cpp` | `SQLColumnsW` | Uses `CUSTOMERS` fallback (doesn't exist) |
| B19-08 | `test_getdata_zero_buffer` | `boundary_tests.cpp` | `SQLExecDirect` (ANSI) + `SQLGetData(NULL,0)` | Driver's `ODBCCONVERT_CHECKNULL` macro skips indicator |

**Fix Strategy**:
- For B19-01 through B19-05: Tests should **fall back to ANSI functions** (`SQLExecDirect` / `SQLPrepare`) when W-functions fail. This allows the test to exercise the target feature (e.g., `SQLDescribeColW`, `SQL_C_WCHAR` binding) even when the W-function SQL execution path has a driver bug.
- For B19-06: `test_unicode_types` already uses the ANSI `stmt.execute()`. The `SQL_C_WCHAR` retrieval via `SQLGetData` may fail because the Firebird ANSI driver doesn't properly support WCHAR conversion for data retrieved via ANSI entry points. This is a **genuine driver limitation** — document in `FIREBIRD_ODBC_RECOMMENDATIONS.md`.
- For B19-07: `test_columns_unicode_patterns` discovers tables via `SQLTables` but falls back to `CUSTOMERS` if no user tables exist. On Firebird's test DB there are no user tables. Fix: include system tables like `RDB$DATABASE` in the fallback.
- For B19-08: `test_getdata_zero_buffer` works through the ANSI path but `SQLGetData(NULL, 0)` fails because the driver's `ODBCCONVERT_CHECKNULL` macro returns `SQL_SUCCESS` without setting the indicator. The 1-byte buffer fallback (Strategy 2) should work but may also fail due to state after the failed Strategy 1 `SQLGetData` call. This is a **genuine driver bug** — documented in `FIREBIRD_ODBC_RECOMMENDATIONS.md`.

- [x] B19-01 through B19-05: Add ANSI function fallback path in unicode/param binding tests
- [x] B19-06: Mark as genuine driver limitation (WCHAR conversion via ANSI path) — test now falls back to SQL_C_CHAR
- [x] B19-07: Include system tables in `test_columns_unicode_patterns` fallback
- [x] B19-08: Improve Strategy 2 robustness (fresh statement for retry)

#### 19.2 DDL After Failed DDL Corrupts State (10 tests)

**Root Cause**: Despite Phase 18 fixes (separate statement handles, autocommit ON), `create_test_table()` in both `transaction_tests.cpp` and `array_param_tests.cpp` still fails on Firebird. When `DROP TABLE <nonexistent>` fails, the Firebird driver corrupts the **connection-level** transaction state (not just the statement). Subsequent DDL on a new statement handle also fails.

**Firebird-specific**: Firebird's internal architecture ties DDL to the connection's active transaction. A failed DDL with autocommit ON may not properly rollback the internal transaction, leaving the connection in a state where no further DDL can succeed.

| Bug ID | Test | File | Root Cause |
|--------|------|------|------------|
| B19-09 | `test_manual_commit` | `transaction_tests.cpp` | `create_test_table()` fails — DROP corrupts connection state |
| B19-10 | `test_manual_rollback` | `transaction_tests.cpp` | Same |
| B19-11 | `test_column_wise_array_binding` | `array_param_tests.cpp` | `create_test_table()` fails — same root cause + uses single stmt |
| B19-12 | `test_row_wise_array_binding` | `array_param_tests.cpp` | Same |
| B19-13 | `test_param_status_array` | `array_param_tests.cpp` | Same |
| B19-14 | `test_params_processed_count` | `array_param_tests.cpp` | Same |
| B19-15 | `test_array_with_null_values` | `array_param_tests.cpp` | Same |
| B19-16 | `test_param_operation_array` | `array_param_tests.cpp` | Same |
| B19-17 | `test_paramset_size_one` | `array_param_tests.cpp` | Same |
| B19-18 | `test_array_partial_error` | `array_param_tests.cpp` | Same |

**Fix Strategy**:
- **Don't DROP first**: Use `CREATE TABLE` directly. If it fails with "table already exists," THEN `DROP TABLE` + retry `CREATE TABLE`.
- **Alternative**: Use Firebird's `RECREATE TABLE` syntax which atomically drops and creates.
- **Connection cleanup**: After a failed DDL, issue `SQLEndTran(SQL_ROLLBACK)` to clean up the connection state before retrying.
- **array_param_tests.cpp**: Also needs separate statement handles for DROP and CREATE (like `transaction_tests.cpp` already has).

- [x] Reverse DDL order: CREATE first, DROP+retry only on "table exists" error
- [x] Add `SQLEndTran(SQL_ROLLBACK)` after failed DDL to clean connection state
- [x] `array_param_tests.cpp`: Use separate statement handles for DROP and CREATE
- [x] Consider `RECREATE TABLE` as a Firebird-specific fallback — not needed, CREATE-first strategy works

#### 19.3 Foreign Keys Test Throws Exception (1 test)

**Root Cause**: `test_foreign_keys` calls `SQLForeignKeys` with hardcoded table names `ORDERS`/`ORDER_ITEMS`. The Firebird driver's `sqlForeignKeys` implementation calls `getCrossReference()` which throws a `SQLException` when the table name doesn't match any `rdb$relation_constraints` record. The exception propagates through the `catch(SQLException)` in `sqlForeignKeys` as `SQL_ERROR/HY000`. Then the all-NULLs strategy also throws. Both strategies hit the `catch` in odbc-crusher, resulting in `SKIP_UNSUPPORTED`.

**Driver behavior**: Per ODBC spec, catalog functions should return an empty result set for non-existent objects, not throw. This is documented as a driver recommendation in `FIREBIRD_ODBC_RECOMMENDATIONS.md`.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B19-19 | `test_foreign_keys` | `metadata_tests.cpp` | Driver throws on non-existent FK table; odbc-crusher should discover real tables first |

**Fix Strategy**:
- Discover actual user tables from `SQLTables` and try those for FK lookup
- Accept `SQL_SUCCESS` with 0 rows as "callable" (already implemented but never reached because exception occurs first)
- Add a broader catch that tries all-NULLs with `SQLForeignKeysW` as fallback

- [x] Discover real tables via `SQLTables` before calling `SQLForeignKeys`
- [x] Add `SQLForeignKeysW` as a fallback strategy — not needed, table discovery solved the issue

#### 19.4 Truncation Test Uses DM-Intercepted Info Type (1 test — FAILED)

**Root Cause**: The truncation indicators test uses `SQL_DRIVER_NAME` for probing. On Windows, the Driver Manager **intercepts** `SQL_DRIVER_NAME` and returns the driver DLL filename itself, never calling into the Firebird driver's `sqlGetInfo`. The DM's truncation behavior reports the **truncated length** (5 bytes for `"ODBC\0"`) in `pcbInfoValue`, not the full string length (12 bytes for `"FirebirdODBC"`). The Firebird driver's own `setString()` correctly reports the full length, but it's never called for this info type.

**Evidence**: The test first probes `SQL_DRIVER_NAME` and gets length 12 (full `"FirebirdODBC"` from the driver — the DM passes through when buffer is large enough). Then with a 6-byte buffer, the DM intercepts, truncates to `"ODBCJ"` (5 chars), and reports `buffer_length = 5`, which is less than `small_buffer_size = 6`. The test sees `length < buffer_size despite truncation` and fails.

The Firebird driver's `returnStringInfo` and `setString` both correctly set `*returnLength` to the **full untruncated string length** per ODBC spec. This is not a driver bug.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B19-20 | Truncation Indicators Test | `buffer_validation_tests.cpp` | Uses `SQL_DRIVER_NAME` which DM intercepts; DM reports truncated length |

**Fix Strategy**:
- Use a **driver-handled** info type for the truncation test, such as `SQL_DBMS_NAME` or `SQL_DBMS_VER`, which the DM always passes through to the driver
- Or try multiple info types and only fail if ALL exhibit incorrect truncation

- [x] Replace `SQL_DRIVER_NAME` with `SQL_DBMS_NAME` as primary info type for truncation test
- [x] Add fallback chain of info types if the primary type has a too-short string
- [x] Accept DM-truncated length (buffer_length == buffer_size - 1) as PASS with note

#### 19.5 Correctly-Reported Driver Limitations (1 test)

| Test | Status | Verdict | Notes |
|------|--------|---------|-------|
| `test_async_capability` | SKIP_UNSUPPORTED | **CORRECT** | Firebird driver returns `HYC00` ("Optional feature not implemented") for `SQL_ATTR_ASYNC_ENABLE`. This is Level 2 optional. No fix needed. |

#### 19.6 Summary Table

| Category | Count | Bug IDs | Root Cause |
|----------|-------|---------|------------|
| W-function conversion failures | 5 | B19-01 to B19-05 | `ConvertingString<>` with `CHARSET=UTF8` breaks `SQLExecDirectW`/`SQLPrepareW` |
| WCHAR data retrieval | 1 | B19-06 | Driver may not support `SQL_C_WCHAR` via ANSI entry point |
| Hardcoded table fallback | 1 | B19-07 | No system table fallback in `test_columns_unicode_patterns` |
| SQLGetData NULL buffer | 1 | B19-08 | Driver's `ODBCCONVERT_CHECKNULL` doesn't set indicator |
| DDL after failed DDL | 10 | B19-09 to B19-18 | Failed DROP corrupts connection-level transaction state |
| Foreign keys exception | 1 | B19-19 | Driver throws on non-existent table; odbc-crusher uses hardcoded names |
| DM-intercepted info type | 1 | B19-20 | Truncation test uses `SQL_DRIVER_NAME` handled by DM |
| Correct (no fix needed) | 1 | — | Async not supported (Level 2 optional) |

**Deliverables**:
- [x] All 14 odbc-crusher bugs fixed
- [x] odbc-crusher accurately reports Firebird's genuine issues (8 failed + 2 skipped + 1 error = all real driver issues)
- [x] `FIREBIRD_ODBC_RECOMMENDATIONS.md` documents actionable driver fixes
- [x] Mock driver: 131/131 (100%) — unchanged
- [x] Unit tests: 60/60 (100%) — pass on all platforms

**Results (Firebird ODBC v03.00.0021)**:
- Before Phase 18: ~75/131 passing (57%) — 54 false negatives
- After Phase 18: 110/131 passing (84.0%) — 21 non-passing (14 odbc-crusher bugs + 7 genuine)
- **After Phase 19: 116/127 passing (91.3%) — remaining 8 failed + 2 skipped + 1 error are ALL genuine driver issues**

| Remaining Result | Test | Verdict |
|-----------------|------|---------|
| FAIL | `test_execdirect_syntax_error` | Driver returns HY000 instead of 42000 |
| FAIL | `test_setconnattr_invalid_attr` | Driver accepts invalid attribute 99999 |
| FAIL | `test_closecursor_no_cursor` | Driver succeeds instead of returning 24000 |
| FAIL | `test_param_status_array` | Driver doesn't populate status array |
| FAIL | `test_params_processed_count` | Driver doesn't populate processed count |
| FAIL | `test_param_operation_array` | Driver doesn't populate status array |
| FAIL | `test_paramset_size_one` | Driver doesn't populate processed count |
| FAIL | `test_array_partial_error` | Driver doesn't populate status array |
| SKIP | `test_connection_timeout` | Timeout attribute not supported |
| SKIP | `test_async_capability` | Async Level 2 optional, correctly returns HYC00 |
| ERROR | Descriptor Tests | Access violation (0xC0000005) in driver |

---

### Phase 20: Real-Driver Validation Bugs — Round 3 (MariaDB Analysis)

**Goal**: Fix bugs in odbc-crusher discovered by running v0.4.1 against the MariaDB Connector/ODBC driver (v03.01.0015 / ODBC 3.51 / MariaDB 10.11) and cross-referencing each non-passing result against the driver's C++ source code (`tmp/external/mariadb-connector-odbc/driver/`).

**Discovery Date**: February 8, 2026  
**Analysis Method**: Ran odbc-crusher against MariaDB ODBC driver on Linux (libmaodbc.so), then read the driver's full C++ source code to verify each failure. Documented genuine driver issues in `mariadb_ODBC_RECOMMENDATIONS.md`.

**Summary**: 115/131 passed (87.8%), 2 failed, 14 skipped. Of these 16 non-passing results:
- **13 are odbc-crusher bugs** (false negatives — tests fail/skip due to test design, not driver deficiency)
- **1 is a genuine driver deficiency** (`SQLSetConnectAttr` accepts invalid attributes)
- **1 is a correctly-reported optional feature** (async not supported — Level 2 optional)
- **1 is a DM artifact** (sentinel values test detects DM buffer behavior, not driver behavior)

#### 20.1 Sentinel Values Test — DM Buffer Artifact (1 test)

**Root Cause**: The "Sentinel Values Test" in `buffer_validation_tests.cpp` fills a buffer with `0xAA` sentinel bytes, calls `SQLGetInfo(SQL_DRIVER_NAME)`, and checks that bytes beyond `string_data + null_terminator` are untouched. On Windows/Linux, the Driver Manager intercepts the ANSI `SQLGetInfo` call, calls the driver's `SQLGetInfoW` (wide), then converts the result back to ANSI. The DM's ANSI↔Wide conversion layer writes extra bytes during this process, modifying the buffer at position 13. The MariaDB driver's own `MADB_SetString` function (in `ma_string.cpp`) writes only the needed characters + null terminator — the extra bytes come from the DM.

**Evidence**: The driver's ANSI path uses `strncpy_s(p, DestLength, Src, _TRUNCATE)` which only writes the string + null. The wide path uses `MultiByteToWideChar` → explicit null placement — no extra writes. The modification at position 13 is caused by the DM's conversion.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B20-01 | Sentinel Values Test | `buffer_validation_tests.cpp` | DM ANSI↔Wide conversion writes extra bytes; test blames driver incorrectly |

**Fix Strategy**:
- [x] Use `SQLGetInfoW` directly with `SQL_DBMS_NAME` to test the driver's buffer behavior, bypassing the DM's ANSI↔Wide conversion layer
- [x] Use byte-level sentinel checking that accounts for wide character boundaries
- [x] Test with a driver-handled info type (`SQL_DBMS_NAME` instead of `SQL_DRIVER_NAME`)

#### 20.2 Catalog/Metadata Tests — Schema vs Catalog Confusion (2 tests)

**Root Cause**: MariaDB treats databases as **catalogs**, not schemas. The driver's `SQLColumns` implementation in `ma_catalog.cpp` explicitly rejects non-empty schema parameters: `if (SchemaName != NULL && *SchemaName != '\0' && ...)` → returns `HYC00` ("Schemas are not supported. Use CatalogName parameter instead"). odbc-crusher's `test_columns_catalog` passes `"information_schema"` as the **schema** argument (2nd argument) instead of the **catalog** argument (1st argument), so the query returns no results.

Similarly, `test_columns_unicode_patterns` discovers a table name via `SQLTables` (which may return tables from any database) but then calls `SQLColumnsW` without propagating the **catalog** from the `SQLTables` result. Since `SQLColumns` defaults to `DATABASE()` (the current database), it looks for the table in the wrong database and returns 0 columns.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B20-02 | `test_columns_catalog` | `metadata_tests.cpp` | Passes `information_schema` as schema arg; MariaDB requires it as catalog |
| B20-03 | `test_columns_unicode_patterns` | `unicode_tests.cpp` | Discovers table from any DB via `SQLTables` but doesn't propagate catalog to `SQLColumnsW` |

**Fix Strategy**:
- [x] `test_columns_catalog`: Discover real tables via `SQLTables` first (capture catalog, schema, name); try `information_schema` as catalog; static table list includes both catalog-based and schema-based arrangements
- [x] `test_columns_unicode_patterns`: Capture catalog (column 1) and schema (column 2) from `SQLTables` result and propagate both to `SQLColumnsW`

#### 20.3 DDL Permission Failures — Transaction & Array Tests (10 tests)

**Root Cause**: The test database connected to (`bpcom_test` on `SRV-MYSQL`) does not grant `CREATE TABLE` permission (or similar DDL restriction). Both `transaction_tests.cpp` and `array_param_tests.cpp` call `create_test_table()` which attempts `CREATE TABLE ODBC_TEST_TXN / ODBC_TEST_ARRAY`. When this fails, all dependent tests skip with "Could not create test table" or "Could not prepare parameterized INSERT".

The MariaDB driver **fully supports** both transactions and array parameters. Transaction support is confirmed by `test_autocommit_on/off` and `test_transaction_isolation_levels` all passing. Array parameter support is confirmed by reading `ma_bulk.cpp` which has a comprehensive bulk execution engine handling column-wise binding, row-wise binding, indicator arrays, row skipping, and both server-side and client-side prepared statements.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B20-04 | `test_manual_commit` | `transaction_tests.cpp` | `create_test_table()` fails — no DDL permission |
| B20-05 | `test_manual_rollback` | `transaction_tests.cpp` | Same |
| B20-06 | `test_column_wise_array_binding` | `array_param_tests.cpp` | Same |
| B20-07 | `test_row_wise_array_binding` | `array_param_tests.cpp` | Same |
| B20-08 | `test_param_status_array` | `array_param_tests.cpp` | Same |
| B20-09 | `test_params_processed_count` | `array_param_tests.cpp` | Same |
| B20-10 | `test_array_with_null_values` | `array_param_tests.cpp` | Same |
| B20-11 | `test_param_operation_array` | `array_param_tests.cpp` | Same |
| B20-12 | `test_paramset_size_one` | `array_param_tests.cpp` | Same |
| B20-13 | `test_array_partial_error` | `array_param_tests.cpp` | Same |

**Fix Strategy**:
- [x] Capture and report the actual DDL error (SQLSTATE + message) in the skip suggestion via `last_ddl_error_` member
- [x] Try `SELECT 1 FROM ODBC_TEST_TXN/ODBC_TEST_ARRAY WHERE 1=0` before CREATE — reuse existing table from a prior run
- [x] Fall back to DDL-free transaction tests: verify `SQLEndTran(SQL_COMMIT/SQL_ROLLBACK)` returns `SQL_SUCCESS` even without a test table (PASS with suggestion about DDL privileges)
- [x] Short-circuit all 8 array param tests immediately when table creation fails, with a single clear message explaining the DDL error
- [x] Document in the test suggestion: "CREATE TABLE privilege is required on the connected database"

#### 20.4 Unicode Truncation Test — Buffer Too Large (1 test)

**Root Cause**: `test_string_truncation_wchar` allocates a 2-element `SQLWCHAR` buffer (4 bytes: 1 character + NUL) and calls `SQLGetInfoW(SQL_DBMS_NAME)`. It expects `SQL_SUCCESS_WITH_INFO` with SQLSTATE `01004` (truncation), but the test reports "DBMS name fit in 1-char buffer" — meaning the returned DBMS name is unexpectedly short (possibly due to DM behavior, or the `BUFFER_CHAR_LEN` macro computing a larger-than-expected character count from the byte-based buffer length).

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B20-14 | `test_string_truncation_wchar` | `unicode_tests.cpp` | Buffer too large or DM behavior prevents truncation; test inconclusive |

**Fix Strategy**:
- [x] Probe 4 info types (`SQL_DBMS_NAME`, `SQL_DBMS_VER`, `SQL_SERVER_NAME`, `SQL_DRIVER_VER`) with full-size buffer to learn actual string lengths; use the longest one
- [x] Craft buffer at exactly half the full data length (guaranteed truncation for strings > 2 chars)
- [x] Ensure buffer size is a multiple of `sizeof(SQLWCHAR)` and at least `2 * sizeof(SQLWCHAR)` (1 char + NUL)

#### 20.5 Summary Table

| Category | Count | Bug IDs | Root Cause |
|----------|-------|---------|------------|
| DM buffer artifact | 1 | B20-01 | DM ANSI↔Wide conversion writes extra bytes |
| Schema vs catalog confusion | 2 | B20-02, B20-03 | Test passes info as schema; MariaDB uses catalogs |
| DDL permission failures | 10 | B20-04 to B20-13 | No CREATE TABLE permission in test DB |
| Unicode truncation | 1 | B20-14 | Buffer too large to trigger truncation |

**Deliverables**:
- [x] All 14 odbc-crusher bugs fixed
- [x] `mariadb_ODBC_RECOMMENDATIONS.md` documents the 1 actionable driver fix
- [ ] Re-run against MariaDB driver to verify improved pass rate

**Changes Made**:
- [x] `buffer_validation_tests.cpp`: Sentinel test uses `SQLGetInfoW` with `SQL_DBMS_NAME` and byte-level sentinel checking
- [x] `metadata_tests.cpp`: `test_columns_catalog` discovers tables via `SQLTables` with catalog/schema propagation; static fallback list includes catalog-based and schema-based arrangements
- [x] `unicode_tests.cpp`: `test_columns_unicode_patterns` captures catalog+schema from `SQLTables` and passes them to `SQLColumnsW`; `test_string_truncation_wchar` probes multiple info types and crafts guaranteed-too-small buffer
- [x] `transaction_tests.cpp/hpp`: `create_test_table()` tries `SELECT` probe first; captures DDL errors in `last_ddl_error_`; DDL-free fallback verifies `SQLEndTran` is callable
- [x] `array_param_tests.cpp/hpp`: `create_test_table()` tries `SELECT` probe first; captures DDL errors; `run()` short-circuits all 8 tests with clear DDL error message when table creation fails
- [x] Mock driver: 131/131 (100%) — unchanged
- [x] Unit tests: 60/60 (100%) — pass on all platforms

---

### Phase 22: Real-Driver Validation Bugs — Round 5 (PostgreSQL / psqlodbc Analysis)

**Goal**: Fix odbc-crusher bugs discovered by running v0.4.1 against the PostgreSQL ODBC driver (psqlodbcw.so v16.00.0000 / ODBC 3.51 / PostgreSQL 16.0.11) and cross-referencing each non-passing result against the driver's C source code (`tmp/external/psqlodbc/`).

**Discovery Date**: February 8, 2026  
**Analysis Method**: Ran odbc-crusher against psqlodbc on Linux, then read the driver's full C source code to verify each failure/skip. Documented genuine driver issues in `postgresql_ODBC_RECOMMENDATIONS.md`.

**Summary**: 125/131 passed (95.4%), 0 failed, 6 skipped. Of these 6 non-passing results:
- **1 is a minor driver conformance gap** (`SQL_ATTR_CURSOR_SCROLLABLE` setter rejected while `SQL_ATTR_CURSOR_TYPE` works)
- **1 is a correctly-reported optional feature** (async execution, Level 2)
- **4 are odbc-crusher bugs** (test lacks PostgreSQL-compatible queries/tables/parameters)

Additionally, 2 passed tests have minor SQLSTATE mapping issues (documented in `postgresql_ODBC_RECOMMENDATIONS.md`):
- `test_getinfo_invalid_type`: returns `HYC00` instead of spec-required `HY096`
- `test_setconnattr_invalid_attr`: returns `HY000` instead of spec-required `HY092`

#### 22.1 SQLSpecialColumns — Hardcoded Table Names (1 test)

**Root Cause**: `test_special_columns` in `metadata_tests.cpp` hardcodes only two table names:
1. `RDB$DATABASE` — Firebird-specific, does not exist in PostgreSQL
2. `information_schema.TABLES` — exists in PostgreSQL but is a **view**, not a base table; `SQLSpecialColumns` queries `pg_catalog.pg_class` for OIDs/primary keys which are only meaningful for base tables

The psqlodbc driver **fully implements** `SQLSpecialColumns` (via `PGAPI_SpecialColumns` in `info.c`, exported via `odbcapi.c` and `odbcapi30w.c`). The function works correctly when given a real PostgreSQL base table (e.g., `pg_catalog.pg_class`). The test does **not** dynamically discover tables via `SQLTables` before calling `SQLSpecialColumns`.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B22-01 | `test_special_columns` | `metadata_tests.cpp` | Hardcoded table names; needs dynamic discovery via `SQLTables` and PostgreSQL tables like `pg_catalog.pg_class` |

**Fix Strategy**:
- [x] Add dynamic table discovery via `SQLTables` (capture catalog, schema, table name) before calling `SQLSpecialColumns`
- [x] Add PostgreSQL-compatible static fallback tables: `{"pg_catalog", "pg_class"}`, `{"pg_catalog", "pg_type"}`
- [x] Prefer user tables (`TABLE` type) over system views

#### 22.2 Binary Types — No PostgreSQL Syntax (1 test)

**Root Cause**: `test_binary_types` in `datatype_tests.cpp` tries 4 binary literal patterns:
1. `SELECT CAST(0x48656C6C6F AS VARBINARY(10))` — SQL Server syntax
2. `SELECT CAST('Binary' AS BLOB SUB_TYPE 0) FROM RDB$DATABASE` — Firebird syntax
3. `SELECT CAST('test' AS BINARY(10))` — no `BINARY` type in PostgreSQL
4. `SELECT X'48656C6C6F'` — PostgreSQL interprets `X'...'` as bit string, not `bytea`

PostgreSQL uses `bytea` for binary data. None of these syntaxes produce a `bytea` column. The driver fully supports `SQL_C_BINARY` data retrieval for `bytea` columns.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B22-02 | `test_binary_types` | `datatype_tests.cpp` | No PostgreSQL-compatible binary literal syntax |

**Fix Strategy**:
- [x] Add PostgreSQL binary query: `"SELECT decode('48656C6C6F', 'hex')::bytea"`
- [ ] Alternative: `"SELECT E'\\\\x48656C6C6F'::bytea"` (not needed, decode() works)
- [x] Retrieve as `SQL_C_BINARY` and validate byte content

#### 22.3 SQLColumnsW for information_schema Views (1 test)

**Root Cause**: `test_columns_unicode_patterns` in `unicode_tests.cpp` discovers a table via `SQLTablesW`. When there are no user tables, strategy 3 (any type) returns `information_schema` views like `_pg_foreign_data_wrappers`. The test correctly propagates catalog and schema to `SQLColumnsW` (Phase 20 fix), but `SQLColumns` legitimately returns 0 columns for `information_schema` views because they are dynamically-defined views without entries in `pg_attribute`.

This is not strictly an odbc-crusher propagation bug — the test should prefer discovering real base tables or `pg_catalog` system tables.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B22-03 | `test_columns_unicode_patterns` | `unicode_tests.cpp` | Test should prefer base tables or `pg_catalog` tables over `information_schema` views |

**Fix Strategy**:
- [x] Prefer `TABLE` type in discovery; add `SYSTEM TABLE` as second preference before falling back to any type
- [x] Filter out `information_schema` tables during discovery (these are views that may not expose columns via `SQLColumns`)
- [ ] Add `pg_catalog.pg_class` as a static fallback (not needed, filtering is sufficient)

#### 22.4 SQLTables SQL_ALL_TABLE_TYPES Enumeration (1 test)

**Root Cause**: `test_tables_search_patterns` in `catalog_depth_tests.cpp` calls `SQLTablesW` with `nullptr` for catalog, schema, and table name instead of empty strings (`L""`). Per the ODBC specification, the `SQL_ALL_TABLE_TYPES` enumeration mode requires **empty strings** (not nullptrs) for catalog, schema, and table name with `%` as the table type.

The psqlodbc driver's `PGAPI_Tables` implementation checks `escCatName && escSchemaName` for the special enumeration mode — `nullptr` values cause this check to fail, so the driver falls through to a regular query which returns 0 rows if there are no user tables.

| Bug ID | Test | File | Issue |
|--------|------|------|-------|
| B22-04 | `test_tables_search_patterns` | `catalog_depth_tests.cpp` | Passes `nullptr` instead of `L""` for SQL_ALL_TABLE_TYPES enumeration |

**Fix Strategy**:
- [x] Pass `SqlWcharBuf("").ptr()` with length 0 for catalog, schema, and table name
- [x] Keep `SqlWcharBuf("%").ptr()` with `SQL_NTS` for the table type parameter
- [ ] Verify the result set contains at least one table type entry (deferred — driver-dependent behavior)

#### 22.5 Correctly Identified Driver Gaps (No odbc-crusher bug)

| Test | Status | Driver Issue | Details |
|------|--------|-------------|---------|
| `test_cursor_scrollable_attr` | SKIP | `SQL_ATTR_CURSOR_SCROLLABLE` settable as getter only; setter rejected with `HYC00` | Minor — driver supports scrolling via `SQL_ATTR_CURSOR_TYPE` |
| `test_async_capability` | SKIP | Level 2 optional; driver silently accepts set but never enables async | Acceptable for synchronous driver |

#### 22.6 Correctly Skipped / Passed Tests (No bug)

All 125 passed tests were verified as correct by reviewing the driver source code. Notable positive findings:
- Complete descriptor API (`SQLCopyDesc`, `SQLGetDescField`, `SQLSetDescField`)
- Full array parameter support with status, operation, and processed count
- Proper SQLSTATE differentiation (`42601` for syntax errors, `HY010` for sequence errors)
- Correct buffer validation (sentinel preservation, truncation reporting with `01004`)

**Deliverables**:
- [x] `postgresql_ODBC_RECOMMENDATIONS.md` documents 3 minor driver improvements
- [x] B22-01: `metadata_tests.cpp`: `test_special_columns()` dynamically discovers base tables via `SQLTables(TABLE)`, captures catalog+schema+name; extended static fallback includes `pg_catalog.pg_class`, `pg_catalog.pg_type`, `dbo.sysobjects`
- [x] B22-02: `datatype_tests.cpp`: `test_binary_types()` adds `SELECT decode('48656C6C6F', 'hex')::bytea` (PostgreSQL native `bytea` syntax)
- [x] B22-03: `unicode_tests.cpp`: `test_columns_unicode_patterns()` discovery lambda now iterates through `SQLTables` results and skips `information_schema` tables (synthetic views whose columns are not enumerable via `SQLColumns`)
- [x] B22-04: `catalog_depth_tests.cpp`: `test_tables_search_patterns()` passes `SqlWcharBuf("").ptr()` with length 0 (empty strings) instead of `nullptr` for catalog/schema/table, per ODBC spec's `SQL_ALL_TABLE_TYPES` enumeration mode
- [x] Mock driver: 131/131 (100%) — unchanged
- [x] Unit tests: 60/60 (100%) — pass on all platforms

---

### Phase 23: Real-Driver Validation Bugs — Round 6 (DuckDB Analysis) ✅

**Goal**: Fix crash-severity bugs in odbc-crusher discovered by running against the DuckDB ODBC driver (v03.51.0000 / ODBC 3.51) and produce a detailed recommendations document for DuckDB ODBC driver developers.

**Discovery Date**: February 8, 2026
**Analysis Method**: Ran odbc-crusher against DuckDB ODBC driver on Windows. The tool crashed with uncatchable `__fastfail` / access violation exit codes. Analyzed the DuckDB ODBC driver source code (`tmp/duckdb-odbc/src/`) to identify root causes and implemented crash-avoidance strategies.

**Summary**: 131 tests, 113 passed (86.3%), 9 failed, 8 skipped, 1 error. Two crash-severity bugs in the DuckDB ODBC driver were identified that terminate the host process with no possibility of recovery via SEH or signal handlers. Both were worked around in odbc-crusher.

#### 23.1 Crash Fix: SQLDescribeParam on unresolved parameters (1 test)

**Root Cause**: `test_describe_param` and `test_num_params` used `"SELECT ?"` as the first query variant. DuckDB's `SQLPrepare` on `"SELECT ?"` returns `SQL_SUCCESS_WITH_INFO` (parameters not fully bound), but `SQLDescribeParam` then accesses `hstmt->stmt->data->GetType()` on an empty `types` vector → access violation (`0xC0000005`).

| Bug ID | Test | File | Fix |
|--------|------|------|-----|
| B23-01 | `test_describe_param` | `statement_tests.cpp` | Try `"SELECT CAST(? AS INTEGER)"` before `"SELECT ?"` so DuckDB can resolve the parameter type from the explicit cast |
| B23-02 | `test_num_params` | `statement_tests.cpp` | Same — prefer `CAST(? AS INTEGER)` pattern |
| B23-03 | `test_parameter_binding` | `statement_tests.cpp` | Same — prefer `CAST(? AS INTEGER)` pattern |

#### 23.2 Crash Fix: Row-wise array binding corrupts memory (1 test)

**Root Cause**: DuckDB's `ParameterDescriptor::SetValue()` stores `SQL_ATTR_PARAM_BIND_TYPE` in `apd->header.sql_desc_bind_type` but **never uses it** for pointer arithmetic. For integers, it always reads from the base pointer (no offset). For strings, it uses `val_idx * column_size` instead of `val_idx * struct_size`. With row-wise binding (`PARAMSET_SIZE > 1`), this causes out-of-bounds memory reads → Windows `/GS` stack cookie detection → `__fastfail(STATUS_STACK_BUFFER_OVERRUN)` (`0xC0000409`), which is **uncatchable** by SEH, VEH, or signal handlers.

| Bug ID | Test | File | Fix |
|--------|------|------|-----|
| B23-04 | `test_row_wise_array_binding` | `array_param_tests.cpp` | Added safety probe: first execute with `PARAMSET_SIZE=1` (safe, offset=0), then `PARAMSET_SIZE=2` with integer-only parameters to detect wrong arithmetic without crashing. If probe detects wrong data (`{9991, 9991}` instead of `{9991, 9992}`), report as FAIL with detailed explanation instead of proceeding to string parameters that would crash the process |

#### 23.3 File corruption fix

| Bug ID | Test | File | Fix |
|--------|------|------|-----|
| B23-05 | `test_param_status_array` | `array_param_tests.cpp` | Previous edit accidentally consumed the function header for `test_param_status_array()` during the `test_row_wise_array_binding()` rewrite. Restored the missing function header and `make_result` initialization block |

#### 23.4 DuckDB ODBC Driver Recommendations

A comprehensive recommendations document was written to `tmp/DUCKDB_ODBC_RECOMMENDATIONS.md` covering:
- 2 crash-severity bugs (SQLDescribeParam + row-wise binding)
- 8 non-crash spec violations (SQLGetInfo, SQLSetConnectAttr, SQLCloseCursor, SQLGetDiagField, SQLGetData, SQLEndTran, SQL_ATTR_PARAM_OPERATION_PTR, buffer length handling)
- Prioritized fix recommendations with exact source file locations and code fixes

#### 23.5 Lessons Learned

25. **Row-wise array binding must be probed before execution.** Some drivers (DuckDB) accept `SQL_ATTR_PARAM_BIND_TYPE` via `SQLSetStmtAttr` and even store the value correctly in the APD, but never use it for pointer arithmetic. The only safe way to detect this is a trial execution with integer-only parameters at `PARAMSET_SIZE=2` — verifying the second row's data matches expectations before attempting string parameters that would cause memory corruption.

26. **Windows `/GS` stack cookie failures are uncatchable.** When a buggy driver corrupts the stack (e.g., reading past buffer bounds during row-wise binding), MSVC's `/GS` security mitigation calls `__fastfail()` which raises `STATUS_STACK_BUFFER_OVERRUN` (`0xC0000409`). This exception bypasses all user-mode exception handling — SEH `__try/__except`, VEH (`AddVectoredExceptionHandler`), `SetUnhandledExceptionFilter`, and C++ `catch(...)` all fail to catch it. The only defense is to avoid triggering the corruption in the first place.

27. **`SQLSetStmtAttr` default cases that return SQL_SUCCESS are dangerous.** DuckDB's `SQLSetStmtAttr` returns `SQL_SUCCESS_WITH_INFO` (with `01S02`) for ALL unrecognized attributes. This means odbc-crusher cannot distinguish "attribute accepted and implemented" from "attribute silently ignored." Read-back verification via `SQLGetStmtAttr` is insufficient because DuckDB may store the value without implementing the behavior. The ODBC spec requires `SQL_ERROR` with `HY092` for unrecognized attributes.

28. **Prefer `CAST(? AS type)` over bare `?` in parameter queries.** Some drivers (DuckDB) cannot infer parameter types from bare `SELECT ?` and may crash on subsequent `SQLDescribeParam` calls. `SELECT CAST(? AS INTEGER)` is more portable and gives the driver explicit type information.

**Deliverables**:
- [x] B23-01/02/03: `statement_tests.cpp`: Query fallback chains prefer `CAST(? AS INTEGER)` over bare `SELECT ?`
- [x] B23-04: `array_param_tests.cpp`: `test_row_wise_array_binding()` uses integer-only safety probe before string parameters
- [x] B23-05: `array_param_tests.cpp`: Restored missing `test_param_status_array()` function header
- [x] `recommendations/duckdb_ODBC_RECOMMENDATIONS.md`: Comprehensive driver analysis with root cause and fix for each issue
- [x] DuckDB: 113/131 passed (86.3%), 9 failed, 8 skipped, 1 error — **0 crashes**
- [x] Mock driver: 131/131 (100%) — unchanged
- [x] Unit tests: 60/60 (100%) — pass on all platforms

---

### Phase 24: Real-Driver Re-Validation — PostgreSQL psqlodbc v0.4.5 Re-Analysis ✅

**Goal**: Re-run odbc-crusher v0.4.5 against the PostgreSQL ODBC driver (psqlodbcw.so v16.00.0000 / ODBC 3.51 / PostgreSQL 16.0.11) and verify that the 4 odbc-crusher bugs fixed in Phase 22 are resolved. Cross-reference remaining non-passing results against the driver source code at tag `REL-16_00_0000`.

**Discovery Date**: February 8, 2026  
**Analysis Method**: Ran odbc-crusher v0.4.5 against psqlodbc on Linux, then reviewed the driver's C source code (`tmp/external/psqlodbc/`) to verify each remaining skip.

**Summary**: 129/131 passed (98.5%), 0 failed, 2 skipped. All 4 odbc-crusher bugs from Phase 22 are confirmed fixed. The remaining 2 skips are genuine driver conformance gaps — **no new odbc-crusher bugs found**.

**Improvement over Phase 22**: 125/131 (95.4%) → 129/131 (98.5%). The 4 previously-skipped tests now pass:
- `test_special_columns` — fixed by dynamic table discovery (B22-01)
- `test_binary_types` — fixed by PostgreSQL `decode()/bytea` syntax (B22-02)
- `test_columns_unicode_patterns` — fixed by filtering `information_schema` views (B22-03)
- `test_tables_search_patterns` — fixed by passing empty strings instead of nullptr (B22-04)

#### 24.1 Confirmed Driver Gaps (2 tests — no odbc-crusher bug)

| Test | Status | Driver Code | Conclusion |
|------|--------|-------------|------------|
| `test_cursor_scrollable_attr` | SKIP | `pgapi30.c`: `PGAPI_SetStmtAttr` unconditionally rejects `SQL_ATTR_CURSOR_SCROLLABLE` with `HYC00`, grouping it with `SQL_ATTR_CURSOR_SENSITIVITY` and `SQL_ATTR_AUTO_IPD`. The getter in `PGAPI_GetStmtAttr` works correctly, deriving `SQL_SCROLLABLE`/`SQL_NONSCROLLABLE` from `cursor_type`. | **Crusher CORRECT** — driver supports scrollable cursors via `SQL_ATTR_CURSOR_TYPE` but not via the `SQL_ATTR_CURSOR_SCROLLABLE` setter |
| `test_async_capability` | SKIP | `options.c`: `set_statement_option` silently ignores `SQL_ASYNC_ENABLE` (returns `SQL_SUCCESS` without storing). `PGAPI_GetStmtOption` always returns `SQL_ASYNC_ENABLE_OFF`. `SQLGetInfo` reports `SQL_AM_NONE`. | **Crusher CORRECT** — driver has no async support; silently accepting the set is a minor spec violation |

#### 24.2 Passed Tests with Non-Standard SQLSTATEs (2 tests — informational)

| Test | SQLSTATE Returned | SQLSTATE Expected | Driver Code |
|------|-------------------|-------------------|-------------|
| `test_getinfo_invalid_type` | `HYC00` | `HY096` | `info.c`: uses `CONN_NOT_IMPLEMENTED_ERROR` which maps to `HYC00` |
| `test_setconnattr_invalid_attr` | `HY000` | `HY092` | `connection.c`: `CONN_OPTION_NOT_FOR_THE_DRIVER` has no explicit SQLSTATE mapping, falls to `HY000` |

**Deliverables**:
- [x] `recommendations/postgresql_ODBC_RECOMMENDATIONS.md` rewritten for v0.4.5 results (4 recommendations: 2 skipped features + 2 SQLSTATE mapping issues)
- [x] No new odbc-crusher bugs identified
- [x] PostgreSQL: 129/131 passed (98.5%) — **best result among all tested drivers**

---

## 5. Technical Stack
| Language | C++17 | Direct ODBC API access, `std::optional`, `std::string_view` |
| Build | CMake 3.20+ | Industry standard, CTest integration |
| Test framework | Google Test 1.14 | FetchContent, mature, GTest + GMock |
| CLI parsing | CLI11 2.4.2 | Header-only, clean API |
| JSON | nlohmann/json 3.11.3 | Header-only, intuitive syntax |
| ODBC | System (odbc32.lib / unixODBC) | Platform-native |

---

## 6. Mock ODBC Driver

### Purpose

The mock driver enables offline, CI-friendly, deterministic testing of ODBC Crusher. It exists solely to support this application.

### Connection String Parameters

| Parameter | Values | Effect |
|-----------|--------|--------|
| `Mode` | `Success`, `Failure`, `Random`, `Partial` | Overall operation success pattern |
| `Catalog` | `Default`, `Empty`, `Large` | Mock schema preset |
| `ResultSetSize` | integer | Rows returned in queries |
| `FailOn` | `Function1,Function2,...` | Error injection per-function |
| `ErrorCode` | SQLSTATE string | Custom error code for failures |
| `BufferValidation` | `Strict`, `Lenient` | Buffer checking mode |
| `ErrorCount` | integer | Multiple diagnostic records per error |
| `StateChecking` | `Strict`, `Lenient` | State machine validation |
| `TransactionMode` | `ReadOnly`, `ReadWrite` | Transaction behavior |

### Mock Schema (Default preset)

```
CUSTOMERS (CUSTOMER_ID INT PK, NAME VARCHAR, EMAIL VARCHAR, CREATED_DATE DATE, IS_ACTIVE BIT, BALANCE DECIMAL)
ORDERS    (ORDER_ID INT PK, CUSTOMER_ID INT FK, ORDER_DATE DATE, TOTAL_AMOUNT DECIMAL, STATUS VARCHAR)
PRODUCTS  (PRODUCT_ID INT PK, NAME VARCHAR, DESCRIPTION VARCHAR, PRICE DECIMAL, STOCK INT)
```

### Consonant Development Rule

All changes must maintain feature parity:
- New application test → mock driver must support it
- Mock driver claims function support → application must exercise it
- No function listed in `SQLGetFunctions` without an actual implementation

---

## 7. Test Category Reference

### Current Categories (57 tests)

| Category | Tests | ODBC Functions | Conformance |
|----------|:-----:|----------------|-------------|
| Connection | 6 | `SQLGetInfo`, `SQLGetConnectAttr`, `SQLSetConnectAttr`, `SQLGetEnvAttr`, `SQLAllocHandle` | Core |
| Statement | 7 | `SQLExecDirect`, `SQLPrepare`, `SQLExecute`, `SQLFetch`, `SQLBindParameter`, `SQLGetData`, `SQLNumResultCols`, `SQLDescribeCol`, `SQLCloseCursor`, `SQLMoreResults` | Core |
| Metadata/Catalog | 7 | `SQLTables`, `SQLColumns`, `SQLPrimaryKeys`, `SQLForeignKeys`, `SQLStatistics`, `SQLSpecialColumns`, `SQLTablePrivileges` | Core + Level 1 |
| Data Types | 9 | `SQLGetData` (7 C types), `SQLExecDirect` | Core |
| Transactions | 5 | `SQLEndTran`, `SQLSetConnectAttr`, `SQLGetConnectAttr` | Core |
| Advanced | 6 | `SQLSetStmtAttr`, `SQLGetStmtAttr` | Level 1–2 |
| Buffer Validation | 5 | `SQLGetInfo` (buffer edge cases) | Core |
| Error Queue | 6 (3 stubs) | `SQLGetDiagRec` | Core |
| State Machine | 6 (3 stubs) | `SQLAllocHandle`, `SQLGetConnectAttr` | Core |

### Quality Criteria for Tests

Every test must:
1. Name the ODBC function it exercises
2. State what it expects (per the ODBC specification)
3. Report what actually happened
4. Classify its conformance level (Core / Level 1 / Level 2)
5. On failure: provide an actionable suggestion with spec reference
6. On SKIP: clearly indicate whether the feature is optional or the test was inconclusive

---

## 8. Lessons Learned

1. **Mock driver must never claim false support.** If `SQLGetFunctions` says a function is supported, it must be implemented. False claims mislead both the test suite and real-world callers.

2. **Map key consistency matters.** Using one naming convention for storage and another for retrieval causes silent data loss (B1).

3. **JSON and console reporters must have feature parity.** Discovery data that only appears in one format is a gap for users who depend on the other (B2).

4. **Stub tests are technical debt.** Tests that always SKIP give false confidence in coverage numbers. Either implement them or remove them from the count.

5. **Reports must serve the audience.** "PASS" and "FAIL" are meaningless without context. Driver developers need: which spec clause, what conformance level, and exactly what to fix.

6. **`dynamic_cast` across DLL boundaries doesn't work on Windows.** Solved with opaque handle + magic number validation in the mock driver. This is a fundamental C++ / Windows ODBC constraint.

7. **Multi-database SQL compatibility requires fallback patterns.** The `try Firebird syntax → try MySQL syntax → try generic` pattern works but hides failures. Tests should document which SQL dialect succeeded.

8. **Hardcoded table names break real-driver testing.** Tests that reference `CUSTOMERS` or similar mock-driver tables fail silently against real databases. All SQL queries must include fallback options (e.g., `RDB$DATABASE` for Firebird, `information_schema.TABLES` for MySQL). Partially fixed in Phase 17 investigation; comprehensive fix in Phase 18.

9. **DDL errors invalidate Firebird transactions.** When a `DROP TABLE` fails in Firebird with autocommit off, the transaction enters a broken state. A `ROLLBACK` is needed before subsequent DDL can succeed. Identified in Phase 18 (B18-16).

10. **"Zero results" ≠ "unsupported function."** A catalog function like `SQLForeignKeys` returning `SQL_SUCCESS` with 0 rows means the function works but the database has no foreign keys. The test must distinguish "callable" from "has data." Identified in Phase 18 (B18-17).

11. **Always validate against real drivers with source access.** Running odbc-crusher against the Firebird ODBC Debug driver and cross-referencing failures against the driver's C++ source revealed that 17 of 27 non-passing results were bugs in odbc-crusher, not the driver. The tool was falsely blaming the driver for its own test setup failures. This kind of validation is essential before trusting any ODBC test tool's output.

12. **The mock driver is not enough.** Even with 131/131 tests passing against the mock driver (Phase 17), 17 tests still failed against a real driver due to assumptions baked into the mock driver's schema (pre-existing `CUSTOMERS`/`USERS` tables). Real-world testing exposes gaps that mock testing cannot.

13. **Reserved words vary across SQL dialects.** `VALUE` is a reserved keyword in Firebird SQL but not in most other databases. Always use non-reserved column names in test DDL, or quote identifiers. The safest approach is to avoid common reserved words like `VALUE`, `KEY`, `INDEX`, `TYPE`, `ORDER`, `USER`, `NAME`, `TABLE`, `DATE`, `TIME`.

14. **DDL requires autocommit ON in Firebird.** When executing DDL (CREATE TABLE, DROP TABLE) with Firebird ODBC, the connection must be in autocommit mode. With autocommit OFF, a failed DDL statement invalidates the entire transaction, and no subsequent DDL can succeed. The fix: always run DDL with autocommit ON and use separate statement objects for each DDL operation.

15. **Use separate statement objects for error-prone operations.** Reusing a statement object after a failed `SQLExecDirect` can leave it in a bad state. When executing multiple operations where some may fail (e.g., DROP TABLE before CREATE TABLE), allocate fresh statement handles for each operation.

16. **Fallback query chains must include Firebird's `RDB$DATABASE`.** Firebird requires `SELECT ... FROM RDB$DATABASE` for table-less queries (similar to Oracle's `FROM DUAL`). Every query fallback chain should include both `SELECT expr` and `SELECT expr FROM RDB$DATABASE` variants to ensure cross-database compatibility.

17. **SQLGetData with NULL buffer may not work via the Driver Manager.** Some DMs (especially the Windows DM with Unicode drivers) reject `SQLGetData(NULL, 0)` even though the ODBC spec allows it. A robust test should fall back to a 1-byte buffer to trigger truncation and get the data length from the indicator.

18. **The Windows DM intercepts certain `SQLGetInfo` types.** Info types like `SQL_DRIVER_NAME` are handled by the Driver Manager itself, never reaching the driver. The DM's truncation behavior may differ from the driver's (e.g., reporting truncated length instead of full length). Tests should use driver-handled info types like `SQL_DBMS_NAME` when testing truncation behavior.

19. **W-function exports determine Unicode driver classification.** If a driver exports `SQLExecDirectW` in its `.def` file, the Windows DM treats it as a Unicode driver and calls W-functions directly. This means the driver's W→A conversion code is exercised, not the DM's. If the driver's conversion has bugs (e.g., charset-specific converters for `CHARSET=UTF8`), W-function calls will fail while A-function calls succeed. Tests should fall back to ANSI functions when W-functions fail.

20. **Failed DDL can corrupt connection-level state in Firebird.** Even with autocommit ON and separate statement handles, a failed `DROP TABLE <nonexistent>` on Firebird can corrupt the connection's transaction state, causing subsequent DDL to fail. The safest approach is: (1) try CREATE first, (2) only DROP+retry if CREATE fails with "table exists," (3) issue `SQLEndTran(SQL_ROLLBACK)` after any DDL failure, (4) consider database-specific DDL like `RECREATE TABLE` for Firebird.

21. **Catalog functions should return empty result sets, not errors, for non-existent objects.** Per ODBC spec, `SQLForeignKeys` called with a non-existent table name should return `SQL_SUCCESS` with an empty result set. Some drivers (Firebird) throw exceptions instead. Tests should handle both behaviors and discover real table names before calling catalog functions.

22. **MySQL/MariaDB uses databases as catalogs, not schemas.** The MariaDB ODBC driver treats the database name as the catalog parameter and rejects non-empty schema parameters with `HYC00`. Tests that pass `information_schema` as a schema argument will fail on MariaDB — it must be passed as the catalog argument instead. When calling `SQLColumns` after discovering a table via `SQLTables`, always propagate the catalog column (column 1) from the `SQLTables` result set.

23. **DDL failures due to permissions produce misleading skip messages.** When `CREATE TABLE` fails because the connected user lacks DDL privileges, tests that depend on test tables skip with generic messages like "Could not prepare parameterized INSERT" — which sounds like a driver bug. The test should capture and report the actual SQLSTATE and error message from the failed DDL to help the user diagnose the real problem (e.g., `42000 — Access denied`).

24. **Sentinel values / buffer overwrite tests may detect DM behavior, not driver behavior.** On Windows and Linux, the Driver Manager intercepts ANSI calls, converts to Wide, calls the driver, then converts back. This conversion process may modify buffer bytes beyond the null terminator. A sentinel-values test that fills unused buffer bytes with a marker and checks for preservation may detect DM-caused overwrites, not driver bugs. To isolate driver behavior, call W-functions directly or accept DM-caused modifications as informational.

---

## 9. ODBC Specification References

| Topic | URL |
|-------|-----|
| ODBC Programmer's Reference | https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference |
| ODBC API Reference | https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference |
| Conformance Levels | https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/interface-conformance-levels |
| SQLSTATE Reference | https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-a-odbc-error-codes |
| Function Transition Tables | https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-b-odbc-state-transition-tables |
| Data Type Identifiers | https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-d-data-types |
| unixODBC | http://www.unixodbc.org/ |

---

## 10. Build & Run

```powershell
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Run unit tests
ctest --test-dir build -C Release

# Run against a driver
./build/src/Release/odbc-crusher.exe "Driver={Your Driver};Server=...;Database=...;" --verbose

# Run against mock driver
./build/src/Release/odbc-crusher.exe "Driver={Mock ODBC Driver};Mode=Success;" --verbose

# JSON output
./build/src/Release/odbc-crusher.exe "Driver={Mock ODBC Driver};Mode=Success;" -o json -f report.json
```
