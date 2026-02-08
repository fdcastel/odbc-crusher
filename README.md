# ODBC Crusher

[![CI](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml)
[![Release](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml)

A command-line tool that tests ODBC drivers for correctness and spec compliance.

Point it at any ODBC connection string and it will run **131 tests** covering connections, statements, metadata, data types, transactions, Unicode handling, catalog functions, diagnostics, cursor behavior, parameter binding, error handling, buffer validation, and state machine compliance — then report what passed, failed, or was skipped.

## Quick Start

Download a binary from the [latest release](https://github.com/fdcastel/odbc-crusher/releases/latest), or [build from source](#build-from-source).

```bash
# Basic usage
odbc-crusher "Driver={MySQL ODBC 9.2 Unicode Driver};Server=localhost;Database=test;UID=root;PWD=secret"

# Use a DSN
odbc-crusher "DSN=MyFirebird"

# Verbose mode — shows diagnostics and suggestions for each test
odbc-crusher "Driver={...}" -v

# JSON output to file
odbc-crusher "Driver={...}" -o json -f report.json

# JSON output to stdout (pipe to jq, etc.)
odbc-crusher "Driver={...}" -o json | jq '.summary'
```

## What It Tests

| Category | Tests | What's Checked |
|----------|-------|----------------|
| Connection | 6 | Connect, attributes, multiple statements, timeout, pooling |
| Statement | 15 | ExecDirect, Prepare/Execute, parameters, column metadata, row count |
| Metadata | 7 | Tables, columns, primary keys, statistics, special columns, privileges |
| Data Types | 9 | Integer, decimal, float, string, date/time, NULL, Unicode, binary, GUID |
| Transactions | 5 | Autocommit, commit/rollback, isolation levels |
| Advanced | 10 | Cursor types, bulk ops, async, rowsets, concurrency, bookmarks |
| Buffer Validation | 5 | Null termination, overflow protection, truncation indicators |
| Error Queue | 6 | Diagnostic records, clearing, hierarchy, field extraction |
| State Machine | 6 | Valid transitions, invalid operations, state reset |
| Descriptors | 2 | Implicit handles, IRD fields |
| Cancellation | 2 | Cancel idle, cancel as reset |
| SQLSTATE Validation | 10 | HY010, 24000, 07009, 42000, HY003, HY096, HY092, HYC00 |
| Boundary Values | 5 | Zero buffers, NULL parameters, empty SQL, column 0 |
| Data Type Edge Cases | 5 | INT_MIN/MAX, empty strings, NULL indicators, type conversion |
| **Unicode Tests** | 5 | SQLGetInfoW, SQLDescribeColW, SQL_C_WCHAR, Unicode patterns, truncation |
| **Catalog Depth** | 6 | Search patterns, result set shape, statistics, procedures, privileges, NULL params |
| **Diagnostic Depth** | 4 | SQL_DIAG_SQLSTATE, SQL_DIAG_NUMBER, SQL_DIAG_ROW_COUNT, multiple records |
| **Cursor Behavior** | 4 | Forward-only fetch, scrolling restrictions, cursor attributes, SQLGetData |
| **Parameter Binding** | 3 | SQL_C_WCHAR input, NULL indicators, rebind/re-execute |

Every test reports `PASS`, `FAIL`, `SKIP` (unsupported), or `ERROR`, with ODBC spec references and fix suggestions where applicable.

## Example Output

```
ODBC Crusher v0.2.0 - Driver analysis report

DRIVER:
  Driver Name:          myodbc9w.dll
  Driver Version:       09.06.0000
  Driver ODBC Version:  03.80
  ODBC Version (DM):    03.80.0000

DATABASE:
  DBMS Name:            MySQL
  DBMS Version:         8.0.35
  Database:             test
  Server:               localhost
  User:                 root
  SQL Conformance:      SQL-92 Intermediate

ODBC FUNCTIONS:
  51/52 ODBC functions supported (as reported by SQLGetFunctions)

  MISSING functions:
    SQLSetDescRec

Connection Tests:                                                      6 passed
  [PASS] test_connection_info [Core] (1.05 ms)
  [PASS] test_connection_string_format [Core] (3 us)
  [PASS] test_multiple_statements [Core] (30 us)
  [PASS] test_connection_attributes [Core] (30 us)
  [PASS] test_connection_timeout [Core] (14 us)
  [PASS] test_connection_pooling [Core] (4 us)

Statement Tests:                                 2 passed, 2 failed, 11 skipped
  [PASS] test_simple_query [Core] (4.66 ms)
  [PASS] test_prepared_statement [Core] (2.48 ms)
  [ ?? ] test_parameter_binding [Core] (1.73 ms)
  ...

SUMMARY:
  Total Tests:  123
  Passed:       92 (74.8%)
  Failed:       2
  Skipped:      29
  Total Time:   204.58 ms

FAILURES BY SEVERITY:

  [WARNING] test_setconnattr_invalid_attr (SQLSetConnectAttr)
    SQLSetConnectAttr accepted invalid attribute 99999
    Fix: Driver should return HY092 for unrecognized attributes

  [FAIL] SOME TESTS FAILED
```

## Build From Source

### Prerequisites

- **CMake** 3.20+
- **C++17 compiler** (MSVC 2019+, GCC 9+, Clang 10+)
- **ODBC Driver Manager** — Windows has it built-in; on Linux install `unixodbc-dev`; on macOS use `brew install unixodbc`

All other dependencies (Google Test, CLI11, nlohmann/json) are fetched automatically by CMake.

### Build

```bash
# Windows (MSVC)
cmake -B build
cmake --build build --config Release

# Linux / macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is at `build/src/Release/odbc-crusher.exe` (Windows) or `build/src/odbc-crusher` (Linux/macOS).

### Run Unit Tests

```bash
ctest --test-dir build -C Release --output-on-failure
```

### Build and Run with Mock ODBC Driver

The project includes a **Mock ODBC Driver** (in `mock-driver/`) for testing without requiring a real database. This is useful for CI/CD pipelines and development.

#### 1. Build the Mock Driver

```bash
# Windows
cd mock-driver
cmake -B build
cmake --build build --config Release
cd ..

# Linux / macOS
cd mock-driver
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd ..
```

The compiled driver is at:
- Windows: `mock-driver/build/src/Release/mockodbc.dll`
- Linux: `mock-driver/build/src/libmockodbc.so`
- macOS: `mock-driver/build/src/libmockodbc.dylib`

#### 2. Register the Driver

**Windows**:
```powershell
# Use ODBC Administrator (odbcad32.exe) or set registry manually:
# HKEY_LOCAL_MACHINE\SOFTWARE\ODBC\ODBCINST.INI\Mock ODBC Driver
# Driver = C:\path\to\mockodbc.dll
```

**Linux/macOS**:
```bash
# Add to /etc/odbcinst.ini or ~/.odbc.ini:
# [Mock ODBC Driver]
# Description = Mock ODBC Driver for Testing
# Driver = /path/to/libmockodbc.so
```

#### 3. Run ODBC Crusher with the Mock Driver

```bash
# Windows
odbc-crusher "Driver={Mock ODBC Driver};Mode=Success;"

# Linux / macOS
odbc-crusher "Driver={Mock ODBC Driver};Mode=Success;"
```

#### 4. Configuration Options

The mock driver supports these connection string parameters:

| Parameter | Values | Description |
|-----------|--------|-------------|
| `Mode` | Success, Failure, Random | Overall behavior (Success = normal operation, Failure = all operations fail) |
| `Catalog` | Default, Empty, Large | Mock schema preset (Default has USERS/ORDERS/PRODUCTS tables) |
| `ResultSetSize` | Number | Number of rows to return in result sets (default: 100) |
| `FailOn` | Function names | Comma-separated list of functions to fail (e.g., `FailOn=SQLExecute,SQLFetch`) |
| `ErrorCode` | SQLSTATE | Error code to return for failures (default: HY000) |

#### 5. Example Usage

```bash
# Test basic functionality
odbc-crusher "Driver={Mock ODBC Driver};Mode=Success;" -v

# Test with larger result sets
odbc-crusher "Driver={Mock ODBC Driver};ResultSetSize=1000;" -o json -f report.json

# Test error handling (all operations fail)
odbc-crusher "Driver={Mock ODBC Driver};Mode=Failure;" -v

# Test specific function failures
odbc-crusher "Driver={Mock ODBC Driver};FailOn=SQLPrepare,SQLExecute;ErrorCode=42000;" -v

# Use JSON output for CI/CD
odbc-crusher "Driver={Mock ODBC Driver};Mode=Success;" -o json | jq '.summary'
```

#### 6. What the Mock Driver Supports

- **All core ODBC 3.x functions** (SQLConnect, SQLPrepare, SQLExecute, etc.)
- **Catalog functions** (SQLTables, SQLColumns, SQLStatistics, SQLForeignKeys, etc.)
- **Data types** (Integer, String, Decimal, Date/Time, NULL, GUID, Binary)
- **Transactions** (Autocommit, commit/rollback with data rollback, isolation levels)
- **Metadata** (Type information, function support, driver attributes)
- **Error diagnostics** (SQLGetDiagRec with SQLSTATE codes)
- **Unicode** (W-variant entry points: SQLConnectW, SQLColumnsW, etc.)
- **Literal SELECT queries** (SELECT 1, SELECT 'hello', SELECT CAST(...), etc.)
- **CREATE/DROP TABLE** (dynamic schema modification)
- **INSERT with data persistence** (in-memory row storage)
- **Scrollable cursors** (static cursors with SQL_FETCH_FIRST/LAST/PRIOR/ABSOLUTE/RELATIVE)
- **Parameter binding** (SQLBindParameter with value substitution in literal SELECTs)

The mock driver achieves **100% pass rate** (131/131 tests) when run with `Mode=Success`.

## CLI Reference

```
Usage: odbc-crusher [OPTIONS] connection

Positionals:
  connection TEXT REQUIRED    ODBC connection string (Driver={...};... or DSN=...)

Options:
  -h,--help                   Print help and exit
  -V,--version                Print version and exit
  -v,--verbose                Show detailed diagnostics and suggestions
  -o,--output TEXT            Output format: 'console' (default) or 'json'
  -f,--file TEXT              Write JSON output to FILE instead of stdout
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | One or more tests failed |
| 2 | ODBC connection error |
| 3 | Other error |

## JSON Output

With `-o json`, the output is a machine-readable JSON document suitable for CI pipelines:

```bash
odbc-crusher "Driver={...}" -o json -f report.json
```

The JSON includes driver information, type support, function support, all test results with status/duration/diagnostics, and a summary object.

## Interpreting Results

- **[PASS]** — The driver behaves correctly for this test.
- **[FAIL]** — The driver returned an unexpected result. Check the `actual` field and the ODBC spec reference for details.
- **[NOT ]** — The test was skipped because the driver does not support this feature (SKIP_UNSUPPORTED).
- **[ ?? ]** — The test result was inconclusive (SKIP_INCONCLUSIVE) — the driver may or may not support the feature.
- **[ERR!]** — An unexpected exception occurred during the test.

In verbose mode (`-v`), each test also shows:
- The **expected** vs **actual** behavior
- A **suggestion** for fixing failures
- The relevant **ODBC spec section**

## Tested Databases

- **Firebird 5.0** via Firebird ODBC Driver
- **MySQL 8.0+** via MySQL Connector/ODBC 9.x
- **Mock ODBC Driver** (included in `mock-driver/`, used for CI)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT
