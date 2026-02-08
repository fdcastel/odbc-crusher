# ODBC Crusher — Project Plan

**Version**: 2.3  
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
