# Mock ODBC Driver - Development Plan

**Project**: Mock ODBC Driver for Testing  
**Purpose**: A configurable ODBC driver that simulates database behavior without actual database connections  
**Repository**: https://github.com/fdcastel/odbc-crusher (separate component)  
**Status**: Planning Phase  
**Version**: 1.0.0 (planned)

---

## 🎯 Project Overview

### Vision

Create a fully functional **Mock ODBC Driver** that implements the ODBC 3.x specification without connecting to any real database. The driver will provide configurable behaviors via connection string parameters, enabling comprehensive testing of ODBC applications including error conditions, edge cases, and various driver capabilities.

### Key Goals

1. **Complete ODBC 3.x Implementation**: Implement all required ODBC functions per specification
2. **Configurable Behavior**: Control driver behavior via connection string parameters
3. **Testing Focus**: Enable testing of both "happy path" and error scenarios
4. **Zero Dependencies**: No database server or network required
5. **Cross-Platform**: Works on Windows, Linux, and macOS
6. **Specification Compliant**: Follows Microsoft ODBC specification exactly

### Use Cases

- **ODBC Crusher Testing**: Run all tests without requiring database installations
- **Application Testing**: Test ODBC error handling in applications
- **Driver Development**: Reference implementation for ODBC driver developers
- **Education**: Learning tool for understanding ODBC behavior
- **CI/CD**: Automated testing in environments without databases

---

## 📚 ODBC Specification Overview

### ODBC Architecture Levels

Based on Microsoft ODBC specification, drivers must support:

#### **Core Functions (Level 1)** - REQUIRED
Minimum conformance level - all drivers must implement these:

**Connection Functions**:
- `SQLAllocHandle` - Allocate environment, connection, statement handles
- `SQLFreeHandle` - Free handles
- `SQLConnect` - Simple connection with DSN
- `SQLDisconnect` - Disconnect
- `SQLGetConnectAttr` / `SQLSetConnectAttr` - Connection attributes
- `SQLGetEnvAttr` / `SQLSetEnvAttr` - Environment attributes

**Statement Functions**:
- `SQLExecDirect` - Execute SQL statement
- `SQLPrepare` / `SQLExecute` - Prepare and execute
- `SQLFetch` - Fetch next row
- `SQLGetData` - Retrieve column data
- `SQLNumResultCols` - Number of columns in result
- `SQLDescribeCol` - Describe a result column
- `SQLRowCount` - Number of affected rows
- `SQLCloseCursor` - Close cursor
- `SQLFreeStmt` - Free statement handle (deprecated, use SQLFreeHandle)
- `SQLGetStmtAttr` / `SQLSetStmtAttr` - Statement attributes

**Diagnostic Functions**:
- `SQLGetDiagRec` - Get diagnostic record
- `SQLGetDiagField` - Get diagnostic field

**Catalog Functions** (Basic):
- `SQLTables` - List tables
- `SQLColumns` - List columns

**Transaction Functions**:
- `SQLEndTran` - Commit or rollback

**Descriptor Functions**:
- `SQLGetDescField` / `SQLSetDescField` - Descriptor field access
- `SQLGetDescRec` / `SQLSetDescRec` - Descriptor record access
- `SQLCopyDesc` - Copy descriptor

**Information Functions**:
- `SQLGetInfo` - Driver/DBMS information
- `SQLGetTypeInfo` - Data type information
- `SQLGetFunctions` - Supported functions

#### **Level 1 Functions** - RECOMMENDED
Extended functionality for better application support:

**Connection**:
- `SQLDriverConnect` - Connect with connection string
- `SQLBrowseConnect` - Interactive connection

**Statement**:
- `SQLBindParameter` - Bind parameter for prepared statements
- `SQLBindCol` - Bind result column to buffer
- `SQLMoreResults` - Multiple result sets
- `SQLParamData` / `SQLPutData` - Long data handling

**Catalog Functions**:
- `SQLPrimaryKeys` - Primary key columns
- `SQLForeignKeys` - Foreign key relationships
- `SQLStatistics` - Index and statistics
- `SQLSpecialColumns` - Special columns (ROWID, etc.)

