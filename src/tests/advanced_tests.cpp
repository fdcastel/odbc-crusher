#include "advanced_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <vector>

namespace odbc_crusher::tests {

std::vector<TestResult> AdvancedTests::run() {
    return {
        test_cursor_types(),
        test_array_binding(),
        test_async_capability(),
        test_rowset_size(),
        test_positioned_operations(),
        test_statement_attributes(),

        // Phase 12: Scrollable Cursor Tests
        test_fetch_scroll_next(),
        test_fetch_scroll_first_last(),
        test_fetch_scroll_absolute(),
        test_cursor_scrollable_attr()
    };
}

TestResult AdvancedTests::test_cursor_types() {
    return run_test(
        "test_cursor_types", "SQLSetStmtAttr(SQL_ATTR_CURSOR_TYPE)",
        "Query supported cursor types",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_CURSOR_TYPE",
        [&](TestResult& r) {
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

                r.actual = oss.str();
                r.status = TestStatus::PASS;
            } else {
                r.actual = "Cursor type query not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Non-forward-only cursor types are a Level 2 feature";
            }
        });
}

TestResult AdvancedTests::test_array_binding() {
    return run_test(
        "test_array_binding", "SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE)",
        "Test array/bulk parameter binding capability",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_PARAMSET_SIZE",
        [&](TestResult& r) {
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
                    r.actual = "Array binding supported (paramset size = 10)";
                    r.status = TestStatus::PASS;
                } else {
                    r.actual = "Array binding setting did not persist";
                    r.status = TestStatus::FAIL;
                }
            } else {
                r.actual = "Array binding not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver does not support SQL_ATTR_PARAMSET_SIZE for bulk operations";
            }
        });
}

TestResult AdvancedTests::test_async_capability() {
    return run_test(
        "test_async_capability", "SQLSetStmtAttr(SQL_ATTR_ASYNC_ENABLE)",
        "Test asynchronous execution capability",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_ASYNC_ENABLE",
        [&](TestResult& r) {
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
                        r.actual = "Asynchronous execution supported";
                        r.status = TestStatus::PASS;
                    } else {
                        // Driver doesn't actually support it even though it accepted the setting
                        r.actual = "Async mode not persistently supported";
                        r.status = TestStatus::SKIP_UNSUPPORTED;
                        r.suggestion = "Driver accepted SQL_ATTR_ASYNC_ENABLE but did not persist the setting";
                    }

                    // Turn it back off
                    SQLSetStmtAttr(
                        stmt.get_handle(),
                        SQL_ATTR_ASYNC_ENABLE,
                        (SQLPOINTER)SQL_ASYNC_ENABLE_OFF,
                        0
                    );
                } else {
                    r.actual = "Could not query async status";
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.suggestion = "SQLGetStmtAttr for SQL_ATTR_ASYNC_ENABLE failed after setting";
                }
            } else {
                r.actual = "Asynchronous execution not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "SQL_ATTR_ASYNC_ENABLE is a Level 2 feature; driver does not support async execution";
            }
        });
}

TestResult AdvancedTests::test_rowset_size() {
    return run_test(
        "test_rowset_size", "SQLSetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE)",
        "Test rowset size for block cursors",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_ROW_ARRAY_SIZE",
        [&](TestResult& r) {
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
                    r.actual = "Rowset size supported (set to 100)";
                    r.status = TestStatus::PASS;
                } else {
                    r.actual = "Rowset size not preserved";
                    r.status = TestStatus::FAIL;
                }
            } else {
                r.actual = "Rowset size attribute not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "SQL_ATTR_ROW_ARRAY_SIZE > 1 is a Level 2 feature for block cursors";
            }
        });
}

TestResult AdvancedTests::test_positioned_operations() {
    return run_test(
        "test_positioned_operations", "SQLSetStmtAttr(SQL_ATTR_CONCURRENCY)",
        "Test positioned update/delete capability",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_CONCURRENCY",
        [&](TestResult& r) {
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

                    r.actual = oss.str();
                    r.status = TestStatus::PASS;
                } else {
                    r.actual = "Could not query concurrency";
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.suggestion = "SQLGetStmtAttr for SQL_ATTR_CONCURRENCY failed after setting";
                }
            } else {
                r.actual = "Positioned operations not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Non-read-only SQL_ATTR_CONCURRENCY is a Level 2 feature";
            }
        });
}

