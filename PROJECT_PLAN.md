# ODBC Crusher - Project Plan

**Last Updated**: February 3, 2026 - Phase 2 In Progress

## ⚠️ CRITICAL FINDING - First Bug Discovered!

**Firebird ODBC Driver - False Error Messages**  
The Firebird ODBC driver reports "[08004] File Database is not found" even when the database file exists and is accessible. DuckDB connects successfully with the same connection string while pyodbc fails. This validates **Rule #1: DO NOT TRUST ANYTHING THE ODBC DRIVER TELLS YOU.**

See [DRIVER_BUGS.md](DRIVER_BUGS.md) for details.

## Project Overview

**Name**: ODBC Crusher  
**Purpose**: A comprehensive CLI debugging tool for ODBC driver developers  
**Language**: Python  
**Package Manager**: uv (not pip)

## Vision

Create an incremental, extensible tool that tests ODBC driver implementations against the ODBC specification, identifying missing, broken, or incorrectly implemented functions with actionable diagnostics.

## Core Requirements

### Input
- Single ODBC connection string (e.g., `"DSN=mydb;UID=user;PWD=pass"`)

### Output
- Comprehensive test report showing:
  - ✅ Functions that work correctly
  - ❌ Functions that return incorrect results
  - ⚠️ Functions that are missing/not implemented
  - 📋 Expected vs actual behavior
  - 💡 Suggestions for fixing issues

### Architecture Principles
1. **Incremental Development**: Start simple, expand gradually
2. **Modular Design**: Each test category is a separate module
3. **Extensibility**: Easy to add new tests
4. **Clear Reporting**: Human-readable output with actionable insights
5. **CI/CD Ready**: Can be integrated into automated testing pipelines

## Development Phases

### Phase 1: Foundation ✅ (Completed - February 3, 2026)
**Goal**: Basic project structure and connection testing

- [x] Initialize uv project structure
- [x] Create CLI entry point with argument parsing
- [x] Implement basic connection test
- [x] Design test framework architecture
- [x] Create simple report generator
- [x] Document setup and usage
- [x] Unit tests with pytest
- [x] Code formatting and linting setup

**Deliverables**: ✅ ALL COMPLETED
- ✅ Working CLI that accepts connection string
- ✅ Successful/failed connection test with diagnostics
- ✅ Rich text report output with colors
- ✅ JSON export capability
- ✅ Comprehensive documentation
- ✅ Unit test coverage at 37%

**Files Created**:
- `src/odbc_crusher/cli.py` - CLI interface with Click
- `src/odbc_crusher/connection.py` - Connection testing
- `src/odbc_crusher/test_runner.py` - Test execution engine
- `src/odbc_crusher/reporter.py` - Rich-formatted reporting
- `src/odbc_crusher/tests/base.py` - Base test classes
- `src/odbc_crusher/tests/connection_tests.py` - Connection tests
- `tests/test_connection.py` - Unit tests for connection
- `tests/test_base.py` - Unit tests for test framework
- `PROJECT_PLAN.md` - This roadmap
- `AGENT_INSTRUCTIONS.md` - AI agent guidelines
- `README.md` - Full documentation
- `docs/GETTING_STARTED.md` - Setup guide
- `QUICKSTART.md` - Quick reference
- `CHANGELOG.md` - Version history

### Phase 2: Core ODBC Functions (IN PROGRESS - Started Feb 3, 2026)
**Goal**: Test fundamental ODBC API functions

**Focus Areas**:
- Environment handle functions (`SQLAllocHandle`, `SQLFreeHandle`)
- Connection functions (`SQLConnect`, `SQLDriverConnect`, `SQLDisconnect`)
- Statement functions (`SQLAllocStmt`, `SQLFreeStmt`)
- Simple query execution (`SQLExecDirect`)

**Tests Implemented**: ✅ IN PROGRESS
- [x] Handle allocation/deallocation (HandleTests)
- [x] Statement handle creation and reuse
- [x] Simple query execution (StatementTests)
- [x] Result set fetching
- [x] Empty result set handling
- [x] Multiple sequential statements
- [ ] Error diagnostics (SQLGetDiagRec)
- [ ] Transaction testing

**Files Created**:
- `src/odbc_crusher/tests/handle_tests.py` - Handle management (4 tests)
- `src/odbc_crusher/tests/statement_tests.py` - Query execution (4 tests)

**Bugs Discovered**:
- Firebird ODBC: False "file not found" error when file exists

### Phase 3: Data Retrieval Functions (COMPLETED ✅ - Feb 3, 2026)
**Goal**: Test query execution and result fetching

**Note**: Phase 3 was merged with Phase 2 - query execution tests were already implemented in StatementTests.

**Implemented in StatementTests**:
- [x] Statement execution (`SQLExecDirect`)
- [x] Result set functions (`SQLFetch`)
- [x] Data retrieval (`SQLGetData`)
- [x] Handle empty result sets
- [x] Multiple sequential queries

### Phase 4: Metadata Functions (COMPLETED ✅ - Feb 3, 2026)
**Goal**: Test catalog and schema information functions

**Focus Areas**:
- `SQLTables`
- `SQLColumns`
- `SQLPrimaryKeys`
- `SQLForeignKeys`
- `SQLStatistics`
- `SQLGetTypeInfo`

**Tests Implemented**: ✅ ALL COMPLETED
- [x] SQLTables - List tables in database
- [x] SQLColumns - Get column metadata  
- [x] SQLPrimaryKeys - Primary key information
- [x] SQLForeignKeys - Foreign key relationships
- [x] SQLStatistics - Index and statistics
- [x] SQLGetTypeInfo - Data type catalog

**Files Created**:
- `src/odbc_crusher/tests/metadata_tests.py` - Metadata catalog tests (6 tests)

**Test Results**: 6/6 metadata tests passing ✅

**Notes**:
- All metadata functions properly implemented in Firebird ODBC driver
- pyodbc exposes all necessary catalog functions
- Graceful handling of empty results

### Phase 5: Advanced Features (COMPLETED ✅ - Feb 3, 2026)
**Goal**: Test advanced ODBC capabilities

**Focus Areas**:
- Transactions (`SQLEndTran`, `SQLSetConnectAttr` with isolation levels)
- Prepared statements with parameters (`SQLBindParameter`)
- Batch operations
- Asynchronous execution
- Cursor types and scrolling
- Bulk operations

**Tests Implemented**: ✅ ALL COMPLETED
- [x] Autocommit mode detection
- [x] Manual transaction commit
- [x] Transaction rollback
- [x] Prepared statement execution
- [x] Parameter binding (multiple data types)
- [x] Multiple result set detection

**Files Created**:
- `src/odbc_crusher/tests/advanced_tests.py` - Advanced features (6 tests)

**Test Results**: 6/6 advanced tests passing

**CRITICAL LEARNING**: Database-specific parameter syntax requirements
- Firebird requires: `SELECT CAST(? AS type) FROM RDB$DATABASE`
- MySQL accepts: `SELECT ?`
- False bug reports corrected after validation across multiple drivers

### Phase 6: Data Type Testing (COMPLETED ✅ - Feb 3, 2026)
**Goal**: Comprehensive testing of SQL data types

**Focus Areas**:
- Numeric types (INTEGER, BIGINT, DECIMAL, NUMERIC, FLOAT, REAL)
- Character types (CHAR, VARCHAR, TEXT)
- Date/time types (DATE, TIME, TIMESTAMP)
- Binary types (BINARY, VARBINARY, BLOB)
- Special types (BOOLEAN, UUID, JSON, XML)
- Edge cases (MIN/MAX values, precision, scale)

**Tests Implemented**: ✅ 8 DATA TYPE TESTS
- [x] Integer types (SMALLINT, INTEGER, BIGINT)
- [x] Decimal types (DECIMAL, NUMERIC)
- [x] Float types (FLOAT, DOUBLE PRECISION, REAL)
- [x] Character types (VARCHAR, CHAR)
- [x] DATE type
- [x] TIME type
- [x] TIMESTAMP type
- [x] Binary types (VARBINARY, BINARY) - optional

**Files Created**:
- `src/odbc_crusher/tests/datatype_tests.py` - Data type tests (8 tests)

**Test Results**: 8/8 data type tests created
- Firebird: 7/8 PASS (1 skip - binary types)
- MySQL: 6/8 PASS (2 fail - syntax differences)

**Key Findings**:
- Binary type support varies widely (commonly skipped)
- Firebird requires FROM clause in CAST expressions
- MySQL more flexible with standalone CAST
- Date/time types well-supported across drivers

### Phase 6.5: Driver Capability Detection (COMPLETED ✅ - Feb 4, 2026)
**Goal**: Use ODBC specification functions to discover driver capabilities - STOP GUESSING!

**Critical Realization**: Instead of guessing driver features from DLL names or catching exceptions, use the ODBC-specified discovery functions: `SQLGetInfo`, `SQLGetTypeInfo`, and `SQLGetFunctions`.

**Implementation**: ✅ ALL THREE ODBC DISCOVERY FUNCTIONS

**1. SQLGetInfo** - Driver/DBMS Information
- [x] Driver name, version, ODBC version
- [x] DBMS name and version
- [x] SQL-92 conformance level
- [x] ODBC interface conformance
- [x] Catalog/schema terminology
- [x] SQL limits (max column name, max tables in SELECT, etc.)
- [x] Feature flags (procedures, outer joins, transactions)
- [x] 40+ driver properties collected
- Method: `conn.getinfo(pyodbc.SQL_*)` - PyODBC exposes this
- Challenge: Not all SQL_* constants available in pyodbc - use `hasattr()` checks

**2. SQLGetTypeInfo** - Supported Data Types
- [x] Query all data types supported by driver
- [x] Get type name, SQL type code, precision, scale
- [x] Nullable, case-sensitive, searchable flags
- [x] Auto-increment capability
- Method: `cursor.getTypeInfo(0)` - PyODBC exposes this
- Challenge: Access result by index (not by attribute name)
- Firebird Result: 22 data types detected

**3. SQLGetFunctions** - Implemented ODBC Functions
- [x] Direct ODBC API call using ctypes and odbc32.dll
- [x] Allocate ODBC environment and connection handles
- [x] Connect via SQLDriverConnectA
- [x] Call SQLGetFunctions with SQL_API_ODBC3_ALL_FUNCTIONS (999)
- [x] Parse 250-element bitmap (4000 bits) using SQL_FUNC_EXISTS macro
- [x] Test 64 important ODBC functions
- Method: **NO PyODBC wrapper - direct ctypes call to Windows ODBC Driver Manager**
- Challenge: PyODBC doesn't expose SQLGetFunctions - implemented using ctypes
- Firebird Result: **49/64 ODBC functions supported** (all 10 catalog functions ✅)

**Refactored Tests**:
- [x] `DriverCapabilityTests` - Replaced DLL inspection with SQLGetInfo
- [x] `test_driver_info` - Reports driver/DBMS using SQLGetInfo
- [x] `test_sql_conformance` - SQL-92 and ODBC interface conformance levels
- [x] `test_supported_features` - Feature detection via info flags (not guessing!)
- [x] `test_unicode_capability` - Still does actual Unicode round-trip testing

**Files Created**:
- `src/odbc_crusher/driver_info.py` - Driver information collection module
- `tmp/test_driver_info.py` - Standalone test script

**Files Modified**:
- `src/odbc_crusher/tests/capability_tests.py` - Major refactoring
- `src/odbc_crusher/cli.py` - Integrated driver info display

**Test Results**: 35 tests total (was 34)
- Firebird: All tests passing ✅
- Driver info displayed before test execution

**Key Learnings**:

1. **PyODBC Limitations**:
   - Not all SQL_* constants exposed (e.g., no SQL_OUTER_JOINS)
   - SQL_DRIVER_HDBC not available (used numeric value 3)
   - No SQLGetFunctions wrapper - had to use ctypes
   - Must use `hasattr(pyodbc, 'SQL_*')` before accessing constants

2. **ODBC API via ctypes** (Windows):
   - Load odbc32.dll: `ctypes.windll.odbc32`
   - Allocate handles: `SQLAllocHandle(type, parent, &handle)`
   - Connect: `SQLDriverConnectA(hdbc, hwnd, connStr, len, ...)`
   - Query: `SQLGetFunctions(hdbc, functionId, &bitmap)`
   - Clean up: `SQLDisconnect()`, `SQLFreeHandle()`
   - This works PERFECTLY when PyODBC doesn't expose functionality!

3. **Before vs After**:
   ```python
   # BEFORE (Guessing):
   if 'w.dll' in driver_name.lower():
       has_unicode = True
   try:
       cursor.execute(query)
   except:
       fallback_query()
   
   # AFTER (Asking the driver):
   outer_joins = conn.getinfo(pyodbc.SQL_OJ_CAPABILITIES)
   if outer_joins & pyodbc.SQL_OJ_LEFT:
       supports_left_join = True
   
   datatypes = cursor.getTypeInfo(0)  # All types
   for dt in datatypes:
       type_name = dt[0]  # TYPE_NAME column
   ```

4. **Firebird Driver Capabilities** (discovered via ODBC functions):
   - Driver: FirebirdODBC v03.00.0021, ODBC 3.51
   - DBMS: Firebird v06.03.1613 (Firebird 5.0)
   - SQL Conformance: Custom level 8
   - ODBC Interface: Level 2 ✅
   - Outer Joins: LEFT, RIGHT, FULL ✅
   - Transactions: DDL causes commit
   - 49/64 ODBC functions implemented
   - All catalog functions present ✅
   - 22 data types supported

**Documentation**:
- Microsoft ODBC API references used:
  - [SQLGetInfo Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlgetinfo-function)
  - [SQLGetTypeInfo Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlgettypeinfo-function)
  - [SQLGetFunctions Function](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/sqlgetfunctions-function)

**Impact**: This is a FUNDAMENTAL improvement - we now ask the driver what it supports instead of guessing. No more false bug reports from incorrect assumptions!

### Phase 7: Performance & Compliance (NEXT)
**Goal**: Test performance characteristics and SQL compliance

**Focus Areas**:
- Connection pooling behavior
- Statement caching
- Large result sets
- SQL conformance levels
- ODBC version compliance (2.x vs 3.x)

### Phase 8: Enhanced Reporting
**Goal**: Multiple output formats and detailed diagnostics

**Deliverables**:
- JSON output format
- HTML report with styling
- XML output
- CSV export
- Severity levels and filtering
- Historical comparison

## Technical Architecture

### Project Structure
```
odbc-crusher/
├── src/
│   └── odbc_crusher/
│       ├── __init__.py
│       ├── cli.py              # CLI entry point
│       ├── connection.py       # Connection management
│       ├── test_runner.py      # Test execution engine
│       ├── reporter.py         # Report generation
│       ├── tests/              # Test modules
│       │   ├── __init__.py
│       │   ├── base.py         # Base test class
│       │   ├── connection.py   # Connection tests
│       │   ├── handles.py      # Handle management tests
│       │   ├── queries.py      # Query execution tests
│       │   ├── metadata.py     # Metadata tests
│       │   └── datatypes.py    # Data type tests
│       └── utils/
│           ├── __init__.py
│           ├── odbc_helpers.py # ODBC utility functions
│           └── validators.py   # Result validators
├── tests/                      # Unit tests for the tool itself
├── docs/
│   ├── GETTING_STARTED.md
│   ├── ADDING_TESTS.md
│   └── ODBC_REFERENCE.md
├── PROJECT_PLAN.md             # This file
├── AGENT_INSTRUCTIONS.md       # Instructions for AI agents
├── pyproject.toml              # uv project configuration
└── README.md
```

### Test Framework Design

**Base Test Class**:
```python
class ODBCTest:
    def __init__(self, connection_string):
        self.connection_string = connection_string
        self.results = []
    
    def run(self):
        """Execute all test methods"""
        pass
    
    def report(self):
        """Return test results"""
        pass
```

**Test Result Structure**:
```python
{
    "test_name": "SQLConnect",
    "status": "PASS|FAIL|SKIP|ERROR",
    "expected": "Connection successful",
    "actual": "Connection failed: [HY000] Authentication failed",
    "diagnostic": "Check username and password in connection string",
    "severity": "CRITICAL|ERROR|WARNING|INFO"
}
```

## Python Dependencies

### Required Libraries
- **pyodbc**: Primary ODBC interface for Python
- **click** or **typer**: CLI framework
- **rich**: Terminal output formatting and colors
- **pydantic**: Data validation and settings
- **tabulate**: Table formatting for reports

### Development Dependencies
- **pytest**: Testing framework
- **pytest-cov**: CovCOMPLETED ✅  
**Version**: 0.1.0  
**Last Milestone**: Full Phase 1 implementation with tests, documentation, and working CLI  
**Next Milestone**: Phase 2 - Core ODBC Functions (handle management, statements, errors)

**Ready to Use**: Yes! The tool is functional and can test basic ODBC connections.

**Test the tool now**:
```bash
uv run odbc-crusher "DSN=YourDSN" --verbose
```

## References

- [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
- [pyodbc Documentation](https://github.com/mkleehammer/pyodbc/wiki)

## Success Metrics

1. **Coverage**: Test at least 80% of commonly used ODBC functions
2. **Accuracy**: Correctly identify known issues in test drivers
3. **Usability**: Clear, actionable reports that help developers
4. **Performance**: Complete test suite runs in < 5 minutes
5. **Extensibility**: Adding a new test takes < 30 minutes

## Current Status

**Phase**: Phase 6.5 - Driver Capability Detection ✅ COMPLETED  
**Version**: 0.5.0  
**Last Milestone**: Implementation of SQLGetInfo, SQLGetTypeInfo, and SQLGetFunctions via ODBC API  
**Next Milestone**: Phase 7 - Performance & Compliance Testing  

**Total Tests**: 35 (connection, handles, statements, metadata, advanced, datatypes, capabilities)  
**Test Coverage**: Core ODBC functionality comprehensively tested  
**Driver Info**: Automatically collected and displayed before test execution

## Known Limitations & Future Ideas

- Currently only supports Windows ODBC drivers (can expand to unixODBC)
- No parallel test execution yet
- No configuration file support (all via CLI args)
- No test filtering/selection mechanism
- Future: Web UI for report viewing
- Future: Integration with CI/CD systems
- Future: Database-specific test suites
- Future: Performance benchmarking mode

---

**Important**: Keep this plan updated as the project evolves. Every significant change should be reflected here.