**Advanced Statement**:
- `SQLNumParams` - Number of parameters
- `SQLDescribeParam` - Describe parameter

#### **Level 2 Functions** - OPTIONAL
Advanced features:

**Cursor Operations**:
- `SQLExtendedFetch` - Extended fetch with scrolling
- `SQLSetPos` - Position cursor and update/delete
- `SQLBulkOperations` - Bulk insert/update/delete/bookmark

**Catalog Functions (Extended)**:
- `SQLProcedures` / `SQLProcedureColumns` - Stored procedures
- `SQLTablePrivileges` / `SQLColumnPrivileges` - Privileges

**Asynchronous Operations**:
- Async execution support via statement attributes

**Positioned Operations**:
- Positioned UPDATE/DELETE with cursors

---

## 🏗️ Architecture Design

### Component Structure

```
mock-odbc-driver/
├── CMakeLists.txt                 # Build system
├── README.md                      # Driver documentation
├── LICENSE                        # MIT License
├── src/
│   ├── driver/                    # Core driver implementation
│   │   ├── driver_main.cpp        # DLL entry point (Windows) / shared lib (Unix)
│   │   ├── handles.hpp/cpp        # Handle management (ENV, DBC, STMT, DESC)
│   │   ├── connection.hpp/cpp     # Connection implementation
│   │   ├── statement.hpp/cpp      # Statement implementation
│   │   ├── descriptor.hpp/cpp     # Descriptor implementation
│   │   ├── environment.hpp/cpp    # Environment implementation
│   │   ├── diagnostics.hpp/cpp    # Diagnostic record management
│   │   └── config.hpp/cpp         # Connection string parsing
│   ├── mock/                      # Mock data and behaviors
│   │   ├── mock_catalog.hpp/cpp   # Mock table/column metadata
│   │   ├── mock_types.hpp/cpp     # Mock data types
│   │   ├── mock_data.hpp/cpp      # Mock result sets
│   │   └── behaviors.hpp/cpp      # Configurable behaviors
│   ├── odbc/                      # ODBC API implementations
│   │   ├── connection_api.cpp     # SQLConnect, SQLDriverConnect, etc.
│   │   ├── statement_api.cpp      # SQLExecDirect, SQLPrepare, etc.
│   │   ├── catalog_api.cpp        # SQLTables, SQLColumns, etc.
│   │   ├── descriptor_api.cpp     # SQLGetDescField, etc.
│   │   ├── info_api.cpp           # SQLGetInfo, SQLGetTypeInfo, etc.
│   │   ├── transaction_api.cpp    # SQLEndTran
│   │   └── diagnostic_api.cpp     # SQLGetDiagRec, SQLGetDiagField
│   └── utils/
│       ├── string_utils.hpp/cpp   # String conversions (ANSI/Unicode)
│       └── validation.hpp/cpp     # Parameter validation
├── tests/
│   ├── unit/                      # Unit tests for driver components
│   └── integration/               # Integration tests with ODBC Crusher
└── install/
    ├── odbcinst.ini.template      # Linux/macOS driver registration
    └── windows_register.reg       # Windows registry template
```

### Handle Management

All ODBC handles inherit from a base class:

```cpp
enum class HandleType {
    ENV,    // Environment (HENV)
    DBC,    // Connection (HDBC)
    STMT,   // Statement (HSTMT)
    DESC    // Descriptor (HDESC)
};

class OdbcHandle {
protected:
    HandleType type_;
    std::vector<DiagnosticRecord> diagnostics_;
    
public:
    HandleType type() const { return type_; }
    void add_diagnostic(DiagnosticRecord diag);
    // ...
};

class EnvironmentHandle : public OdbcHandle { /* ... */ };
class ConnectionHandle : public OdbcHandle { /* ... */ };
class StatementHandle : public OdbcHandle { /* ... */ };
class DescriptorHandle : public OdbcHandle { /* ... */ };
```

---

## ⚙️ Configuration via Connection String

