# Instructions for AI Agents Working on ODBC Crusher C++

## 🤖 Welcome, Agent!

You are working on **ODBC Crusher**, a C++ CLI tool for testing and debugging ODBC drivers.
This document contains critical instructions for maintaining consistency and quality.

---

## MASTER RULE: Keep working until successful

**RULE #0**: Always ensure the work is complete and verified for all platforms.

Whenever you’re asked to do something, follow this process after completing the task:
- Build the project and run all tests.
  - If anything fails, fix the issues and repeat until all tests pass.
- Once all tests are green:
  - Commit the changes.
  - Push them to GitHub.
  - Monitor the workflow run using the `gh` command.
    - Avoid `gh` commands that require interactive input; provide all required information via CLI flags.
  - If the workflow fails, fix the issue and repeat this step until the workflow completes successfully.


## 📋 MANDATORY: Update PROJECT_PLAN.md

**RULE #1**: Every time you make significant changes to this project, you MUST update [PROJECT_PLAN.md](PROJECT_PLAN.md).

### What Requires a Plan Update?

- ✅ Completing a phase or milestone
- ✅ Adding new test modules or test categories
- ✅ Adding new dependencies (CMake packages, libraries)
- ✅ Changing project structure (new directories, reorganization)
- ✅ Implementing new features
- ✅ Updating architecture decisions
- ✅ Identifying new limitations or future ideas

### What Doesn't Require a Plan Update?

- ❌ Fixing typos or minor bugs
- ❌ Refactoring without functional changes
- ❌ Adding comments or documentation
- ❌ Updating dependencies to patch versions

### How to Update the Plan

1. **Read the current plan first**: Always read [PROJECT_PLAN.md](PROJECT_PLAN.md) before making changes
2. **Update relevant sections**:
   - Mark completed items in the current phase with ✅
   - Update "Current Status" section
   - Add new phases or modify existing ones if needed
   - Update "Last Updated" date at the top
3. **Be specific**: Include what was added, changed, or removed
4. **Keep it current**: The plan should always reflect the actual state of the project

### Example Plan Update

```markdown
### Phase 1: Core ODBC Infrastructure ✅ (Completed - February 5, 2026)
**Goal**: RAII wrappers for ODBC handles and basic connection

- [x] OdbcEnvironment class with proper initialization
- [x] OdbcConnection class with connect/disconnect
- [x] OdbcStatement class with basic execution
...

```

---

## 🗂️ MANDATORY: Use ./tmp for Temporary Files

**RULE #2**: All temporary files and scripts created during investigation or development MUST go in `./tmp/` folder.

### What Goes in ./tmp/?

- ✅ Test output files (JSON, HTML, XML, CSV, logs)
- ✅ Temporary test scripts or executables
- ✅ Investigation/debugging scripts
- ✅ Sample data files for testing
- ✅ Compiled test binaries during development
- ✅ Any file created during development that isn't part of the final product

### What Does NOT Go in ./tmp/?

