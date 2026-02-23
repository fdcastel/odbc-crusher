# ODBC Crusher — Project Plan

**Version**: 3.0  
**Purpose**: A command-line tool for ODBC driver developers to validate driver correctness, discover capabilities, and identify spec violations.  
**Last Updated**: February 22, 2026

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
| **Test suites** | `src/tests/` | 12 test categories, each a `TestBase` subclass |
| **Reporting** | `src/reporting/` | `ConsoleReporter`, `JsonReporter` |
| **Mock driver** | `mock-driver/` | Standalone ODBC DLL for CI/offline testing |
| **Unit tests** | `tests/` | GTest suite for regression testing |

### Data Flow

1. Parse CLI → create reporter → connect to data source
2. **Phase 1 (Discovery)**: Collect driver info, type info, function support, scalar function bitmasks
3. **Phase 2 (Testing)**: Run 12 test categories, collect `TestResult` vectors
4. **Phase 3 (Reporting)**: Format everything for the target audience

---

## 3. Current State

### What's Implemented

- **131 tests across 12 categories** — all pass against the mock driver (100%)
- **Mock driver implements 60+ ODBC functions** with Unicode-first architecture, configurable behavior, escape sequence support, SQL_NUMERIC_STRUCT, scrollable cursors, array parameters, DDL, and data persistence
- **Console reporter** with conformance levels, spec references, severity-ranked summaries, and scalar function matrix
- **JSON reporter** with full discovery data, structured test results, and scalar function data
- **60/60 GTest unit tests** pass on all platforms (Windows, Linux, macOS)
- **CI/CD**: GitHub Actions builds and tests on all 3 platforms; release workflow creates artifacts
- **Real-driver validated** against Firebird, MariaDB, PostgreSQL, and DuckDB ODBC drivers

### Real-Driver Results (v0.4.5)

| Driver | Version | Pass Rate | Notes |
|--------|---------|-----------|-------|
| Mock ODBC | — | 131/131 (100%) | Reference implementation |
| PostgreSQL (psqlodbc) | v16.00.0000 | 129/131 (98.5%) | 2 minor driver gaps |
| Firebird | v03.00.0021 | 116/127 (91.3%) | Remaining are genuine driver issues |
| MariaDB | v03.01.0015 | 115/131 (87.8%) | Needs DDL permissions for full coverage |
| DuckDB | v1.4.4.0 | 108/123 (87.8%) | 2 crash-severity driver bugs worked around |

### `TestResult` Structure

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

---

## 4. Test Categories (131 tests)

| Category | Tests | Key ODBC Functions | Conformance |
|----------|:-----:|----------------|-------------|
| Connection | 6 | `SQLGetInfo`, `SQLGetConnectAttr`, `SQLSetConnectAttr` | Core |
| Statement | 7 | `SQLExecDirect`, `SQLPrepare`, `SQLExecute`, `SQLFetch`, `SQLBindParameter`, `SQLGetData` | Core |
| Metadata/Catalog | 7 | `SQLTables`, `SQLColumns`, `SQLPrimaryKeys`, `SQLForeignKeys`, `SQLStatistics`, `SQLSpecialColumns` | Core + L1 |
| Data Types | 9 | `SQLGetData` (7 C types), `SQLExecDirect` | Core |
| Transactions | 5 | `SQLEndTran`, `SQLSetConnectAttr` | Core |
| Advanced | 6 | `SQLSetStmtAttr`, `SQLGetStmtAttr` | L1–L2 |
| Buffer Validation | 5 | `SQLGetInfo` (buffer edge cases) | Core |
| Error Queue | 6 | `SQLGetDiagRec`, `SQLGetDiagField` | Core |
| State Machine | 6 | `SQLAllocHandle`, `SQLGetConnectAttr` | Core |
| Escape Sequences | 15 | `SQLNativeSql`, `SQLExecDirect`, `SQLGetInfo` | Core + L1–L2 |
| Numeric Struct | 4 | `SQLGetData(SQL_C_NUMERIC)`, `SQLSetDescField` | Core |
| Cursor Stress | 2 | `SQLExecDirect`, `SQLFetch`, `SQLCloseCursor` | Core |

### Quality Criteria for Tests