### Configuration Parameters

The driver behavior is controlled via key-value pairs in the connection string:

```
Driver={Mock ODBC Driver};
Mode=Success;
Catalog=Default;
Types=AllTypes;
ResultSetSize=100;
FailOn=SQLExecute;
ErrorCode=42000;
Latency=10ms;
MaxConnections=1;
```

### Configuration Keys

#### **Mode** (Behavior Preset)
- `Success` (default) - All operations succeed
- `Failure` - Operations fail with errors
- `Random` - Randomly succeed/fail (for stress testing)
- `Partial` - Some operations succeed, others fail based on FailOn

#### **Catalog** (Mock Schema)
- `Default` - Standard set of tables (USERS, ORDERS, PRODUCTS)
- `Empty` - No tables
- `Large` - Many tables/columns (performance testing)
- `Custom:JSON_FILE` - Load custom catalog from JSON

#### **Types** (Data Types Supported)
- `AllTypes` - All SQL types
- `BasicTypes` - Only INTEGER, VARCHAR, DATE
- `NumericOnly` - Numeric types only
- `Custom:TypeList` - Comma-separated list

#### **ResultSetSize** 
- Number of rows to return (default: 100)
- `0` for empty result sets

#### **FailOn** (Inject Failures)
- Comma-separated list of functions to fail
- Examples: `SQLExecute`, `SQLFetch`, `SQLPrepare`

#### **ErrorCode** (SQLSTATE to return on failure)
- Default: `42000` (Syntax error)
- Examples: `08001` (Connection error), `23000` (Integrity constraint)

#### **Latency** (Simulated Network Delay)
- Time to delay before returning from functions
- Format: `{number}ms` or `{number}us`
- Examples: `10ms`, `500us`

#### **MaxConnections**
- Maximum simultaneous connections (default: unlimited)
- Test connection pooling behavior

#### **TransactionMode**
- `Autocommit` - Always autocommit (default)
- `Manual` - Require explicit commit
- `ReadOnly` - Reject modifications

#### **IsolationLevel**
- `ReadUncommitted`, `ReadCommitted`, `RepeatableRead`, `Serializable`

---

## 🗃️ Mock Data Design

### Mock Catalog (Default Schema)

The driver provides a built-in catalog with realistic tables:

```sql
-- Table: USERS
Columns:
  - USER_ID (INTEGER, Primary Key, Auto-increment)
  - USERNAME (VARCHAR(50), Unique)
  - EMAIL (VARCHAR(100))
  - CREATED_DATE (DATE)
  - IS_ACTIVE (BOOLEAN)
  - BALANCE (DECIMAL(10,2))

-- Table: ORDERS
Columns:
  - ORDER_ID (INTEGER, Primary Key)
  - USER_ID (INTEGER, Foreign Key -> USERS.USER_ID)
  - ORDER_DATE (TIMESTAMP)
  - TOTAL_AMOUNT (DECIMAL(10,2))
  - STATUS (VARCHAR(20))

-- Table: PRODUCTS  
Columns:
  - PRODUCT_ID (INTEGER, Primary Key)
  - NAME (VARCHAR(100))
  - DESCRIPTION (TEXT)
  - PRICE (DECIMAL(10,2))
  - STOCK_QUANTITY (INTEGER)
  - CATEGORY (VARCHAR(50))

-- Table: ORDER_ITEMS
Columns:
  - ORDER_ITEM_ID (INTEGER, Primary Key)
  - ORDER_ID (INTEGER, Foreign Key -> ORDERS.ORDER_ID)
  - PRODUCT_ID (INTEGER, Foreign Key -> PRODUCTS.PRODUCT_ID)
  - QUANTITY (INTEGER)
  - UNIT_PRICE (DECIMAL(10,2))
```

### Mock Data Generation

- **Deterministic**: Same query always returns same results (unless configured otherwise)
- **Realistic Values**: Names, emails, dates look realistic
- **Referential Integrity**: Foreign keys reference valid IDs
- **Configurable Size**: Control number of rows via ResultSetSize

