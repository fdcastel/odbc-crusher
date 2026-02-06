# ODBC Crusher

[![CI](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml)
[![Release](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml)

A command-line tool that tests ODBC drivers for correctness and spec compliance.

Point it at any ODBC connection string and it will run **120+ tests** covering connections, statements, metadata, data types, transactions, Unicode handling, catalog functions, diagnostics, cursor behavior, parameter binding, error handling, buffer validation, and state machine compliance — then report what passed, failed, or was skipped.

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
