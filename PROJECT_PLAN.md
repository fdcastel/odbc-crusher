# ODBC Crusher C++ - Project Plan

**Version**: 2.0.0  
**Last Updated**: February 5, 2026  
**Status**: Phase 3 - Complete

---

## 🎯 Project Overview

**Name**: ODBC Crusher  
**Purpose**: A comprehensive CLI debugging and testing tool for ODBC driver developers  
**Language**: C++ (C++17 or later)  
**Build System**: CMake (3.20+)  
**Test Framework**: CTest with Google Test (gtest)  
**Platforms**: Windows, Linux (primary), macOS (secondary)

---

## 📚 Lessons Learned from Python Version

The original Python/pyodbc implementation taught us critical lessons that inform this C++ rewrite:

### Why We're Migrating to C++

1. **Direct ODBC API Access**: pyodbc abstracts away ODBC internals. We need raw access to:
   - All ODBC handle types (Environment, Connection, Statement, Descriptor)
   - All ODBC functions, not just what pyodbc exposes
   - Diagnostic records and SQLSTATE codes
   - Driver Manager vs Driver behavior distinction

2. **No Wrapper Limitations**: pyodbc doesn't expose:
   - `SQLGetFunctions` (we had to use ctypes hack)
   - Many `SQL_*` constants for `SQLGetInfo`
   - Descriptor handle operations
   - Asynchronous operations
   - Bulk operations properly

3. **Performance**: C++ allows microsecond-level timing, zero overhead testing

4. **Driver Developer Target**: ODBC drivers are written in C/C++ - tool should match

### Critical Knowledge Preserved