TestResult AdvancedTests::test_statement_attributes() {
    return run_test(
        "test_statement_attributes", "SQLGetStmtAttr (various attributes)",
        "Query various statement attributes",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetStmtAttr",
        [&](TestResult& r) {
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

            r.actual = oss.str();
            r.status = TestStatus::PASS;
        });
}

// ============================================================
// Phase 12: Scrollable Cursor Tests
// ============================================================

TestResult AdvancedTests::test_fetch_scroll_next() {
    return run_test(
        "test_fetch_scroll_next", "SQLFetchScroll(SQL_FETCH_NEXT)",
        "SQLFetchScroll with SQL_FETCH_NEXT works",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLFetchScroll",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_NEXT, 0);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLFetchScroll(SQL_FETCH_NEXT) succeeded";
                    success = true;
                    break;
                } else if (rc == SQL_NO_DATA) {
                    // A26: this used to PASS as "empty result". The query
                    // is `SELECT 1`, which has exactly one row — so
                    // SQL_NO_DATA on the first fetch means the driver lost
                    // it, not that the result set was empty.
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetchScroll(SQL_FETCH_NEXT) returned "
                               "SQL_NO_DATA on the first row of a "
                               "single-row result set";
                    r.severity = Severity::ERR;
                    r.suggestion =
                        "SELECT 1 returns one row; the first "
                        "SQLFetchScroll(SQL_FETCH_NEXT) must deliver it.";
                    success = true;
                    break;
                }            } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQLFetchScroll(SQL_FETCH_NEXT)";
            }
        });
}

TestResult AdvancedTests::test_fetch_scroll_first_last() {
    return run_test(
        "test_fetch_scroll_first_last",
        "SQLFetchScroll(SQL_FETCH_FIRST/SQL_FETCH_LAST)",
        "SQLFetchScroll with FIRST/LAST orientation",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLFetchScroll, SQL_FETCH_FIRST",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Need scrollable cursor
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                          (SQLPOINTER)SQL_CURSOR_STATIC, 0);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_FIRST, 0);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLFetchScroll(SQL_FETCH_FIRST) succeeded (scrollable cursor)";
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

                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLFetchScroll(SQL_FETCH_FIRST) not supported (SQLSTATE=" + state + ")";
                    r.suggestion = "Scrollable cursors (SQL_FETCH_FIRST/LAST) are a Level 2 feature";
                    success = true;
                    break;
                }            } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test scrollable cursor";
            }
        });
}

TestResult AdvancedTests::test_fetch_scroll_absolute() {
    return run_test(
        "test_fetch_scroll_absolute", "SQLFetchScroll(SQL_FETCH_ABSOLUTE)",
        "SQLFetchScroll with absolute row position",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLFetchScroll, SQL_FETCH_ABSOLUTE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Need scrollable cursor
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                          (SQLPOINTER)SQL_CURSOR_STATIC, 0);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                SQLRETURN rc = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_ABSOLUTE, 1);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLFetchScroll(SQL_FETCH_ABSOLUTE, 1) succeeded";
                    success = true;
                    break;
                } else if (rc == SQL_ERROR) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLFetchScroll(SQL_FETCH_ABSOLUTE) not supported";
                    r.suggestion = "Absolute positioning is a Level 2 cursor feature";
                    success = true;
                    break;
                }            } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQL_FETCH_ABSOLUTE";
            }
        });
}

TestResult AdvancedTests::test_cursor_scrollable_attr() {
    return run_test(
        "test_cursor_scrollable_attr", "SQLSetStmtAttr(SQL_ATTR_CURSOR_SCROLLABLE)",
        "Set and verify SQL_ATTR_CURSOR_SCROLLABLE",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr, SQL_ATTR_CURSOR_SCROLLABLE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try to set cursor scrollable
            SQLRETURN rc = SQLSetStmtAttr(
                stmt.get_handle(),
                SQL_ATTR_CURSOR_SCROLLABLE,
                (SQLPOINTER)SQL_SCROLLABLE,
                0
            );

            if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::PASS;
                r.actual = "SQL_ATTR_CURSOR_SCROLLABLE set to SQL_SCROLLABLE";
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQL_ATTR_CURSOR_SCROLLABLE not supported";
                r.suggestion = "Scrollable cursors are a Level 2 feature per ODBC 3.x";
            }
        });
}

} // namespace odbc_crusher::tests
