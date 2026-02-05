# ODBC Crusher

[![CI](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/ci.yml)
[![Release](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml/badge.svg)](https://github.com/fdcastel/odbc-crusher/actions/workflows/release.yml)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)]()
[![License](https://img.shields.io/badge/license-MIT-blue)]()

**Repository**: https://github.com/fdcastel/odbc-crusher

A comprehensive CLI debugging and testing tool for ODBC driver developers written in modern C++.

## 🎉 Project Status: **COMPLETE** (Phases 0-9)

All core phases including **reporting** have been successfully implemented. The application is **fully functional** with beautiful console output and JSON export capabilities!

## ✨ Features

### Core Infrastructure (Phase 1)
- **RAII ODBC Wrappers**: Safe, modern C++ wrappers for ODBC handles
  - `OdbcEnvironment` - Environment handle management
  - `OdbcConnection` - Connection handle with connect/disconnect
  - `OdbcStatement` - Statement handle with execute/fetch/prepare
- **Comprehensive Error Handling**: `OdbcError` exception class with full diagnostic extraction
- **Memory Safe**: No manual memory management, zero leaks

### Driver Discovery (Phase 2)
- **DriverInfo**: Collects 11+ driver and DBMS properties via `SQLGetInfo`
- **TypeInfo**: Enumerates all supported data types via `SQLGetTypeInfo`
- **FunctionInfo**: Checks 50+ ODBC functions via `SQLGetFunctions` bitmap

### Connection Tests (Phase 3)
- Connection establishment and validation
- Connection attributes (autocommit, timeout)
- Multiple statement handle allocation
- Driver name and database name retrieval

### Statement Tests (Phase 4)
- Simple query execution (`SQLExecDirect`)
- Prepared statements (`SQLPrepare`/`SQLExecute`)
- Parameter binding (`SQLBindParameter`)
- Result fetching (`SQLFetch`)
- Column metadata (`SQLNumResultCols`, `SQLDescribeCol`)
- Statement reuse (`SQLCloseCursor`)
- Multiple result sets (`SQLMoreResults`)

### Metadata/Catalog Tests (Phase 5)
- Table listing (`SQLTables`)
- Column metadata (`SQLColumns`)
- Primary key information (`SQLPrimaryKeys`)
- Index/statistics (`SQLStatistics`)
- Special columns (`SQLSpecialColumns`)

### Data Type Tests (Phase 6)
- Integer types (SMALLINT, INTEGER, BIGINT)
- Decimal types (DECIMAL, NUMERIC)
- Float types (FLOAT, DOUBLE, REAL)
- String types (CHAR, VARCHAR)
- Date/Time types (DATE with SQL_DATE_STRUCT)
- NULL value handling (SQL_NULL_DATA indicator)

### Reporting (Phase 7) ✅
- **Console Reporter**: ASCII formatted output with:
  - Category-based organization
  - Pass/fail/skip/error icons ([PASS]/[FAIL]/[SKIP]/[ERR!])
  - Duration formatting (us, ms, s)
  - Verbose mode (`-v`) for detailed diagnostics
  - Summary statistics with percentages
  - **Driver Information Display**: Shows comprehensive driver info BEFORE tests
    - Driver name, version, ODBC version
    - DBMS name and version
    - SQL conformance levels
    - All supported data types in formatted table
    - ODBC function support (52 functions checked)
- **JSON Reporter**: Structured output for CI/CD:
  - Machine-readable format
  - Complete test details and diagnostics
  - Timestamp and connection metadata
  - File (`-f filename.json`) or stdout output
- **Smart Exit Codes**:
  - 0 = All tests passed
  - 1 = Some tests failed/errored
  - 2 = ODBC connection error
  - 3 = Other error

### Transaction Tests (Phase 8) ✅
- Autocommit mode testing (query and toggle)
- Manual commit/rollback with `SQLEndTran`
- Transaction isolation level queries
- Table creation for transaction testing

### Advanced Features (Phase 9) ✅
- Cursor types (forward-only, static, keyset, dynamic)
- Array/bulk parameter binding
- Asynchronous execution capability
- Rowset size for block cursors
- Positioned operations (concurrency control)
- Statement attribute queries

## 🚀 Quick Start

### Build from Source

#### Prerequisites
- CMake 3.20 or higher
- Console output (default)
.\build\src\Debug\odbc-crusher.exe "Driver={Your ODBC Driver};..."

# Verbose mode - shows detailed diagnostics
.\build\src\Debug\odbc-crusher.exe "Driver={...}" -v

# JSON output to stdout
.\build\src\Debug\odbc-crusher.exe "Driver={...}" -o json

# JSON output to file
.\build\src\Debug\odbc-crusher.exe "Driver={...}" -o json -f report.json

# Example with environment variable
$env:MYSQL_ODBC_CONNECTION='Driver={MySQL ODBC 9.6 Unicode Driver};Server=localhost;Database=test;UID=root;PWD=password'
.\build\src\Debug\odbc-crusher.exe $env:MYSQL_ODBC_CONNECTION
```

### Example Output

```
╔════════════════════════════════════════════════════════════════╗
║           ODBC CRUSHER - Driver Testing Report                ║
╚════════════════════════════════════════════════════════════════╝

Connection: Driver={MySQL ODBC 9.6 Unicode Driver};...

────────────────────────────────────────────────────────────────
  Connection Tests
────────────────────────────────────────────────────────────────
  ✓ test_connection_info (992 μs)
  ✓ test_connection_string_format (3 μs)
  ✓ test_multiple_statements (48 μs)
  ✓ test_connection_attributes (25 μs)
  ✓ test_connection_timeout (98 μs)

  Category Summary: 5 passed

... (more test categories) ...

════════════════════════════════════════════════════════════════
                        FINAL SUMMARY
════════════════════════════════════════════════════════════════

  Total Tests:  23
  Passed:       20 (87.0%)
  Skipped:      3
  Total Time:   74.28 ms

  ✓ ALL TESTS PASSED!

════════════════════════════════════════════════════════════════
```powershell
# Windows with MSVC
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug

# Linux with GCC
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Run

```powershell
# Basic usage
.\build\src\Debug\odbc-crusher.exe "Driver={Your ODBC Driver};..."

# With environment variables
$env:FIREBIRD_ODBC_CONNECTION='Driver={Firebird ODBC Driver};...'
.\build\src\Debug\odbc-crusher.exe $env:FIREBIRD_ODBC_CONNECTION
```

### Test

```powershell
# Set connection strings
$env:FIREBIRD_ODBC_CONNECTION='Driver={Firebird ODBC Driver};Database=/path/to/db.fdb;UID=SYSDBA;PWD=masterkey'
$env:MYSQL_ODBC_CONNECTION='Driver={MySQL ODBC 9.6 Unicode Driver};Server=localhost;Database=test;UID=root;PWD=password'

# Run all tests
ctest --test-dir build -C Debug --output-on-failure
```

## 📊 Test Results

```
Test Project C:/temp/new/odbc-crusher/build
  Total Tests: 27
  Passed:      27 (100%)
  Failed:      0
  Skipped:     05,000+ lines of C++17
- **Test Files**: 16 files
- **Reporting System**: 2 reporters (Console, JSON)
- **Build Time**: <30 seconds (MSVC, Windows)
- **Test Execution**: <100ms for all 23 tests
- **Code Quality**: 
  - RAII throughout, no manual memory management
  - Exception-based error handling
  - Modern C++17 features (std::optional, std::chrono, std::unique_ptr)
  - Beautiful Unicode output with box-drawing characters MySQL)
- **Statement Tests**: 2 integration tests
- **Metadata Tests**: 2 integration tests
- **Data Type Tests**: 2 integration tests

## 🏗️ Architecture

```
odbc-crusher/
├── src/
│   ├── core/          # Core ODBC wrappers
│   │   ├── odbc_environment.hpp/cpp
│   │   ├── odbc_connection.hpp/cpp
│   │   ├── odbc_statement.hpp/cpp
│   │   └── odbc_error.hpp/cpp
│   ├── discovery/     # Driver discovery
│   │   ├── driver_info.hpp/cpp
│   │   ├── type_info.hpp/cpp
│   │   └── function_info.hpp/cpp
│   ├── tests/         # Test infrastructure
│   │   ├── test_base.hpp/cpp
│   │   ├── connection_tests.hpp/cpp
│   │   ├── statement_tests.hpp/cpp
│   │   ├── metadata_tests.hpp/cpp
│   │   └── datatype_tests.hpp/cpp
│   └── cli/           # Command-line interface
├── tests/             # Unit tests
├── cmake/             # CMake modules
└── CMakeLists.txt
```

## 🧪 Tested Databases

- ✅ **Firebird 5.0** (Firebird ODBC Driver)
- ✅ **MySQL 8.0+** (MySQL Connector/ODBC 9.6)

## 📈 Project Metrics

- **Total Lines of Code**: ~4,500+ lines of C++17
- **Test Files**: 16 files
- **Build Time**: <30 seconds (MSVC, Windows)
- **Test Execution**: <2 seconds for all 27 tests
- **Code Quality**: 
  - RAII throughout, no manual memory management
  - Exception-based error handling
  - Modern C++17 features (std::optional, std::chrono)

## 🎯 Design Principles

### 1. Direct ODBC API Access
No wrapper limitations - direct access to all ODBC functions and handles.

### 2. Cross-Database Compatibility
Test queries support multiple database syntaxes:
- Firebird: `SELECT ... FROM RDB$DATABASE`
- MySQL: `SELECT ...`
- Oracle: `SELECT ... FROM DUAL`
- SQL-92: Standard literals

### 3. Graceful Degradation
Tests skip gracefully when features aren't supported by the driver.

### 4. Comprehensive Diagnostics
Full SQLSTATE and diagnostic record extraction on every error.

## 📝 License

MIT License - see LICENSE file for details.

## 🙏 Acknowledgments

- Based on lessons learned from the Python/pyodbc version
- Inspired by the need for better ODBC driver testing tools
- Built following [AGENT_INSTRUCTIONS.md](AGENT_INSTRUCTIONS.md) standards

## 🔗 Related Projects

- [Firebird ODBC Driver](https://github.com/FirebirdSQL/firebird-odbc-driver)
- [MySQL Connector/ODBC](https://dev.mysql.com/downloads/connector/odbc/)
- [unixODBC](http://www.unixodbc.org/) (Linux/macOS)

---

**Built with ❤️ and modern C++**