### Supported SQL Subset

The driver will parse and handle basic SQL:

**Supported**:
- `SELECT * FROM table`
- `SELECT col1, col2 FROM table WHERE condition`
- `INSERT INTO table VALUES (...)`
- `UPDATE table SET col=val WHERE condition`
- `DELETE FROM table WHERE condition`
- `SELECT COUNT(*) FROM table`
- Simple WHERE clauses (=, <, >, LIKE)
- ORDER BY, LIMIT

**Not Supported** (returns empty result or error):
- JOINs (can be added later)
- Complex expressions
- Subqueries
- Window functions

---

## 📋 Implementation Phases

### Phase 0: Project Setup (Week 1) ✅ COMPLETED
**Goal**: Create project structure and build system

**Tasks**:
- [x] Create separate repository or subdirectory
- [x] CMake build system for shared library (.dll/.so/.dylib)
- [x] Basic README with driver purpose
- [x] License (MIT)
- [x] .gitignore for build artifacts
- [x] Basic CI setup (build only)

**Deliverables**:
- Compilable empty driver library ✅
- CMake configuration for Windows/Linux/macOS ✅
- Project documentation structure ✅
- 29 unit tests passing ✅

---

### Phase 1: Core Handle Management (Week 1-2) ✅ COMPLETED
**Goal**: Implement ODBC handle lifecycle

**ODBC Functions**:
- `SQLAllocHandle` (ENV, DBC, STMT, DESC) ✅
- `SQLFreeHandle` ✅
- `SQLGetEnvAttr` / `SQLSetEnvAttr` ✅
- `SQLGetDiagRec` / `SQLGetDiagField` ✅

**Components**:
- Handle base class and derived classes ✅
- Handle validation and type checking ✅
- Diagnostic record management ✅
- Thread-safe handle allocation ✅

**Tests**: 16 tests passing
- Allocate/free all handle types ✅
- Invalid handle operations ✅
- Diagnostic retrieval ✅
- Memory leak checks ✅

---

### Phase 2: Connection Management (Week 2-3)
**Goal**: Implement connection lifecycle

**ODBC Functions**:
- `SQLDriverConnect`
- `SQLConnect`
- `SQLDisconnect`
- `SQLGetConnectAttr` / `SQLSetConnectAttr`
- `SQLGetInfo`

**Components**:
- Connection string parsing
- Configuration parameter handling
- Connection attributes (autocommit, timeout, etc.)
- SQLGetInfo information retrieval (50+ info types)

**Tests**:
- Connect with various connection strings
- Connection attribute get/set
- Multiple connections
- Disconnect scenarios
- Invalid connection strings

---

### Phase 3: Basic Statement Execution (Week 3-4)
**Goal**: Execute simple queries and return results

**ODBC Functions**:
- `SQLExecDirect`
- `SQLNumResultCols`
- `SQLDescribeCol`
- `SQLFetch`
- `SQLGetData`
- `SQLCloseCursor`
- `SQLGetStmtAttr` / `SQLSetStmtAttr`

**Components**:
- Simple SQL parser (SELECT, INSERT, UPDATE, DELETE)
- Mock result set generation
- Column metadata
- Data type conversions (SQL to C types)

**Tests**:
- SELECT from mock tables
- Fetch all rows
- Various data types
- Empty result sets
- Error cases

---

### Phase 4: Prepared Statements & Parameters (Week 4-5)
**Goal**: Implement prepared statements and parameter binding

**ODBC Functions**:
- `SQLPrepare`
- `SQLExecute`
- `SQLBindParameter`
- `SQLNumParams`
- `SQLDescribeParam`

**Components**:
- Parameter marker parsing (?)
- Parameter binding and storage
- Type conversions (C to SQL types)
- Parameter validation

**Tests**:
- Prepare and execute with parameters
- Multiple parameter types
- NULL parameters
- Parameter binding errors

---

### Phase 5: Catalog Functions (Week 5-6)
**Goal**: Implement metadata retrieval functions