- ❌ Source code files (go in `src/`, `include/`)
- ❌ Unit tests (go in `tests/`)
- ❌ Documentation (README.md, PROJECT_PLAN.md, etc.)
- ❌ Build files (go in `build/` directory)
- ❌ CMake files (CMakeLists.txt, cmake/*.cmake)

### Why?

The `./tmp/` folder is gitignored to prevent temporary files from polluting commits. **Always create temporary files here** to keep the repository clean.

---

## 🛠️ Project Standards

### C++ Standards

**C++ Version**: C++17 (minimum)

**Code Style**:
- **Formatting**: Use `clang-format` (configuration in `.clang-format`)
- **Naming**:
  - Classes: `PascalCase` (e.g., `OdbcConnection`, `TestRunner`)
  - Functions/Methods: `snake_case` (e.g., `get_connection()`, `execute_query()`)
  - Variables: `snake_case` with trailing underscore for members (e.g., `connection_string_`, `handle_`)
  - Constants: `kPascalCase` or `UPPER_CASE` (e.g., `kMaxRetries`, `MAX_BUFFER_SIZE`)
  - Namespaces: `snake_case` (e.g., `odbc_crusher::core`, `odbc_crusher::tests`)
- **Header Guards**: Use `#pragma once`
- **Include Order**:
  1. Corresponding header (for .cpp files)
  2. C++ standard library headers
  3. Third-party library headers
  4. Project headers

**Example**:
```cpp
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::core {

class OdbcConnection {
public:
    explicit OdbcConnection(OdbcEnvironment& env);
    ~OdbcConnection();
    
    // Non-copyable, movable
    OdbcConnection(const OdbcConnection&) = delete;
    OdbcConnection& operator=(const OdbcConnection&) = delete;
    OdbcConnection(OdbcConnection&&) noexcept = default;
    OdbcConnection& operator=(OdbcConnection&&) noexcept = default;
    
    void connect(std::string_view connection_string);
    void disconnect();
    bool is_connected() const noexcept;
    
    SQLHDBC get_handle() const noexcept { return handle_; }

private:
    SQLHDBC handle_ = SQL_NULL_HDBC;
    OdbcEnvironment& env_;
    bool connected_ = false;
};

} // namespace odbc_crusher::core
```

### CMake Standards

**Version**: CMake 3.20+ (minimum)

**Best Practices**:
- Use modern target-based CMake (no `include_directories()`, use `target_include_directories()`)
- One `CMakeLists.txt` per directory with targets
- Use `FetchContent` for external dependencies
- Separate compilation flags per target, not globally

**Example**:
```cmake
# src/core/CMakeLists.txt
add_library(odbc_crusher_core
    odbc_handle.cpp
    odbc_connection.cpp
    odbc_statement.cpp
    odbc_error.cpp
)

target_include_directories(odbc_crusher_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(odbc_crusher_core
    PUBLIC
        ODBC::ODBC
    PRIVATE
        project_warnings
        project_options
)

target_compile_features(odbc_crusher_core PUBLIC cxx_std_17)
```

### Testing with Google Test

**Framework**: Google Test + Google Mock

**Location**: Tests for the tool itself go in `tests/` directory

**Naming**:
- Test files: `test_*.cpp` (e.g., `test_odbc_handle.cpp`)
- Test fixtures: `*Test` (e.g., `class OdbcHandleTest : public ::testing::Test`)
- Test cases: `TEST_F(FixtureName, TestName)` or `TEST(SuiteName, TestName)`

**Example**:
```cpp
// tests/test_odbc_connection.cpp
#include <gtest/gtest.h>
#include "core/odbc_connection.hpp"

class OdbcConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        env_ = std::make_unique<OdbcEnvironment>();
    }
    
    std::unique_ptr<OdbcEnvironment> env_;
};

TEST_F(OdbcConnectionTest, ConstructorDoesNotThrow) {
    EXPECT_NO_THROW({
        OdbcConnection conn(*env_);
    });
}

TEST_F(OdbcConnectionTest, ConnectWithValidDSN) {
    OdbcConnection conn(*env_);
    ASSERT_NO_THROW(conn.connect("DSN=TestDB"));
    EXPECT_TRUE(conn.is_connected());
}
```

### Building the Project

**Build Directory**: Always use out-of-source builds

```bash
# Configure (Debug)
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Run the tool
./build/src/odbc-crusher --help
```

**Build Types**:
- `Debug`: Full symbols, no optimization, assertions enabled
- `Release`: Optimized, no symbols, assertions disabled
- `RelWithDebInfo`: Optimized with symbols (for profiling)
- `MinSizeRel`: Optimized for size

### Git Commit Messages

Use conventional commits format:
```
feat: add SQLGetFunctions support
fix: handle connection timeout correctly
docs: update installation instructions
test: add unit tests for reporter module
refactor: extract error handling to separate class
perf: optimize result set fetching
build: update CMake minimum version to 3.20
ci: add Linux build to GitHub Actions
```

---

## 🏗️ Architecture Guidelines

### Adding New ODBC Tests

1. **Location**: Create test classes in `src/tests/`
2. **Header/Source**: Use `*_tests.hpp` and `*_tests.cpp` naming
3. **Inherit from base**: All test classes should inherit from `TestBase`
4. **Namespace**: Use `odbc_crusher::tests`

**Example**:
```cpp
// src/tests/connection_tests.hpp
#pragma once

#include "test_base.hpp"
#include <vector>

namespace odbc_crusher::tests {

class ConnectionTests : public TestBase {
public:
    explicit ConnectionTests(OdbcConnection& conn);
    
    std::vector<TestResult> run() override;

private:
    TestResult test_basic_connection();
    TestResult test_connection_attributes();
    TestResult test_connection_timeout();
};

} // namespace odbc_crusher::tests
```

### Test Result Format

Every test must return results in this format:

```cpp
struct TestResult {
    std::string test_name;           // e.g., "test_sql_connect"
    std::string function;            // e.g., "SQLConnect"
    TestStatus status;               // PASS, FAIL, SKIP, ERROR
    Severity severity;               // CRITICAL, ERROR, WARNING, INFO
    std::string expected;            // Expected behavior
    std::string actual;              // Actual behavior
    std::optional<std::string> diagnostic;  // Error details
    std::optional<std::string> suggestion;  // How to fix
    std::chrono::microseconds duration;     // Test execution time
};
```

### Error Handling

**Always extract ODBC diagnostics** when an ODBC function fails:

```cpp
SQLRETURN ret = SQLConnect(hdbc, ...);
if (!SQL_SUCCEEDED(ret)) {
    // Extract all diagnostic records
    SQLSMALLINT rec = 1;
    SQLCHAR sqlstate[6];
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH];
    SQLINTEGER native_error;
    SQLSMALLINT text_length;
    
    while (SQL_SUCCEEDED(SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, rec,
                                       sqlstate, &native_error,
                                       message, sizeof(message), &text_length))) {
        // Store diagnostic info
        rec++;
    }
}
```

**RAII for Handles**: Always use RAII wrappers, never raw handles in test code:

```cpp
// ✅ CORRECT
OdbcConnection conn(env);
conn.connect(connection_string);
// Automatic cleanup in destructor

// ❌ WRONG - Manual handle management
SQLHDBC hdbc;
SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
SQLDriverConnect(hdbc, ...);
// Forgot to call SQLFreeHandle!
```

### Adding New Dependencies

1. **CMake Package**: If available via `find_package()`, add to root `CMakeLists.txt`
2. **FetchContent**: For header-only or small libraries
3. **Git Submodule**: For larger dependencies requiring specific versions
4. **Update PROJECT_PLAN.md**: Add to "Dependencies" section with rationale

**Example FetchContent**:
```cmake
include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)

FetchContent_MakeAvailable(json)

target_link_libraries(odbc_crusher_reporter PRIVATE nlohmann_json::nlohmann_json)
```

---

## 📚 ODBC Knowledge Resources

When implementing ODBC tests, refer to:

1. **Primary Reference**: [ODBC API Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/syntax/odbc-api-reference)
2. **Detailed Guide**: [ODBC Programmer's Reference](https://learn.microsoft.com/en-us/sql/odbc/reference/odbc-programmer-s-reference)
3. **ODBC 3.8 Features**: [Upgrading to ODBC 3.8](https://learn.microsoft.com/en-us/sql/odbc/reference/develop-driver/upgrading-a-3-5-driver-to-a-3-8-driver)
4. **Linux (unixODBC)**: [unixODBC Documentation](http://www.unixodbc.org/)

### Key ODBC Concepts

- **Handles**: Environment (HENV), Connection (HDBC), Statement (HSTMT), Descriptor (HDESC)
- **Handle Hierarchy**: Environment → Connection → Statement/Descriptor
- **Return Codes**: 
  - `SQL_SUCCESS` (0)
  - `SQL_SUCCESS_WITH_INFO` (1)
  - `SQL_ERROR` (-1)
  - `SQL_INVALID_HANDLE` (-2)
  - `SQL_NO_DATA` (100)
- **Diagnostic Records**: Use `SQLGetDiagRec` to get error details after failures

---

## 🐛 Testing and Debugging

### Test Connection Strings

For consistent testing across development, use these pre-configured connection strings:

#### Firebird
```powershell
# PowerShell
$env:FIREBIRD_ODBC_CONNECTION='Driver={Firebird ODBC Driver};Database=/fbodbc-tests/TEST.FB50.FDB;UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;CLIENT=/fbodbc-tests/fb502/fbclient.dll'
```

```bash
# Linux/macOS
export FIREBIRD_ODBC_CONNECTION='Driver={Firebird ODBC Driver};Database=/fbodbc-tests/TEST.FB50.FDB;UID=SYSDBA;PWD=masterkey;CHARSET=UTF8;CLIENT=/fbodbc-tests/fb502/fbclient.dll'
```

#### MySQL
```powershell
# PowerShell
$env:MYSQL_ODBC_CONNECTION='Driver={MySQL ODBC 9.6 Unicode Driver};Server=SRV-MYSQL;Database=bpcom_test;UID=root;PWD=masterkey;'
```

```bash
# Linux/macOS
export MYSQL_ODBC_CONNECTION='Driver={MySQL ODBC 9.6 Unicode Driver};Server=SRV-MYSQL;Database=bpcom_test;UID=root;PWD=masterkey;'
```

### Testing ODBC Connections

- **Test DSN**: Create a DSN using:
  - Windows: ODBC Data Source Administrator (`odbcad32.exe`)
  - Linux: Edit `/etc/odbc.ini` or `~/.odbc.ini`
- **Connection String Formats**:
  - DSN-based: `"DSN=name;UID=user;PWD=pass"`
  - Driver-based: `"DRIVER={SQL Server};SERVER=localhost;DATABASE=test;UID=sa;PWD=pass"`
  - File DSN: `"FILEDSN=c:\\mydb.dsn"`

### Debugging Tips

**Enable ODBC Tracing**:
- Windows: ODBC Data Source Administrator → Tracing tab
- Linux: Set `Trace=Yes` and `TraceFile=/tmp/odbc.log` in `odbcinst.ini`

**Use Debugger**:
```bash
# GDB (Linux)
gdb --args ./build/odbc-crusher "DSN=test"

# LLDB (macOS)
lldb -- ./build/odbc-crusher "DSN=test"

# Visual Studio (Windows)
# F5 with command-line arguments set in project properties
```

**Memory Checking**:
```bash
# Valgrind (Linux)
valgrind --leak-check=full ./build/odbc-crusher "DSN=test"

# AddressSanitizer (All platforms with Clang/GCC)
cmake -B build -DCMAKE_CXX_FLAGS="-fsanitize=address"
cmake --build build
./build/odbc-crusher "DSN=test"
```

---

## ✅ Quality Checklist

Before considering work complete:

- [ ] Code compiles without warnings on Windows and Linux
- [ ] All unit tests pass (`ctest --test-dir build`)
- [ ] Code follows project naming conventions
- [ ] All public APIs have documentation comments
- [ ] RAII used for all ODBC handles (no manual cleanup)
- [ ] Error handling extracts full diagnostic records
- [ ] No memory leaks (checked with valgrind or sanitizers)
- [ ] PROJECT_PLAN.md is updated (if applicable)
- [ ] README.md is updated (if user-facing changes)
- [ ] Git commit message follows conventional commits

---

## 🎯 Project Philosophy

**Remember**: This tool is for ODBC driver **developers**, not end users. It should:

1. **Be thorough**: Test edge cases and error conditions
2. **Be clear**: Reports should explain exactly what went wrong with SQLSTATE codes
3. **Be helpful**: Suggest fixes and provide context from ODBC specification
4. **Be reliable**: The tool itself should never crash (handle all ODBC errors gracefully)
5. **Be direct**: Use native ODBC API, no abstractions that hide behavior
6. **Be fast**: C++ allows microsecond timing, use it

---

## 🔄 Continuous Improvement

This project grows over time. When you notice:

- Repetitive code → Create a helper function or template
- Complex logic → Add comments and break into smaller functions
- Missing tests → Add them to the next phase in PROJECT_PLAN.md
- Unclear documentation → Improve it immediately
- Platform-specific code → Abstract into platform layer

---

## 📊 Performance Considerations

- Use `std::string_view` for read-only string parameters (avoid copies)
- Use `std::optional` instead of exceptions for expected failures
- Pre-allocate buffers for ODBC result fetching
- Use move semantics for large objects
- Profile with `perf` (Linux) or Visual Studio Profiler (Windows)
- Aim for <1ms overhead per test (excluding ODBC call time)

---

**Thank you for maintaining ODBC Crusher C++!** Your adherence to these guidelines ensures the project remains organized, performant, and valuable for ODBC driver developers worldwide.

---

*Last Updated*: February 5, 2026