Every test must:
1. Name the ODBC function it exercises
2. State what it expects (per the ODBC specification)
3. Report what actually happened
4. Classify its conformance level (Core / Level 1 / Level 2)
5. On failure: provide an actionable suggestion with spec reference
6. On SKIP: clearly indicate whether the feature is optional or the test was inconclusive

---

## 5. Mock ODBC Driver

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

## 6. Technical Stack

| Component | Technology | Notes |
|-----------|-----------|-------|
| Language | C++17 | Direct ODBC API access, `std::optional`, `std::string_view` |
| Build | CMake 3.20+ | CTest integration, FetchContent for deps |
| Test framework | Google Test 1.14 | GTest + GMock |
| CLI parsing | CLI11 2.4.2 | Header-only |
| JSON | nlohmann/json 3.11.3 | Header-only |
| ODBC | System | odbc32.lib (Windows) / unixODBC (Linux/macOS) |

---

## 7. Future Work

Potential areas for future development (not yet scheduled):

- **More escape sequence coverage**: ODBC `{INTERVAL ...}` escapes, additional scalar function categories
- **Performance benchmarking**: Measure and compare fetch throughput, parameter binding overhead across drivers
- **Result set comparison mode**: Run the same test against two drivers and diff the results
- **HTML reporter**: Visual report with expandable sections, color-coded severity
- **Driver Manager bypass mode**: Call driver functions directly (skip DM) for isolation testing
- **Extended catalog testing**: `SQLProcedures`, `SQLProcedureColumns`, `SQLGetTypeInfo` deep validation
- **SQLBulkOperations / SQLSetPos testing**: When mock driver supports positioned operations
- **Connection pooling tests**: Validate driver behavior under DM connection pooling
- **Empty database handling**: `test_columns_catalog` fails on fresh databases (e.g., DuckDB) — needs temp table creation or `VIEW` type fallback

---

## 8. Lessons Learned

### Core ODBC Wisdom

1. **Mock driver must never claim false support.** If `SQLGetFunctions` says a function is supported, it must be implemented. False claims mislead both the test suite and callers.

2. **"Zero results" ≠ "unsupported function."** A catalog function like `SQLForeignKeys` returning `SQL_SUCCESS` with 0 rows means the function works but no foreign keys exist. Tests must distinguish "callable" from "has data."

3. **ODBC escape sequences are the ultimate RDBMS-independence test.** `{fn ...}`, `{d ...}`, `{ts ...}`, `{call ...}`, and `{oj ...}` escapes are the purest way to test drivers without RDBMS-specific code. The driver translates escapes to native SQL — testing this validates the driver's core ODBC contract. `SQLNativeSql` verifies translation without execution. `SQLGetInfo` bitmasks (`SQL_STRING_FUNCTIONS`, `SQL_NUMERIC_FUNCTIONS`, etc.) reveal claimed support. Comparing claims with actual execution catches "claims but doesn't implement" bugs.

4. **Query `SQLGetInfo` capability bitmasks before testing scalar functions.** Only exercise functions the driver claims via the bitmask and report "not claimed" for unsupported ones rather than failing. The `SQL_CONVERT_*` family provides a complete type conversion matrix.

5. **Prefer `CAST(? AS type)` over bare `?` in parameter queries.** Some drivers (DuckDB) cannot infer parameter types from bare `SELECT ?` and may crash on `SQLDescribeParam`. `SELECT CAST(? AS INTEGER)` is more portable.

### Cross-Driver SQL Compatibility

6. **Hardcoded table names break real-driver testing.** Tests referencing mock-driver tables fail silently against real databases. Tests that need writable tables should `CREATE TABLE` their own; read-only tests should discover tables via `SQLTables` or use literal queries.

7. **Fallback query chains must cover all major dialects.** Every query chain should include: standard SQL (`SELECT expr`), Firebird (`SELECT expr FROM RDB$DATABASE`), Oracle (`SELECT expr FROM DUAL`), and generic variants.

8. **Reserved words vary across SQL dialects.** `VALUE`, `KEY`, `INDEX`, `TYPE`, `ORDER`, `USER`, `NAME`, `TABLE`, `DATE`, `TIME` are reserved in various databases. Use non-reserved column names in test DDL.

9. **MySQL/MariaDB uses databases as catalogs, not schemas.** The MariaDB driver rejects non-empty schema parameters with `HYC00`. When calling `SQLColumns` after `SQLTables`, always propagate the catalog column (column 1) from the result set.

### Driver Manager Behavior

