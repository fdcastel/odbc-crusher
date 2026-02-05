# ODBC Crusher — Project Plan

**Version**: 2.1  
**Purpose**: A command-line tool for ODBC driver developers to validate driver correctness, discover capabilities, and identify spec violations.  
**Last Updated**: February 5, 2026

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

### Phase 11: Fix Bugs & Report Quality ⬜

**Goal**: Fix all known bugs and transform the report output from "test log" to "driver compliance report."

#### 11.1 Bug Fixes

- [ ] **B1**: Fix `DriverInfo::get_properties()` key mismatch — collect additional `SQLGetInfo` calls for `SQL_ODBC_VER`, `SQL_DATABASE_NAME`, `SQL_SERVER_NAME`, `SQL_USER_NAME`, `SQL_CATALOG_TERM`, `SQL_SCHEMA_TERM`, `SQL_TABLE_TERM`, `SQL_PROCEDURE_TERM`, `SQL_IDENTIFIER_QUOTE_CHAR` and store them so `get_properties()` can find them
- [ ] **B2**: Add discovery data to `JsonReporter` — add `driver_info`, `type_info`, and `function_info` sections to the JSON output
- [ ] **B3**: Remove false `SQLBrowseConnect`/`SQLSetPos`/`SQLBulkOperations` from mock driver's supported function list
- [ ] **B4**: Remove debug file I/O from mock driver
- [ ] **B5**: Fix `OdbcStatement::close_cursor()` to properly check return codes
- [ ] **B6**: Remove dead CLI stub files or implement them
- [ ] **B7**: Extract result-tallying into a helper function in `main.cpp`

#### 11.2 Report Overhaul

- [ ] Add ODBC conformance level to each test result: `Core`, `Level 1`, `Level 2`
- [ ] Add ODBC spec reference to each test (e.g., "Per ODBC 3.x, SQLGetInfo")
- [ ] Split SKIP into `SKIP_UNSUPPORTED` (driver doesn't support optional feature) and `SKIP_INCONCLUSIVE` (test couldn't determine)
- [ ] Add severity-ranked failure summary (CRITICAL → ERROR → WARNING)
- [ ] Add function coverage matrix to report: which functions were called, result, and whether the driver claims support
- [ ] Add discovery data (driver info, types, function support) to JSON reporter
- [ ] Improve all suggestion text with actionable guidance citing spec sections
- [ ] Add `--conformance-level` CLI flag to filter tests by Core/Level 1/Level 2

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

### Phase 12: Implement Stub Tests & Expand Coverage ⬜

**Goal**: Implement the 6 stub tests and add tests for the 15+ ODBC functions the mock driver supports but are never exercised.

#### 12.1 Implement Existing Stubs

- [ ] `test_multiple_errors`: Use mock driver `ErrorCount=3` to generate multiple diagnostics, iterate with `SQLGetDiagRec`
- [ ] `test_error_clearing`: Trigger error, then execute successful operation, verify diagnostics cleared
- [ ] `test_field_extraction`: Use `SQLGetDiagField` to extract SQLSTATE, native error, message text, row number, column number
- [ ] `test_invalid_operation`: Call `SQLExecute` without `SQLPrepare`, verify HY010 (Function sequence error)
- [ ] `test_state_reset`: Execute query, `SQLCloseCursor`, verify statement is reusable
- [ ] `test_prepare_execute_cycle`: Prepare → Execute → Close → Execute → Close cycle

#### 12.2 New Test Categories

**Descriptor Tests** (new category):
- [ ] Get implicit APD/ARD/IPD/IRD handles via `SQLGetStmtAttr`
- [ ] Read IRD fields after `SQLPrepare` (column metadata via descriptors)
- [ ] Set APD fields for parameter binding
- [ ] `SQLCopyDesc` between statement handles
- [ ] Verify descriptor auto-population after `SQLExecDirect`

**Column Binding Tests** (extend Statement Tests):
- [ ] `SQLBindCol` for integer, string, date columns
- [ ] Fetch with bound columns vs `SQLGetData`
- [ ] `SQLFreeStmt(SQL_UNBIND)` to reset bindings
- [ ] Bind + fetch multiple rows