**ODBC Functions**:
- `SQLTables`
- `SQLColumns`
- `SQLPrimaryKeys`
- `SQLForeignKeys`
- `SQLStatistics`
- `SQLSpecialColumns`
- `SQLGetTypeInfo`

**Components**:
- Mock catalog metadata
- Filter patterns (table name, column name, etc.)
- Catalog result set formatting per ODBC spec

**Tests**:
- List all tables
- Get columns for specific table
- Primary/foreign key retrieval
- Pattern matching (wildcards)

---

### Phase 6: Transactions (Week 6)
**Goal**: Implement transaction control

**ODBC Functions**:
- `SQLEndTran` (commit/rollback)
- Transaction isolation levels

**Components**:
- Transaction state tracking
- Autocommit mode handling
- Rollback behavior (clear pending changes)

**Tests**:
- Autocommit on/off
- Manual commit
- Rollback
- Transaction isolation

---

### Phase 7: Advanced Features (Week 7-8)
**Goal**: Implement advanced ODBC features

**ODBC Functions**:
- `SQLBindCol`
- `SQLRowCount`
- `SQLMoreResults`
- `SQLGetFunctions`
- `SQLSetPos` (optional)
- `SQLBulkOperations` (optional)

**Components**:
- Column binding
- Affected row count
- Multiple result sets
- Function availability bitmap

**Tests**:
- Bound columns vs SQLGetData
- Row count for INSERT/UPDATE/DELETE
- Multiple result sets
- Batch operations

---

### Phase 8: Error Injection & Edge Cases (Week 8-9)
**Goal**: Configurable error scenarios

**Features**:
- FailOn parameter implementation
- Random failure mode
- Various SQLSTATE codes
- Timeout simulation
- Connection limit enforcement

**Tests**:
- Inject errors at specific functions
- Handle all SQLSTATE codes
- Timeout behavior
- Connection pool limits

---

### Phase 9: Integration with ODBC Crusher (Week 9-10)
**Goal**: Replace real database connections in tests

**Tasks**:
- [x] Create driver registration scripts for Windows
- [x] Create test configuration for ODBC Crusher  
- [x] Register mock driver with ODBC Driver Manager
- [x] **Fix critical dynamic_cast crash** ✅ (MAJOR FIX!)
- [ ] Fix catalog function crashes (SQLGetTypeInfo, etc.)
- [ ] Verify all 31 tests pass with Mock Driver
- [ ] Add new tests for error conditions
- [ ] Update CI to use Mock Driver

**Status**: **IN PROGRESS** (61% tests passing - major progress!)
- **19 of 31 tests PASS** (was 0 before dynamic_cast fix)
- 12 tests still crash in catalog functions
- Root cause: dynamic_cast across DLL boundary is undefined behavior
- Fix: Manual type checking using HandleType enum
- See commit 782a82d for details

**Benefits**:
- Tests run anywhere without database installations
- Faster test execution
- More comprehensive error testing
- Easier CI/CD integration

---

### Phase 10: Polish & Documentation (Week 10-11)
**Goal**: Production-ready driver

**Tasks**:
- [ ] Comprehensive documentation
- [ ] Installation instructions (Windows registry, odbcinst.ini)
- [ ] Performance optimization
- [ ] Memory leak testing (Valgrind, Dr. Memory)
- [ ] Thread safety audit
- [ ] Example applications
- [ ] User guide with configuration examples

---

## 🧪 Testing Strategy

### Unit Tests (Per Phase)
- Test each ODBC function independently
- Mock handle creation/destruction
- Parameter validation
- Error condition handling

### Integration Tests
- Full ODBC workflows (connect → execute → fetch → disconnect)
- Multiple simultaneous connections
- Prepared statement lifecycle
- Transaction scenarios

### Compliance Tests
- Run against ODBC Crusher test suite
- Verify all 34 tests pass
- Test error scenarios

### Performance Tests
- Large result sets (10,000+ rows)
- Many connections (100+)
- Rapid connect/disconnect cycles

### Memory Tests
- Valgrind on Linux
- AddressSanitizer
- Dr. Memory on Windows
- No leaks in any scenario