10. **The Windows DM intercepts certain `SQLGetInfo` types.** `SQL_DRIVER_NAME` is handled by the DM itself. The DM's truncation behavior may differ from the driver's. Use driver-handled info types like `SQL_DBMS_NAME` for truncation tests.

11. **Sentinel / buffer overwrite tests may detect DM behavior, not driver behavior.** The DM's ANSI↔Wide conversion may modify buffer bytes beyond the null terminator. To isolate driver behavior, call W-functions directly.

12. **The Windows DM intercepts `SQLGetData(SQL_C_NUMERIC)` and converts via `SQL_C_CHAR`.** Applications MUST call `SQLSetDescField` on the ARD to set `SQL_DESC_TYPE`, `SQL_DESC_PRECISION`, and `SQL_DESC_SCALE` before `SQLGetData(SQL_C_NUMERIC)`. Without this, the DM produces garbage values.

13. **W-function exports determine Unicode driver classification.** If a driver exports `SQLExecDirectW`, the DM treats it as a Unicode driver and calls W-functions directly. If the driver's W→A conversion has bugs, W-calls fail while A-calls succeed. Tests should fall back to ANSI when W-functions fail.

14. **`SQLGetData` with NULL buffer may not work through the DM.** Some DMs reject `SQLGetData(NULL, 0)`. Fall back to a 1-byte buffer to trigger truncation and get data length from the indicator.

### Driver-Specific Pitfalls

15. **DDL errors invalidate Firebird transactions.** A failed `DROP TABLE` with autocommit off leaves the transaction broken. Issue `SQLEndTran(SQL_ROLLBACK)` after DDL failure. Safest approach: CREATE first, DROP+retry only if CREATE fails with "table exists."

16. **DDL requires autocommit ON in Firebird.** With autocommit OFF, failed DDL invalidates the entire transaction. Use separate statement objects for each DDL operation.

17. **DDL failures due to missing permissions produce misleading skip messages.** Capture and report the actual SQLSTATE and error message from failed DDL to help users diagnose permission issues.

### Crash Safety

18. **Row-wise array binding must be probed before execution.** Some drivers (DuckDB) store `SQL_ATTR_PARAM_BIND_TYPE` but never use it for pointer arithmetic. Probe with integer-only parameters at `PARAMSET_SIZE=2` before attempting string parameters that could cause memory corruption.

19. **Windows `/GS` stack cookie failures are uncatchable.** When a driver corrupts the stack, MSVC's `/GS` calls `__fastfail()` raising `STATUS_STACK_BUFFER_OVERRUN` (`0xC0000409`). No user-mode exception handler can catch it — SEH, VEH, `SetUnhandledExceptionFilter`, and `catch(...)` all fail. The only defense is avoiding the corruption.

20. **`SQLSetStmtAttr` default cases that return `SQL_SUCCESS` are dangerous.** Some drivers return success for ALL unrecognized attributes. Read-back via `SQLGetStmtAttr` is insufficient — the driver may store the value without implementing the behavior.

### Development Process

21. **Always validate against real drivers with source access.** Running against Firebird revealed that 17 of 27 non-passing results were bugs in odbc-crusher, not the driver. Mock-only testing is insufficient.

22. **The mock driver is not enough.** Even with 131/131 tests passing against the mock, 17 tests failed against a real driver due to assumptions baked into the mock schema. Real-world testing is essential.

23. **`dynamic_cast` across DLL boundaries doesn't work on Windows.** Solved with opaque handle + magic number validation in the mock driver.

24. **Reports must serve the audience.** "PASS" and "FAIL" are meaningless without context. Driver developers need: which spec clause, what conformance level, and exactly what to fix.

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
| Scalar Functions (Appendix E) | https://learn.microsoft.com/en-us/sql/odbc/reference/appendixes/appendix-e-scalar-functions |
| Escape Sequences | https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/escape-sequences-in-odbc |
| SQLNativeSql Function | https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlnativesql-function |
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

# Run against mock driver
./build/src/Release/odbc-crusher.exe "Driver={Mock ODBC Driver};Mode=Success;" --verbose

# Run against a real driver
./build/src/Release/odbc-crusher.exe "Driver={Your Driver};Server=...;Database=...;" --verbose

# JSON output
./build/src/Release/odbc-crusher.exe "Driver={Mock ODBC Driver};Mode=Success;" -o json -f report.json
```