**Scrollable Cursor Tests** (extend Advanced Tests):
- [ ] `SQLFetchScroll(SQL_FETCH_NEXT)`
- [ ] `SQLFetchScroll(SQL_FETCH_FIRST)` / `SQL_FETCH_LAST`
- [ ] `SQLFetchScroll(SQL_FETCH_ABSOLUTE, n)`
- [ ] Set `SQL_ATTR_CURSOR_SCROLLABLE` and verify

**Row Count & Parameter Tests** (extend Statement Tests):
- [ ] `SQLRowCount` after INSERT/UPDATE/DELETE
- [ ] `SQLNumParams` after prepare
- [ ] `SQLDescribeParam` for bound parameters
- [ ] `SQLNativeSql` translation

**Cancellation Tests** (new):
- [ ] `SQLCancel` on an idle statement (should succeed)
- [ ] `SQLCancel` as state reset

#### 12.3 Mock Driver Updates

- [ ] Remove false support claims (`SQLBrowseConnect`, `SQLSetPos`, `SQLBulkOperations`)
- [ ] Remove debug file I/O
- [ ] Verify mock driver returns HY010 for function sequence errors
- [ ] Verify mock driver returns 01004 for string truncation
- [ ] Add `SQLNativeSql` implementation (pass-through)
- [ ] Verify `SQLCancel` implementation is correct

**Deliverables**:
- 0 stub tests remaining
- 15+ new tests across new sub-categories
- Mock driver honesty: only claims what it implements
- ~70+ total tests, all exercisable against mock driver

---

### Phase 13: Negative Testing & Spec Compliance ⬜

**Goal**: Test that the driver *rejects* invalid operations correctly, per ODBC spec.

ODBC drivers are required to return specific SQLSTATEs for specific error conditions. This phase verifies the driver's error behavior.

#### 13.1 SQLSTATE Validation Tests

| Test | Expected SQLSTATE | Spec Reference |
|------|-------------------|----------------|
| `SQLExecute` without `SQLPrepare` | HY010 | Function sequence error |
| `SQLFetch` without active cursor | 24000 | Invalid cursor state |
| `SQLGetData` for column 0 (bookmarks disabled) | 07009 | Invalid descriptor index |
| `SQLGetData` for column > num_cols | 07009 | Invalid descriptor index |
| `SQLDriverConnect` with invalid DSN | IM002 | Data source not found |
| `SQLExecDirect` with syntax error | 42000 | Syntax error or access violation |
| `SQLBindParameter` with invalid C type | HY003 | Invalid application buffer type |
| `SQLGetInfo` with invalid info type | HY096 | Information type out of range |
| `SQLSetConnectAttr` with invalid attribute | HY092 | Invalid attribute/option |
| `SQLCloseCursor` with no open cursor | 24000 | Invalid cursor state |

#### 13.2 Boundary Value Tests

- [ ] `SQLGetInfo` with buffer size = 0 (should return required length)
- [ ] `SQLGetData` with buffer size = 0 (should return data length)
- [ ] `SQLBindParameter` with NULL value pointer and `SQL_NULL_DATA` indicator
- [ ] `SQLExecDirect` with empty SQL string
- [ ] `SQLDescribeCol` with column number = 0

#### 13.3 Data Type Edge Cases

- [ ] INTEGER: `INT_MIN`, `INT_MAX`, 0
- [ ] VARCHAR: empty string, max-length string, string with special characters
- [ ] DATE: epoch (1970-01-01), far future, leap year dates
- [ ] DECIMAL: maximum precision, edge precision/scale
- [ ] NULL indicators for every supported C type
- [ ] Type conversion: retrieve INTEGER as `SQL_C_CHAR`, CHAR as `SQL_C_SLONG`

**Deliverables**:
- 10+ SQLSTATE validation tests
- 5+ boundary value tests
- 10+ data type edge case tests
- Mock driver returns spec-correct SQLSTATEs for all negative tests

---

### Phase 14: Polish & Documentation ⬜

**Goal**: Production-ready tool with comprehensive documentation.

- [ ] `--help` output covers all options with examples
- [ ] README.md with quick-start, installation, usage, output interpretation
- [ ] Example output files (console and JSON) for reference
- [ ] Contributing guide
- [ ] Release build configuration
- [ ] Version tagging

---

## 5. Technical Stack

| Component | Choice | Rationale |
|-----------|--------|-----------|
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
