# ODBC Crusher

[![CI](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml)
[![Release](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml)

A command-line tool that tests ODBC drivers for correctness and spec compliance.

Point it at any ODBC connection string and it will run **90+ tests** covering connections, statements, metadata, data types, transactions, error handling, buffer validation, and state machine compliance — then report what passed, failed, or was skipped.

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
| Connection | 5 | Connect, attributes, multiple statements, timeout |
| Statement | 7 | ExecDirect, Prepare/Execute, parameters, column metadata |
| Metadata | 5 | Tables, columns, primary keys, statistics, special columns |
| Data Types | 6 | Integer, decimal, float, string, date/time, NULL handling |
| Transactions | 4 | Autocommit, commit/rollback, isolation levels |
| Advanced | 6 | Cursor types, bulk ops, async, rowsets, concurrency |
| Buffer Validation | 5 | Null termination, overflow protection, truncation indicators |
| Error Queue | 6 | Diagnostic records, clearing, hierarchy, field extraction |
| State Machine | 6 | Valid transitions, invalid operations, state reset |
| Descriptors | 5 | Implicit handles, IRD, APD fields, CopyDesc |
| Cancellation | 2 | Cancel idle, cancel as reset |
| SQLSTATE Validation | 10 | HY010, 24000, 07009, 42000, HY003, HY096, HY092 |
| Boundary Values | 5 | Zero buffers, NULL parameters, empty SQL, column 0 |
| Data Type Edge Cases | 10 | INT_MIN/MAX, empty strings, NULL indicators, type conversion |

Every test reports `PASS`, `FAIL`, `SKIP` (unsupported), or `ERROR`, with ODBC spec references and fix suggestions where applicable.

## Example Output

```
╔════════════════════════════════════════════════════════════════╗
║           ODBC CRUSHER - Driver Testing Report                ║
╚════════════════════════════════════════════════════════════════╝

Connection: Driver={MySQL ODBC 9.2 Unicode Driver};...

────────────────────────────────────────────────────────────────
  Connection Tests
────────────────────────────────────────────────────────────────
  ✓ test_connection_info (992 μs)
  ✓ test_connection_string_format (3 μs)
  ✓ test_multiple_statements (48 μs)
  ✓ test_connection_attributes (25 μs)
  ✓ test_connection_timeout (98 μs)

  Category Summary: 5 passed
  ...

════════════════════════════════════════════════════════════════
                        FINAL SUMMARY
════════════════════════════════════════════════════════════════

  Total Tests:  82
  Passed:       78 (95.1%)
  Skipped:       4
  Total Time:   142.5 ms

  ✓ ALL TESTS PASSED!
════════════════════════════════════════════════════════════════
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

- **PASS** — The driver behaves correctly for this test.
- **FAIL** — The driver returned an unexpected result. Check the `actual` field and the ODBC spec reference for details.
- **SKIP** — The test was skipped because the driver does not support the feature, or the result was inconclusive.
- **ERROR** — An unexpected exception occurred during the test.

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