---

## 🎯 Success Criteria

### Minimum Viable Product (MVP)
- ✅ All Core Functions (Level 1) implemented
- ✅ Basic catalog (3-5 tables)
- ✅ Connection string configuration
- ✅ ODBC Crusher tests pass
- ✅ Windows + Linux support

### Full Release (v1.0)
- ✅ All Level 1 + Level 2 functions
- ✅ Complete mock catalog
- ✅ Error injection
- ✅ All platforms (Windows, Linux, macOS)
- ✅ Comprehensive documentation
- ✅ Zero memory leaks
- ✅ Performance: <1ms per operation

---

## 📦 Deliverables

### Code
- Mock ODBC Driver shared library
- Header files (if exposing internal APIs)
- Build scripts (CMake)
- Installation scripts

### Documentation
- Driver User Guide
- Configuration Reference
- Developer Guide (for contributors)
- API Documentation (Doxygen)

### Tests
- Unit test suite (100+ tests)
- Integration tests with ODBC Crusher
- Performance benchmarks

### Distribution
- GitHub Releases with binaries
- Installation packages
  - Windows: MSI installer
  - Linux: DEB/RPM packages
  - macOS: Homebrew formula

---

## 🔗 References

### ODBC Specification
- [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
- [Developing an ODBC Driver](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-driver/developing-an-odbc-driver)
- [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
- [ODBC Function Conformance](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/function-conformance)

### Implementation Examples
- [SQLite ODBC Driver](https://github.com/gkoberger/SQLiteODBC) - Simple reference
- [PostgreSQL ODBC Driver](https://github.com/postgresql-interfaces/psqlodbc) - Full-featured example
- [unixODBC](https://github.com/lurcher/unixODBC) - Driver Manager source

### Testing
- [ODBC Test Suite](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-app/odbc-test)
- ODBC Crusher C++ (this project)

---

## 📊 Timeline Estimate

**Total Duration**: 10-11 weeks (part-time development)

| Phase | Duration | Focus |
|-------|----------|-------|
| Phase 0 | 1 week | Setup |
| Phase 1 | 1 week | Handles |
| Phase 2 | 1 week | Connection |
| Phase 3 | 1 week | Basic SQL |
| Phase 4 | 1 week | Prepared Statements |
| Phase 5 | 1 week | Catalog |
| Phase 6 | 1 week | Transactions |
| Phase 7 | 2 weeks | Advanced Features |
| Phase 8 | 1 week | Error Testing |
| Phase 9 | 1 week | Integration |
| Phase 10 | 1 week | Polish |

**Milestones**:
- Week 3: Basic queries work
- Week 6: Catalog complete
- Week 8: All core features done
- Week 10: Integration with ODBC Crusher
- Week 11: Release v1.0

---

## 🚀 Getting Started

Once development begins:

### For Developers
```bash
git clone https://github.com/fdcastel/mock-odbc-driver
cd mock-odbc-driver
mkdir build && cd build
cmake ..
cmake --build .
```

### For Users (Future)
```bash
# Windows - Install MSI package
mock-odbc-driver-setup.msi

# Linux - Install via package manager  
sudo apt install mock-odbc-driver

# Test installation
odbcinst -q -d | grep "Mock ODBC"
```

### Example Connection
```cpp
// C++ Application
const char* conn_str = 
    "Driver={Mock ODBC Driver};"
    "Mode=Success;"
    "Catalog=Default;"
    "ResultSetSize=100;";

SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str, SQL_NTS, ...);
```

---

## 🤝 Contributing

Once the project is established:
- Issues: Report bugs or request features
- Pull Requests: Contribute code (with tests!)
- Documentation: Improve guides and examples
- Testing: Try the driver with different applications

---

## 📝 License

MIT License - Same as ODBC Crusher main project

---

**Status**: This is a planning document. Development has not yet started.  
**Next Steps**: Review plan → Start Phase 0 → Begin implementation

**Questions? Feedback?** Open an issue in the ODBC Crusher repository.
