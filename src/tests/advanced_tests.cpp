#include "advanced_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <vector>

namespace odbc_crusher::tests {

std::vector<TestResult> AdvancedTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_cursor_types());
    results.push_back(test_array_binding());
    results.push_back(test_async_capability());
    results.push_back(test_rowset_size());
    results.push_back(test_positioned_operations());
    results.push_back(test_statement_attributes());
    
    // Phase 12: Scrollable Cursor Tests
    results.push_back(test_fetch_scroll_next());
    results.push_back(test_fetch_scroll_first_last());
    results.push_back(test_fetch_scroll_absolute());
    results.push_back(test_cursor_scrollable_attr());
    
    return results;
}

TestResult AdvancedTests::test_cursor_types() {
    TestResult result = make_result(
        "test_cursor_types",
        "SQLSetStmtAttr(SQL_ATTR_CURSOR_TYPE)",
        TestStatus::PASS,
        "Query supported cursor types",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_CURSOR_TYPE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to get current cursor type
        SQLULEN cursor_type = 0;
        SQLRETURN ret = SQLGetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_CURSOR_TYPE,
            &cursor_type,
            0,
            nullptr
        );
        
        if (SQL_SUCCEEDED(ret)) {
            std::ostringstream oss;
            oss << "Default cursor type: ";
            
            switch (cursor_type) {
                case SQL_CURSOR_FORWARD_ONLY:
                    oss << "FORWARD ONLY";
                    break;
                case SQL_CURSOR_STATIC:
                    oss << "STATIC";
                    break;
                case SQL_CURSOR_KEYSET_DRIVEN:
                    oss << "KEYSET DRIVEN";
                    break;
                case SQL_CURSOR_DYNAMIC:
                    oss << "DYNAMIC";
                    break;
                default:
                    oss << "Unknown (" << cursor_type << ")";
            }
            
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "Cursor type query not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Non-forward-only cursor types are a Level 2 feature";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_array_binding() {
    TestResult result = make_result(
        "test_array_binding",
        "SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE)",
        TestStatus::PASS,
        "Test array/bulk parameter binding capability",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_PARAMSET_SIZE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to set parameter array size
        SQLULEN array_size = 10;
        SQLRETURN ret = SQLSetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_PARAMSET_SIZE,
            (SQLPOINTER)array_size,
            0
        );
        
        if (SQL_SUCCEEDED(ret)) {
            // Verify it was set
            SQLULEN check_size = 0;
            ret = SQLGetStmtAttr(
                stmt.get_handle(),
                SQL_ATTR_PARAMSET_SIZE,
                &check_size,
                0,
                nullptr
            );
            
            if (SQL_SUCCEEDED(ret) && check_size == array_size) {
                result.actual = "Array binding supported (paramset size = 10)";
                result.status = TestStatus::PASS;
            } else {
                result.actual = "Array binding setting did not persist";
                result.status = TestStatus::FAIL;
            }
        } else {
            result.actual = "Array binding not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Driver does not support SQL_ATTR_PARAMSET_SIZE for bulk operations";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_async_capability() {
    TestResult result = make_result(
        "test_async_capability",
        "SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)",
        TestStatus::PASS,
        "Test asynchronous execution capability",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_ASYNC_ENABLE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to enable async execution
        SQLRETURN ret = SQLSetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_ASYNC_ENABLE,
            (SQLPOINTER)SQL_ASYNC_ENABLE_ON,
            0
        );
        
        if (SQL_SUCCEEDED(ret)) {
            // Verify it was set
            SQLULEN async_enabled = 0;
            ret = SQLGetStmtAttr(
                stmt.get_handle(),
                SQL_ATTR_ASYNC_ENABLE,
                &async_enabled,
                0,
                nullptr
            );
            
            if (SQL_SUCCEEDED(ret)) {
                if (async_enabled == SQL_ASYNC_ENABLE_ON) {
                    result.actual = "Asynchronous execution supported";
                    result.status = TestStatus::PASS;
                } else {
                    // Driver doesn't actually support it even though it accepted the setting
                    result.actual = "Async mode not persistently supported";
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.suggestion = "Driver accepted SQL_ATTR_ASYNC_ENABLE but did not persist the setting";
                }
                
                // Turn it back off
                SQLSetStmtAttr(
                    stmt.get_handle(),
                    SQL_ATTR_ASYNC_ENABLE,
                    (SQLPOINTER)SQL_ASYNC_ENABLE_OFF,
                    0
                );
            } else {
                result.actual = "Could not query async status";
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.suggestion = "SQLGetStmtAttr for SQL_ATTR_ASYNC_ENABLE failed after setting";
            }
        } else {
            result.actual = "Asynchronous execution not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQL_ATTR_ASYNC_ENABLE is a Level 2 feature; driver does not support async execution";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_rowset_size() {
    TestResult result = make_result(
        "test_rowset_size",
        "SQLSetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE)",
        TestStatus::PASS,
        "Test rowset size for block cursors",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_ROW_ARRAY_SIZE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to set rowset size
        SQLULEN rowset_size = 100;
        SQLRETURN ret = SQLSetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_ROW_ARRAY_SIZE,
            (SQLPOINTER)rowset_size,
            0
        );
        
        if (SQL_SUCCEEDED(ret)) {
            // Verify
            SQLULEN check_size = 0;
            ret = SQLGetStmtAttr(
                stmt.get_handle(),
                SQL_ATTR_ROW_ARRAY_SIZE,
                &check_size,
                0,
                nullptr
            );
            
            if (SQL_SUCCEEDED(ret) && check_size == rowset_size) {
                result.actual = "Rowset size supported (set to 100)";
                result.status = TestStatus::PASS;
            } else {
                result.actual = "Rowset size not preserved";
                result.status = TestStatus::FAIL;
            }
        } else {
            result.actual = "Rowset size attribute not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQL_ATTR_ROW_ARRAY_SIZE > 1 is a Level 2 feature for block cursors";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_positioned_operations() {
    TestResult result = make_result(
        "test_positioned_operations",
        "SQLSetStmtAttr(SQL_ATTR_CONCURRENCY)",
        TestStatus::PASS,
        "Test positioned update/delete capability",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_CONCURRENCY"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to set concurrency for positioned operations
        SQLRETURN ret = SQLSetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_CONCURRENCY,
            (SQLPOINTER)SQL_CONCUR_LOCK,
            0
        );
        
        if (SQL_SUCCEEDED(ret)) {
            SQLULEN concurrency = 0;
            ret = SQLGetStmtAttr(
                stmt.get_handle(),
                SQL_ATTR_CONCURRENCY,
                &concurrency,
                0,
                nullptr
            );
            
            if (SQL_SUCCEEDED(ret)) {
                std::ostringstream oss;
                oss << "Concurrency control: ";
                
                switch (concurrency) {
                    case SQL_CONCUR_READ_ONLY:
                        oss << "READ ONLY";
                        break;
                    case SQL_CONCUR_LOCK:
                        oss << "LOCK";
                        break;
                    case SQL_CONCUR_ROWVER:
                        oss << "ROWVER";
                        break;
                    case SQL_CONCUR_VALUES:
                        oss << "VALUES";
                        break;
                    default:
                        oss << "Unknown (" << concurrency << ")";
                }
                
                result.actual = oss.str();
                result.status = TestStatus::PASS;
            } else {
                result.actual = "Could not query concurrency";
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.suggestion = "SQLGetStmtAttr for SQL_ATTR_CONCURRENCY failed after setting";
            }
        } else {
            result.actual = "Positioned operations not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Non-read-only SQL_ATTR_CONCURRENCY is a Level 2 feature";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_statement_attributes() {
    TestResult result = make_result(
        "test_statement_attributes",
        "SQLGetStmtAttr (various attributes)",
        TestStatus::PASS,
        "Query various statement attributes",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetStmtAttr"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        int attrs_checked = 0;
        int attrs_supported = 0;
        
        // Check multiple attributes
        std::vector<std::pair<SQLINTEGER, std::string>> attrs = {
            {SQL_ATTR_QUERY_TIMEOUT, "Query timeout"},
            {SQL_ATTR_MAX_ROWS, "Max rows"},
            {SQL_ATTR_MAX_LENGTH, "Max length"},
            {SQL_ATTR_NOSCAN, "No scan"},
            {SQL_ATTR_RETRIEVE_DATA, "Retrieve data"}
        };
        
        for (const auto& [attr, name] : attrs) {
            SQLULEN value = 0;
            SQLRETURN ret = SQLGetStmtAttr(
                stmt.get_handle(),
                attr,
                &value,
                0,
                nullptr
            );
            
            attrs_checked++;
            if (SQL_SUCCEEDED(ret)) {
                attrs_supported++;
            }
        }
        
        std::ostringstream oss;
        oss << attrs_supported << "/" << attrs_checked << " statement attributes queryable";
        
        result.actual = oss.str();
        result.status = TestStatus::PASS;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

// ============================================================
// Phase 12: Scrollable Cursor Tests
// ============================================================

TestResult AdvancedTests::test_fetch_scroll_next() {
    TestResult result = make_result(
        "test_fetch_scroll_next",
        "SQLFetchScroll(SQL_FETCH_NEXT)",
        TestStatus::PASS,
        "SQLFetchScroll with SQL_FETCH_NEXT works",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLFetchScroll"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_NEXT, 0);
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLFetchScroll(SQL_FETCH_NEXT) succeeded";
                    success = true;
                    break;
                } else if (rc == SQL_NO_DATA) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLFetchScroll(SQL_FETCH_NEXT) returned SQL_NO_DATA (empty result)";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test SQLFetchScroll(SQL_FETCH_NEXT)";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_fetch_scroll_first_last() {
    TestResult result = make_result(
        "test_fetch_scroll_first_last",
        "SQLFetchScroll(SQL_FETCH_FIRST/SQL_FETCH_LAST)",
        TestStatus::PASS,
        "SQLFetchScroll with FIRST/LAST orientation",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLFetchScroll, §SQL_FETCH_FIRST"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Need scrollable cursor
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                      (SQLPOINTER)SQL_CURSOR_STATIC, 0);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_FIRST, 0);
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLFetchScroll(SQL_FETCH_FIRST) succeeded (scrollable cursor)";
                    success = true;
                    break;
                } else if (rc == SQL_ERROR) {
                    SQLCHAR sqlstate[6] = {0};
                    SQLINTEGER native = 0;
                    SQLCHAR msg[256] = {0};
                    SQLSMALLINT msg_len = 0;
                    SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(), 1,
                                 sqlstate, &native, msg, sizeof(msg), &msg_len);
                    std::string state(reinterpret_cast<char*>(sqlstate));
                    
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "SQLFetchScroll(SQL_FETCH_FIRST) not supported (SQLSTATE=" + state + ")";
                    result.suggestion = "Scrollable cursors (SQL_FETCH_FIRST/LAST) are a Level 2 feature";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test scrollable cursor";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_fetch_scroll_absolute() {
    TestResult result = make_result(
        "test_fetch_scroll_absolute",
        "SQLFetchScroll(SQL_FETCH_ABSOLUTE)",
        TestStatus::PASS,
        "SQLFetchScroll with absolute row position",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLFetchScroll, §SQL_FETCH_ABSOLUTE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Need scrollable cursor
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                      (SQLPOINTER)SQL_CURSOR_STATIC, 0);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_ABSOLUTE, 1);
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLFetchScroll(SQL_FETCH_ABSOLUTE, 1) succeeded";
                    success = true;
                    break;
                } else if (rc == SQL_ERROR) {
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "SQLFetchScroll(SQL_FETCH_ABSOLUTE) not supported";
                    result.suggestion = "Absolute positioning is a Level 2 cursor feature";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test SQL_FETCH_ABSOLUTE";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult AdvancedTests::test_cursor_scrollable_attr() {
    TestResult result = make_result(
        "test_cursor_scrollable_attr",
        "SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE)",
        TestStatus::PASS,
        "Set and verify SQL_ATTR_CURSOR_SCROLLABLE",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 §SQLSetStmtAttr, §SQL_ATTR_CURSOR_SCROLLABLE"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to set cursor scrollable
        SQLRETURN rc = SQLSetStmtAttr(
            stmt.get_handle(),
            SQL_ATTR_CURSOR_SCROLLABLE,
            (SQLPOINTER)SQL_SCROLLABLE,
            0
        );
        
        if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::PASS;
            result.actual = "SQL_ATTR_CURSOR_SCROLLABLE set to SQL_SCROLLABLE";
        } else {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQL_ATTR_CURSOR_SCROLLABLE not supported";
            result.suggestion = "Scrollable cursors are a Level 2 feature per ODBC 3.x";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
