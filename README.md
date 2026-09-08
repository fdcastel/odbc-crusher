# ODBC Crusher

[![CI](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml)
[![Release](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml)

A command-line tool that tests ODBC drivers for correctness and spec compliance.

Point it at any ODBC connection string and it will run **205 checks** covering connections, statements, metadata, data types, transactions, Unicode handling (including non-ASCII round-trip), catalog functions, diagnostics, cursor behavior, parameter binding (including `{?=CALL …}` IN/OUT/INOUT), error handling, buffer validation, NUMERIC byte-equality, escape sequence translation, and state machine compliance — then report what passed, failed, or was skipped.

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

# Run one category instead of all 23 — useful when bisecting a hang
odbc-crusher --list-categories
odbc-crusher "Driver={...}" -c "Cursor Behavior Tests"
```

### Exit codes

`0` when nothing failed, `1` when a probe failed, `2` on an ODBC error
(a driver that will not connect), `3` on a usage error.

By default *any* FAIL or ERROR makes the exit code `1`, which against a
real driver means it is almost always `1`. To gate your own CI on
something narrower, give `--fail-on` the least severity you care about:

```bash
# Fail the build only on CRITICAL findings; report everything else.
odbc-crusher "Driver={...}" --fail-on=critical

# Never fail on probe results — for a scheduled run that only collects.
odbc-crusher "Driver={...}" --fail-on=none -o json -f report.json
```

`--fail-on` changes the exit code and nothing else: every probe still
runs and the report is unchanged. A connection that fails still exits
non-zero whatever the threshold — there were no results to grade.

## What It Tests

| Category | Tests | What's Checked |
|----------|------:|----------------|
| Connection | 6 | Connect, attributes, multiple statements, timeout, pooling |
| Statement | 15 | ExecDirect, Prepare/Execute, parameters, column metadata, row count |
| Metadata/Catalog | 11 | Tables, columns, primary keys, statistics, special columns, privileges, **`SQLProcedures` / `SQLProcedureColumns` discovery** |
| Data Types | 9 | Integer, decimal, float, string, date/time, NULL, Unicode, binary, GUID |
| Transactions | 6 | Autocommit, commit/rollback, isolation levels, **rollback × open cursor cross-state** |
| Advanced | 10 | Cursor types, bulk ops, async, rowsets, concurrency, bookmarks |
| Buffer Validation | 5 | Null termination, overflow protection, truncation indicators |
| Error Queue | 6 | Diagnostic records, clearing, hierarchy, field extraction |
| State Machine | 6 | Valid transitions, invalid operations, state reset |
| Descriptors | 5 | Implicit handles, IRD fields, descriptor field access |
| Cancellation | 2 | Cancel idle, cancel as reset |
| SQLSTATE Validation | 10 | HY010, 24000, 07009, 42000, HY003, HY096, HY092, HYC00 |
| Boundary Values | 5 | Zero buffers, NULL parameters, empty SQL, column 0 |
| Data Type Edge Cases | 14 | INT_MIN/MAX, empty strings, NULL indicators, type conversion, raw-byte integrity, **NULL-vs-empty / NULL-vs-zero contrast** |
| Unicode Tests | 7 | `SQLGetInfoW`, `SQLDescribeColW`, `SQL_C_WCHAR`, Unicode patterns, truncation, **non-ASCII round-trip + surrogate-pair preservation** |
| Catalog Function Depth | 6 | Search patterns, result-set shape, statistics, procedures, privileges, NULL params |
| Diagnostic Depth | 4 | `SQL_DIAG_SQLSTATE`, `SQL_DIAG_NUMBER`, `SQL_DIAG_ROW_COUNT`, multiple records |
| Cursor Behavior | 4 | Forward-only fetch, scrolling restrictions, cursor attributes, `SQLGetData` |
| Parameter Binding | 24 | `SQL_C_WCHAR` input, NULL indicators, rebind/re-execute, integer/float→VARCHAR round-trips, `SQLRowCount` after DML, `SQLDescribeParam`, batch execute |
| Array Parameters | 10 | Column-wise / row-wise binding, `SQL_PARAM_STATUS_PTR`, `SQL_PARAMS_PROCESSED_PTR`, NULL values, operation array, **driver-detected per-row failure**, **`SQL_ATTR_PARAMSET_SIZE` HYC00 fallback** |
| Escape Sequences | 20 | `{fn …}`, `{d …}`, `{t …}`, `{ts …}`, `{oj …}` translation, scalar-function execution, **claim-vs-execute matrix**, **`{?=CALL …}` IN/OUT/INOUT directions** |
| Numeric Struct | 6 | `SQL_C_NUMERIC` binding/retrieval, precision/scale, sign byte, **byte-for-byte mantissa equality**, **DECIMAL sum-loop precision** |
| Cursor Stress | 4 | Rapid lifecycle, concurrent statements, **phase-separated open/close timing**, **handle-reuse leak detection** |

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
  Total Tests:  205
  Passed:       142 (72.8%)
  Failed:       16
  Skipped:      37
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
- Windows: `mock-driver/build/Release/mockodbc.dll` (or `Debug/`)
- Linux: `mock-driver/build/libmockodbc.so`
- macOS: `mock-driver/build/libmockodbc.dylib`

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

The mock driver supports these connection-string parameters:

| Parameter | Values | Description |
|-----------|--------|-------------|
| `Mode` | `Success`, `Failure`, `Random`, `Partial` | Overall behavior. `Success` = normal operation; `Failure` = all operations fail with `ErrorCode`; `Random` fails a percentage of calls. `Partial` is the mode for "fail only what `FailOn` names", but since D26 `FailOn` is honoured in **every** mode — it names functions, so it is a per-function override rather than a property of a mode. |
| `Catalog` | `Default`, `Empty`, `Large` | Mock schema preset. `Default` has USERS/ORDERS/PRODUCTS plus the `MOCK_INOUT(IN n INTEGER, OUT m INTEGER, INOUT s VARCHAR)` procedure used by the `{?=CALL …}` probes. |
| `ResultSetSize` | Number | Rows to return in result sets (default: 100). |
| `FailOn` | Function names | Comma-separated list of functions to fail (e.g., `FailOn=SQLExecute,SQLFetch`). Works with any `Mode`, including the default `Success`; naming a function also *narrows* `Failure` and `Random` to the named ones. **A `…W` name** (D78) fails only calls that entered through that Unicode entry point, while an ANSI name fails both widths — so `FailOn=SQLPrepareW` leaves the ANSI `SQLPrepare` working. Note this is a lever for the driver's own tests: through a driver manager, a Unicode-only driver receives *both* widths as W, so an application cannot tell the two configurations apart. |
| `ErrorCode` | SQLSTATE | SQLSTATE returned for failures (default: HY000). |
| `ErrorCount` | Number | Number of diagnostic records emitted per error (default: 1). |
| `Latency` | e.g. `10ms`, `500us`, `2s` | Simulated per-call delay. A bare number is milliseconds. |
| `FetchReturnsWarning` | `false` (default), `true` | Every `SQLFetch` that returns a row also posts `01004` and returns `SQL_SUCCESS_WITH_INFO`. Real drivers warn per row; an application must keep fetching until `SQL_NO_DATA`. |
| `BufferValidation` | `Strict` (default), `Lenient` | `Lenient` returns strings **without** their NUL terminator, the classic careless-driver behaviour. Since D62 this is applied in the one copy every string return passes through, so it reaches `SQLGetData`, `SQLDescribeCol`, `SQLGetCursorName`, `SQLNativeSql` and `SQLGetDiagRec` as well as `SQLGetInfo`. |
| `StateChecking` | `Strict`, `Lenient` | ODBC state-machine validation. |
| `TransactionMode` | `ReadOnly`, `ReadWrite` | Transaction read/write capability. |
| **`SilentCorruption`** | `None` (default), `DropInserts`, `MangleVarchar`, `TruncateNumeric`, `NullAsEmpty`, `MangleUnicode`, `SkewNumeric`, `SkewNumericBound` | Silently tamper with stored data while keeping return codes successful — used to validate that round-trip / `verify_rows_persisted` checks actually catch a misbehaving driver. `NullAsEmpty` = NULL char/wchar cells fetch as empty + `ind=0` (Oracle-style). `MangleUnicode` = non-ASCII bytes replaced with `?` (codepage-bound driver pattern). `SkewNumeric` = every numeric cell comes back +1 on both the bound-column and `SQLGetData` paths. `SkewNumericBound` = only the bound-column path is skewed, so reading one column both ways disagrees. |
| **`NativeSqlPassThrough`** | `true`, `false` (default) | When `true`, `SQLNativeSql` returns the input verbatim without translating any escape sequence. Drives the escape-translation probes' canary path. |
| **`Procedures`** | (default) / `BrokenInout` | When `BrokenInout`, `MOCK_INOUT` runs but its callback returns no output values — mocks drivers that accept `{?=CALL …}` syntactically but never write back to OUT/INOUT bound buffers. |
| **`ArrayBindRowFailsAt`** | Number (default 0) | When `> 0`, the Nth row (1-indexed) of any array-parameter execute is forced to fail with SQLSTATE `23000`; surrounding rows execute normally. |
| **`SupportsArrayBind`** | `true` (default), `false` | When `false`, `SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE, > 1)` returns `SQL_ERROR` with SQLSTATE `HYC00`. |

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

Against `Mode=Success` the mock driver scores **190/190 scored probes (100%)**, with
15 further probes reported as `INFORMATIONAL` — 205 results in total. Six of those
record what the driver answered where the spec leaves no right answer to grade (an
optional attribute's value, a `SQLGetInfo` bitmask, the type `COUNT(*)` comes back
as). The other nine are enforced by the **driver manager** rather than the driver:
both the Windows DM and unixODBC check the ODBC state-transition table before
dispatching a call, so those results describe the stack you are running on and not
the driver you are testing. Neither group counts toward the pass rate — a guaranteed
pass is not evidence about a driver.

This number is a property of the reference driver, not a target: the point of the
mock is that every probe which *can* fail does fail when the driver is misconfigured,
which is what the fault-injection knobs and the e2e scenarios exercise.

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

Progress output goes to **stderr**, so stdout carries nothing but the report and
the pipe form works as written:

```bash
odbc-crusher "Driver={...}" -o json | jq '.summary'
```

The document is versioned:

| Key | Type | Notes |
|---|---|---|
| `schema_version` | integer | Currently `1`. Bumped whenever an existing key changes meaning, is renamed or is removed; purely additive changes keep the number. Consumers should reject a version they do not know. |
| `timestamp` | string | ISO-8601 UTC, e.g. `2026-09-07T13:28:29Z`. |
| `complete` | boolean | `false` in a report written while the run was still going. With `-f`, the report is snapshotted to the file as categories finish, so a run that is killed (CI caps it at 570s) still leaves a valid document holding everything that completed. Always check this before treating a summary as authoritative — a partial report has `categories` but no `summary`. |

## Interpreting Results

- **[PASS]** — The driver behaves correctly for this test.
- **[FAIL]** — The driver returned an unexpected result. Check the `actual` field and the ODBC spec reference for details.
- **[NOT ]** — The test was skipped because the driver does not support this feature (SKIP_UNSUPPORTED).
- **[ ?? ]** — The test result was inconclusive (SKIP_INCONCLUSIVE) — the driver may or may not support the feature.
- **[ERR!]** — An unexpected exception occurred during the test.
- **[INFO]** — Reported, but not scored (`INFORMATIONAL`). Some probes record what the driver said — a `SQLGetInfo` bitmask, a raw byte layout, a row count the spec leaves to the engine — where there is no right answer to grade. They are excluded from the pass rate rather than counted as passes.

The pass rate is `passed / scored`, where `scored` is every result except the
informational ones. The JSON summary publishes `scored` and `informational`
alongside `total_tests` so a consumer never has to infer the denominator.

In verbose mode (`-v`), each test also shows:
- The **expected** vs **actual** behavior
- A **suggestion** for fixing failures
- The relevant **ODBC spec section**

## Tested Databases

The `stress-test` GitHub Actions workflow exercises crusher against
six real ODBC drivers plus the in-repo mock. Driver versions, install
methods, source repos, and tags are all pinned in
[.github/drivers.json](./.github/drivers.json) — single source of truth
consumed by both the workflow (at runtime via `jq`) and the
`/triage-driver` skill (during analysis).

| Driver | Version | Source |
|---|---|---|
| **PostgreSQL** (psqlodbc) | 16.00.0000 | [postgresql-interfaces/psqlodbc](https://github.com/postgresql-interfaces/psqlodbc) |
| **MariaDB Connector/ODBC** | 3.1.15 | [mariadb-corporation/mariadb-connector-odbc](https://github.com/mariadb-corporation/mariadb-connector-odbc) |
| **MySQL Connector/ODBC** | 9.7.0 | [mysql/mysql-connector-odbc](https://github.com/mysql/mysql-connector-odbc) |
| **DuckDB ODBC** | 1.5.2.0 | [duckdb/duckdb-odbc](https://github.com/duckdb/duckdb-odbc) |
| **ClickHouse ODBC** | 1.5.3.20260311 | [ClickHouse/clickhouse-odbc](https://github.com/ClickHouse/clickhouse-odbc) |
| **Firebird ODBC Driver** | 3.5.0-rc1 | [FirebirdSQL/firebird-odbc-driver](https://github.com/FirebirdSQL/firebird-odbc-driver) |
| **Mock ODBC Driver** (in-repo) | tracks master | `mock-driver/` |

## Stress-Test Workflow

```bash
# Run all 6 drivers in parallel
gh workflow run stress-test.yml

# Run a single driver
gh workflow run stress-test.yml -f driver=duckdb
```

Every driver job:

1. Reads `.github/drivers.json` for its pinned version, URL/apt package,
   library name, and connection string.
2. Installs the driver — apt jobs use `apt-get install pkg=$APT_VERSION`
   so any drift in Ubuntu's repo fails loudly. Binary-download jobs pin
   the exact GitHub release URL.
3. Records `actual_version.txt` next to the report so triage can verify
   provenance.
4. Downloads the `odbc-crusher` binary from the **latest successful
   master CI run** (not the latest tag) — every stress-test exercises
   the probes that are in master right now.
5. Runs crusher twice (verbose text + JSON) and uploads both reports
   plus `actual_version.txt` as `report-<DRIVER>`.

## `/triage-driver` Skill

Once a stress-test run completes, the `/triage-driver` skill closes the
loop: it dispatches a single-driver run, clones the matching driver
source tree at the exact tag the binary was built from, and produces a
markdown report classifying every FAIL/ERROR with one of four labels:

| Label | Meaning |
|---|---|
| `BUG_IN_DRIVER` | Driver source has spec-violating behavior. Cite `file:line` + spec ref. |
| `BUG_IN_CRUSHER` | Probe is wrong — false positive. Cite `src/tests/*.cpp:line`. |
| `DRIVER_LIMITATION` | Driver knowingly doesn't implement an optional ODBC feature (spec-legal). |
| `INCONCLUSIVE` | Couldn't tell from source alone — needs runtime trace or upstream issue search. |

```
# From inside Claude Code, in this repo:
/triage-driver duckdb
```

The skill orchestrates seven steps: read the manifest → dispatch CI →
clone source in parallel → wait + download artifact → cross-check three
version values (manifest vs `actual_version.txt` vs JSON's
`driver_info.driver_version`) → spawn an analysis sub-agent → write
`./tmp/triage/<DRIVER>/<DRIVER_UPPER>-v<VERSION>-ODBC-CRUSHER-REPORT.md`.

If any of the three version values disagree the skill aborts before
producing a misleading report. The skill itself lives in
[.claude/skills/triage-driver/](./.claude/skills/triage-driver/) (both
the orchestration body and the analysis sub-agent prompt).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT
