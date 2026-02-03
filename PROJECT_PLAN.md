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

### Phase 5: Advanced Features (Next)
**Goal**: Test advanced ODBC capabilities

**Focus Areas**:
- Transactions (`SQLEndTran`, `SQLSetConnectAttr` with isolation levels)
- Prepared statements with parameters (`SQLBindParameter`)
- Batch operations
- Asynchronous execution
- Cursor types and scrolling
- Bulk operations

### Phase 6: Data Type Testing
**Goal**: Comprehensive testing of SQL data types

**Focus Areas**:
- Numeric types (INTEGER, BIGINT, DECIMAL, NUMERIC, FLOAT, REAL)
- Character types (CHAR, VARCHAR, TEXT)
- Date/time types (DATE, TIME, TIMESTAMP)
- Binary types (BINARY, VARBINARY, BLOB)
- Special types (BOOLEAN, UUID, JSON, XML)
- Edge cases (MIN/MAX values, precision, scale)

### Phase 7: Performance & Compliance
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

**Phase**: Phase 1 - Foundation  
**Version**: 0.1.0-dev  
**Last Milestone**: Project initialization  
**Next Milestone**: First working CLI with connection test

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