#### Rule #1: DO NOT TRUST ANYTHING THE ODBC DRIVER TELLS YOU
- Drivers report false errors (e.g., Firebird's "[08004] File Database is not found" when file exists)
- Always verify with independent means when possible

#### Rule #2: Don't Assume Bugs - Ask the Driver
Use ODBC specification functions instead of guessing:
- `SQLGetInfo` - Driver/DBMS properties (40+ attributes)
- `SQLGetTypeInfo` - Supported data types catalog
- `SQLGetFunctions` - Which ODBC functions are implemented

#### Rule #3: Database-Specific SQL Syntax
Different databases require different SQL:
| Database     | Bare `?` | Requires FROM      | Requires CAST |
|-------------|----------|-------------------|---------------|
| Firebird    | ❌       | ✅ RDB$DATABASE   | ⚠️ Recommended |
| MySQL       | ✅       | ❌                | ❌            |
| Oracle      | ❌       | ✅ DUAL           | ❌            |
| SQL Server  | ✅       | ❌                | ❌            |

#### Bugs Already Discovered
- **Firebird ODBC**: False "file not found" error when database file exists

---

## 🏗️ Architecture

### Project Structure
```
odbc-crusher/
├── CMakeLists.txt              # Root CMake configuration
├── cmake/                      # CMake modules and toolchain files
│   ├── FindODBC.cmake          # Cross-platform ODBC detection
│   ├── CompilerWarnings.cmake  # Strict warning flags
│   └── Platform.cmake          # Platform-specific settings
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                # CLI entry point
│   ├── core/
│   │   ├── CMakeLists.txt
│   │   ├── odbc_handle.hpp     # RAII handle wrappers
│   │   ├── odbc_handle.cpp
│   │   ├── odbc_connection.hpp # Connection management
│   │   ├── odbc_connection.cpp
│   │   ├── odbc_statement.hpp  # Statement execution
│   │   ├── odbc_statement.cpp
│   │   ├── odbc_error.hpp      # Error/diagnostic handling
│   │   ├── odbc_error.cpp
│   │   └── odbc_types.hpp      # Type mappings and utilities
│   ├── discovery/
│   │   ├── CMakeLists.txt
│   │   ├── driver_info.hpp     # SQLGetInfo wrapper
│   │   ├── driver_info.cpp
│   │   ├── type_info.hpp       # SQLGetTypeInfo wrapper
│   │   ├── type_info.cpp
│   │   ├── function_info.hpp   # SQLGetFunctions wrapper
│   │   └── function_info.cpp
│   ├── tests/                  # ODBC test implementations
│   │   ├── CMakeLists.txt
│   │   ├── test_base.hpp       # Base test class
│   │   ├── test_base.cpp
│   │   ├── connection_tests.hpp
│   │   ├── connection_tests.cpp
│   │   ├── handle_tests.hpp
│   │   ├── handle_tests.cpp
│   │   ├── statement_tests.hpp
│   │   ├── statement_tests.cpp
│   │   ├── metadata_tests.hpp
│   │   ├── metadata_tests.cpp
│   │   ├── datatype_tests.hpp
│   │   ├── datatype_tests.cpp
│   │   ├── advanced_tests.hpp
│   │   ├── advanced_tests.cpp
│   │   ├── capability_tests.hpp
│   │   └── capability_tests.cpp
│   ├── runner/
│   │   ├── CMakeLists.txt
│   │   ├── test_runner.hpp     # Test execution engine
│   │   ├── test_runner.cpp
│   │   ├── test_result.hpp     # Result data structures
│   │   └── test_result.cpp
│   ├── reporter/
│   │   ├── CMakeLists.txt
│   │   ├── reporter.hpp        # Abstract reporter interface
│   │   ├── console_reporter.hpp # Rich terminal output
│   │   ├── console_reporter.cpp
│   │   ├── json_reporter.hpp   # JSON output
│   │   ├── json_reporter.cpp
│   │   ├── html_reporter.hpp   # HTML report
│   │   └── html_reporter.cpp
│   └── cli/
│       ├── CMakeLists.txt
│       ├── cli_parser.hpp      # Command-line parsing
│       ├── cli_parser.cpp
│       ├── config.hpp          # Configuration management
│       └── config.cpp
├── tests/                      # Unit tests for the tool itself
│   ├── CMakeLists.txt
│   ├── test_main.cpp           # GTest main
│   ├── test_odbc_handle.cpp
│   ├── test_driver_info.cpp
│   ├── test_reporter.cpp
│   └── mock_odbc.hpp           # ODBC mocking utilities
├── include/                    # Public headers (if library mode)
│   └── odbc_crusher/
│       └── version.hpp
├── docs/
│   ├── GETTING_STARTED.md
│   ├── BUILDING.md
│   ├── ADDING_TESTS.md
│   └── ODBC_REFERENCE.md
├── tmp/                        # Temporary files (gitignored)
├── PROJECT_PLAN.md             # This file
├── AGENT_INSTRUCTIONS.md       # AI agent guidelines
├── README.md                   # Project documentation
├── LICENSE
└── .gitignore
```

### Core Components

#### 1. RAII Handle Management
```cpp
// Modern C++ RAII wrappers for ODBC handles
class OdbcEnvironment {
    SQLHENV handle_ = SQL_NULL_HENV;
public:
    OdbcEnvironment();
    ~OdbcEnvironment();  // Automatic cleanup
    SQLHENV get() const noexcept;
    // Non-copyable, movable
};

class OdbcConnection {
    SQLHDBC handle_ = SQL_NULL_HDBC;
    OdbcEnvironment& env_;
public:
    explicit OdbcConnection(OdbcEnvironment& env);
    void connect(std::string_view connection_string);
    void disconnect();
    ~OdbcConnection();
};

class OdbcStatement {
    SQLHSTMT handle_ = SQL_NULL_HSTMT;
    OdbcConnection& conn_;
public:
    explicit OdbcStatement(OdbcConnection& conn);
    void execute(std::string_view sql);
    bool fetch();
    ~OdbcStatement();
};
```

#### 2. Error Handling
```cpp
struct OdbcDiagnostic {
    std::string sqlstate;      // 5-char SQLSTATE
    SQLINTEGER native_error;   // Driver-specific error code
    std::string message;       // Error message
    SQLSMALLINT record_number; // Diagnostic record number
};

class OdbcError : public std::runtime_error {
    std::vector<OdbcDiagnostic> diagnostics_;
public:
    // Extracts all diagnostic records from handle
    static OdbcError from_handle(SQLSMALLINT handle_type, SQLHANDLE handle);
    const std::vector<OdbcDiagnostic>& diagnostics() const;
};
```

#### 3. Test Result Structure
```cpp
enum class TestStatus { PASS, FAIL, SKIP, ERROR };
enum class Severity { CRITICAL, ERROR, WARNING, INFO };

struct TestResult {
    std::string test_name;
    std::string function;        // ODBC function tested
    TestStatus status;
    Severity severity;
    std::string expected;
    std::string actual;
    std::optional<std::string> diagnostic;
    std::optional<std::string> suggestion;
    std::chrono::microseconds duration;
};
```

### Cross-Platform ODBC

#### Windows
```cpp
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
// Link: odbc32.lib
```

#### Linux (unixODBC)
```cpp
#include <sql.h>
#include <sqlext.h>
// Link: libodbc.so
// Package: unixodbc-dev
```

#### CMake Detection
```cmake
# cmake/FindODBC.cmake
find_package(ODBC REQUIRED)
target_link_libraries(odbc_crusher PRIVATE ODBC::ODBC)
```

---

## 📋 Development Phases

### Phase 0: Project Setup ✅ (Completed - February 5, 2026)
**Goal**: Establish project structure and build system

- [x] Create CMake project structure
- [x] Set up cross-platform ODBC detection
- [x] Configure compiler warnings (strict mode)
- [x] Set up CTest integration
- [x] Add Google Test as dependency
- [ ] Create CI/CD pipeline (GitHub Actions) - Deferred
- [x] Basic CLI argument parsing
- [x] Create AGENT_INSTRUCTIONS.md

**Deliverables**: ✅ ALL CORE COMPLETED
- ✅ Project builds on Windows
- ✅ `odbc-crusher --help` works
- ✅ 13 unit tests passing (100%)

**Files Created**:
- `CMakeLists.txt` - Root CMake configuration
- `cmake/CompilerWarnings.cmake` - Strict compiler warnings
- `cmake/Platform.cmake` - Platform-specific settings
- `.gitignore` - Build artifacts and temporary files
- `include/odbc_crusher/version.hpp` - Version information
- `src/main.cpp` - CLI entry point
- `src/CMakeLists.txt` - Source build configuration
- `tests/CMakeLists.txt` - Test build configuration
- `tests/test_main.cpp` - Google Test main

### Phase 1: Core ODBC Infrastructure ✅ (Completed - February 5, 2026)
**Goal**: RAII wrappers for ODBC handles and basic connection

- [x] `OdbcEnvironment` class with proper initialization
- [x] `OdbcConnection` class with connect/disconnect
- [x] `OdbcStatement` class with basic execution
- [x] `OdbcError` class with diagnostic extraction
- [x] Comprehensive error handling (all ODBC return codes)
- [ ] Basic logging infrastructure - Deferred
- [x] Unit tests for all core classes

**ODBC Functions Covered**:
- `SQLAllocHandle` / `SQLFreeHandle` ✅
- `SQLSetEnvAttr` (ODBC version) ✅
- `SQLDriverConnect` / `SQLDisconnect` ✅
- `SQLGetDiagRec` / `SQLGetDiagField` ✅
- `SQLExecDirect` / `SQLPrepare` / `SQLExecute` ✅
- `SQLFetch` / `SQLCloseCursor` ✅

**Deliverables**: ✅ ALL COMPLETED
- ✅ Connect to any ODBC data source
- ✅ Report connection success/failure with diagnostics
- ✅ Clean resource management (RAII, no leaks)

**Files Created**:
- `src/core/odbc_environment.hpp/cpp` - Environment handle wrapper
- `src/core/odbc_connection.hpp/cpp` - Connection handle wrapper
- `src/core/odbc_statement.hpp/cpp` - Statement handle wrapper
- `src/core/odbc_error.hpp/cpp` - Error and diagnostic handling
- `src/cli/cli_parser.hpp/cpp` - CLI parsing (placeholder)
- `src/cli/config.hpp/cpp` - Configuration (placeholder)
- `tests/test_odbc_environment.cpp` - Environment tests (4 tests)
- `tests/test_odbc_connection.cpp` - Connection tests (6 tests)
- `tests/test_odbc_error.cpp` - Error handling tests (3 tests)

**Test Results**: 13/13 tests passing ✅
- Successfully connects to Firebird ODBC
- Successfully connects to MySQL ODBC
- Clean disconnect operations
- Clean resource management (no leaks)

### Phase 2: Driver Discovery ✅ (Completed - February 5, 2026)
**Goal**: Implement ODBC discovery functions (from Python Phase 6.5)

- [x] `DriverInfo` class using `SQLGetInfo`
  - Driver name, version, ODBC version
  - DBMS name and version
  - SQL conformance level
  - Feature flags (40+ properties)
- [x] `TypeInfo` class using `SQLGetTypeInfo`
  - All supported data types
  - Type properties (precision, scale, nullable)
- [x] `FunctionInfo` class using `SQLGetFunctions`
  - Bitmap of implemented functions
  - Human-readable function names
- [ ] Display driver capabilities before running tests - Deferred to later phase

**ODBC Functions Covered**:
- `SQLGetInfo` (all relevant info types) ✅
- `SQLGetTypeInfo` ✅
- `SQLGetFunctions` (SQL_API_ODBC3_ALL_FUNCTIONS) ✅

**Deliverables**: ✅ ALL COMPLETED
- ✅ Collect driver and DBMS information
- ✅ Enumerate all supported data types
- ✅ Check which ODBC functions are implemented
- ✅ Format informative summaries

**Files Created**:
- `src/discovery/driver_info.hpp/cpp` - SQLGetInfo wrapper (collects 11+ driver/DBMS properties)
- `src/discovery/type_info.hpp/cpp` - SQLGetTypeInfo wrapper (enumerates data types)
- `src/discovery/function_info.hpp/cpp` - SQLGetFunctions wrapper (checks 50+ ODBC functions)
- `tests/test_driver_info.cpp` - Driver info tests (2 tests, Firebird and MySQL)
- `tests/test_type_info.cpp` - Type info tests (2 tests)
- `tests/test_function_info.cpp` - Function info tests (2 tests)

**Test Results**: 19/19 tests passing ✅ (100%)
- Successfully collects Firebird driver info (FirebirdODBC, Firebird 5.0)
- Successfully collects MySQL driver info
- Firebird: 22 data types discovered
- MySQL: Gracefully handles SQLGetTypeInfo limitations
- Firebird: 50+ ODBC functions checked
- All catalog functions verified as supported
- `SQLGetFunctions` (SQL_API_ODBC3_ALL_FUNCTIONS)

**Deliverables**:
- Complete driver capability report
- Know what the driver supports before testing

### Phase 3: Connection Tests ✅ (Completed - February 5, 2026)
**Goal**: Test connection establishment and properties

- [x] Basic connection test
- [x] Connection with various string formats
- [x] Connection attributes (get/set)
- [x] Connection timeout handling
- [x] Multiple simultaneous connections (via multiple statement handles)
- [ ] Connection pooling behavior - Deferred

**ODBC Functions Covered**:
- `SQLConnect` ✅ (via tests)
- `SQLDriverConnect` ✅
- `SQLBrowseConnect` - Deferred
- `SQLGetConnectAttr` / `SQLSetConnectAttr` ✅

**Deliverables**: ✅ CORE COMPLETED
- ✅ TestBase infrastructure for reusable test patterns
- ✅ ConnectionTests class with 5 different connection tests
- ✅ Detailed test result structure (status, timing, diagnostics)
- ✅ Integration tests with Firebird and MySQL

**Files Created**:
- `src/tests/test_base.hpp/cpp` - Base class for all ODBC test categories
- `src/tests/connection_tests.hpp/cpp` - Connection-specific tests (5 tests)
- `tests/test_connection_tests.cpp` - Unit tests for connection tests (2 integration tests)

**Test Results**: 21/21 tests passing ✅ (100%)
- Firebird connection tests: 5 tests run (connection info, driver name, multiple statements, attributes, timeout)
- MySQL connection tests: 5 tests run
- All tests complete within microseconds
- Comprehensive diagnostic capture

### Phase 4: Statement Tests ⬜
**Goal**: Test statement execution and result handling

- [ ] Handle allocation and reuse
- [ ] Simple query execution (`SQLExecDirect`)
- [ ] Prepared statements (`SQLPrepare` / `SQLExecute`)
- [ ] Parameter binding (`SQLBindParameter`)
- [ ] Result set fetching (`SQLFetch`, `SQLFetchScroll`)
- [ ] Column binding (`SQLBindCol`, `SQLGetData`)
- [ ] Multiple result sets
- [ ] Statement attributes

**ODBC Functions Covered**:
- `SQLPrepare` / `SQLExecute` / `SQLExecDirect`
- `SQLBindParameter`
- `SQLNumParams` / `SQLDescribeParam`
- `SQLFetch` / `SQLFetchScroll`
- `SQLBindCol` / `SQLGetData`
- `SQLNumResultCols` / `SQLDescribeCol`
- `SQLRowCount`
- `SQLMoreResults`
- `SQLCloseCursor`

### Phase 5: Metadata Tests ⬜
**Goal**: Test catalog functions (from Python Phase 4)

- [ ] `SQLTables` - Table listing
- [ ] `SQLColumns` - Column metadata
- [ ] `SQLPrimaryKeys` - Primary key information
- [ ] `SQLForeignKeys` - Foreign key relationships
- [ ] `SQLStatistics` - Index information
- [ ] `SQLProcedures` / `SQLProcedureColumns`
- [ ] `SQLSpecialColumns` - Row identifiers
- [ ] `SQLTablePrivileges` / `SQLColumnPrivileges`

**ODBC Functions Covered**:
- All catalog functions
- Proper handling of NULL catalog/schema/table parameters

### Phase 6: Data Type Tests ⬜
**Goal**: Comprehensive data type handling (from Python Phase 6)

- [ ] Integer types (SMALLINT, INTEGER, BIGINT)
- [ ] Decimal types (DECIMAL, NUMERIC)
- [ ] Float types (FLOAT, DOUBLE, REAL)
- [ ] Character types (CHAR, VARCHAR, LONGVARCHAR)
- [ ] Unicode types (WCHAR, WVARCHAR, WLONGVARCHAR)
- [ ] Binary types (BINARY, VARBINARY, LONGVARBINARY)
- [ ] Date/Time types (DATE, TIME, TIMESTAMP)
- [ ] Interval types
- [ ] GUID/UUID type
- [ ] Edge cases (NULL, MIN/MAX values, precision limits)

**ODBC Functions Covered**:
- Type conversions via SQLBindCol/SQLGetData
- SQL_C_* type specifications

### Phase 7: Transaction Tests ⬜
**Goal**: Test transaction handling (from Python Phase 5)

- [ ] Autocommit mode
- [ ] Manual commit/rollback
- [ ] Transaction isolation levels
- [ ] Nested transactions (savepoints if supported)
- [ ] DDL in transactions behavior

**ODBC Functions Covered**:
- `SQLEndTran`
- `SQLSetConnectAttr` (isolation level)

### Phase 8: Advanced Features ⬜
**Goal**: Test advanced ODBC capabilities

- [ ] Asynchronous execution
- [ ] Array binding (bulk operations)
- [ ] Cursor types (forward-only, static, keyset, dynamic)
- [ ] Positioned updates/deletes
- [ ] Bookmark support
- [ ] Descriptor handle operations
- [ ] SQLCancel / SQLCancelHandle
- [ ] ODBC 3.8 features

**ODBC Functions Covered**:
- `SQLSetStmtAttr` (cursor, array binding)
- `SQLGetDescField` / `SQLSetDescField`
- `SQLCopyDesc`
- `SQLCancel`

### Phase 9: Reporting ⬜
**Goal**: Rich, actionable output

- [ ] Console reporter (colored, formatted tables)
- [ ] JSON reporter
- [ ] HTML reporter (standalone file)
- [ ] XML reporter
- [ ] CSV export
- [ ] Summary statistics
- [ ] Severity filtering
- [ ] Comparison mode (diff between runs)

### Phase 10: Polish & Documentation ⬜
**Goal**: Production-ready tool

- [ ] Comprehensive --help
- [ ] Configuration file support
- [ ] Test filtering/selection
- [ ] Parallel test execution
- [ ] Progress indicator
- [ ] Timeout handling
- [ ] Complete documentation
- [ ] Example output files
- [ ] Integration with CI/CD systems

---

## 🛠️ Technical Decisions

### C++ Standard: C++17
**Rationale**:
- `std::optional`, `std::string_view` for clean APIs
- `std::filesystem` for path handling
- Structured bindings for cleaner code
- Wide compiler support (MSVC 2017+, GCC 7+, Clang 5+)

### Build System: CMake 3.20+
**Rationale**:
- Industry standard for C++ projects
- Excellent cross-platform support
- Native CTest integration
- Modern target-based configuration

### Test Framework: Google Test
**Rationale**:
- Mature, well-documented
- Good CMake integration (FetchContent)
- Mocking support (Google Mock)
- Compatible with CTest

### CLI Parsing: CLI11 or Argparse
**Rationale**:
- Header-only options available
- Similar to Python's Click/Argparse
- Clean, modern C++ API

### JSON: nlohmann/json
**Rationale**:
- Header-only
- Intuitive syntax
- Wide adoption

### Terminal Colors: fmt + platform detection
**Rationale**:
- {fmt} library for formatting
- ANSI codes on Linux/macOS
- Windows Console API or ANSI (Win10+)

---

## ✅ Success Metrics

| Metric | Target |
|--------|--------|
| ODBC Function Coverage | 100+ functions tested |
| Platform Support | Windows + Linux builds passing |
| Test Coverage | >80% code coverage |
| Build Time | <2 minutes (clean build) |
| Binary Size | <5 MB (Release) |
| Test Suite Runtime | <5 minutes per driver |
| Documentation | All public APIs documented |

---

## 📦 Dependencies

### Required
- CMake 3.20+
- C++17 compatible compiler
- ODBC headers and libraries
  - Windows: Built-in (odbc32.lib)
  - Linux: unixODBC-dev package

### Bundled/Fetched
- Google Test (via FetchContent)
- CLI11 (header-only)
- nlohmann/json (header-only)
- {fmt} library (optional, for formatting)

---

## 🔄 Comparison: Python vs C++ Version

| Aspect | Python (old) | C++ (new) |
|--------|--------------|-----------|
| ODBC Access | Via pyodbc wrapper | Direct ODBC API |
| SQLGetFunctions | ctypes hack | Native call |
| Handle Control | Hidden by pyodbc | Full RAII control |
| Descriptor Access | Not available | Full access |
| Error Details | Limited | Complete diagnostic chain |
| Performance | Milliseconds | Microseconds |
| Binary | Python runtime required | Standalone executable |
| Platforms | Windows (Linux needs work) | Windows, Linux, macOS |

---

## 📚 References

### ODBC Specification
- [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
- [ODBC 3.8 Upgrade Guide](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-driver/upgrading-a-3-5-driver-to-a-3-8-driver)

### unixODBC (Linux)
- [unixODBC Documentation](http://www.unixodbc.org/)
- [unixODBC API Reference](http://www.unixodbc.org/doc/)

### C++ Libraries
- [Google Test](https://google.github.io/googletest/)
- [CLI11](https://github.com/CLIUtils/CLI11)
- [nlohmann/json](https://github.com/nlohmann/json)
- [{fmt}](https://fmt.dev/)

---

## 🚦 Current Status

**Phase**: Phase 0 - Project Setup (Not Started)  
**Version**: 2.0.0-dev  
**Last Milestone**: Project plan created from Python version learnings  
**Next Milestone**: Create CMake project structure and basic CLI

---

**Important**: Keep this plan updated as the project evolves. Every significant change should be reflected here.
