#include "sqlstate_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"

namespace odbc_crusher::tests {

std::vector<TestResult> SqlstateTests::run() {
    return {
        test_execute_without_prepare(),
        test_fetch_no_cursor(),
        test_getdata_col0_no_bookmark(),
        test_getdata_col_out_of_range(),
        test_execdirect_syntax_error(),
        test_bindparam_invalid_ctype(),
        test_getinfo_invalid_type(),
        test_setconnattr_invalid_attr(),
        test_closecursor_no_cursor(),
        test_connect_already_connected()
    };
}

std::string SqlstateTests::get_stmt_sqlstate(SQLHSTMT hstmt) {
    SQLCHAR sqlstate[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT msg_len = 0;
    SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1,
                                 sqlstate, &native, msg, sizeof(msg), &msg_len);
    if (SQL_SUCCEEDED(rc)) {
        return std::string(reinterpret_cast<char*>(sqlstate));
    }
    return "";
}

std::string SqlstateTests::get_conn_sqlstate(SQLHDBC hdbc) {
    SQLCHAR sqlstate[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT msg_len = 0;
    SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1,
                                 sqlstate, &native, msg, sizeof(msg), &msg_len);
    if (SQL_SUCCEEDED(rc)) {
        return std::string(reinterpret_cast<char*>(sqlstate));
    }
    return "";
}

TestResult SqlstateTests::test_execute_without_prepare() {
    TestResult result = make_result(
        "test_execute_without_prepare",
        "SQLExecute",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE HY010",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecute, Appendix B: State Transition Tables"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        SQLRETURN rc = SQLExecute(stmt.get_handle());
        
        if (rc == SQL_ERROR) {
            std::string state = get_stmt_sqlstate(stmt.get_handle());
            if (state == "HY010") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with HY010 (Function sequence error)";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected HY010)";
                result.severity = Severity::WARNING;
                result.suggestion = "ODBC spec requires HY010 for SQLExecute without SQLPrepare";
            }
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "SQLExecute did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
            result.severity = Severity::ERR;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_fetch_no_cursor() {
    TestResult result = make_result(
        "test_fetch_no_cursor",
        "SQLFetch",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE 24000",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLFetch, Appendix B: Statement Transitions"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // SQLFetch without any prior execute - no cursor open
        SQLRETURN rc = SQLFetch(stmt.get_handle());
        
        if (rc == SQL_ERROR) {
            std::string state = get_stmt_sqlstate(stmt.get_handle());
            if (state == "24000") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with 24000 (Invalid cursor state)";
            } else if (state == "HY010") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with HY010 (Function sequence error) - acceptable alternative";
                result.suggestion = "ODBC spec prefers 24000 for fetch without active cursor";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 24000)";
                result.severity = Severity::WARNING;
            }
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "SQLFetch did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
            result.severity = Severity::ERR;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_getdata_col0_no_bookmark() {
    TestResult result = make_result(
        "test_getdata_col0_no_bookmark",
        "SQLGetData",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE 07009 for column 0",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Descriptor Index"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute a query to get a result set
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    // Now try SQLGetData with column 0 (bookmark) when bookmarks aren't enabled
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 0, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (rc == SQL_ERROR) {
                        std::string state = get_stmt_sqlstate(stmt.get_handle());
                        if (state == "07009") {
                            result.status = TestStatus::PASS;
                            result.actual = "SQL_ERROR with 07009 (Invalid descriptor index) for column 0";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 07009)";
                            result.severity = Severity::WARNING;
                        }
                    } else {
                        result.status = TestStatus::FAIL;
                        result.actual = "SQLGetData(col=0) did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
                        result.severity = Severity::WARNING;
                        result.suggestion = "Driver should return 07009 for column 0 unless bookmarks are enabled";
                    }
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test column 0 access";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_getdata_col_out_of_range() {
    TestResult result = make_result(
        "test_getdata_col_out_of_range",
        "SQLGetData",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE 07009 for column > num_cols",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Descriptor Index"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    // Try a column way beyond what exists
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 999, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (rc == SQL_ERROR) {
                        std::string state = get_stmt_sqlstate(stmt.get_handle());
                        if (state == "07009") {
                            result.status = TestStatus::PASS;
                            result.actual = "SQL_ERROR with 07009 (Invalid descriptor index) for column 999";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 07009)";
                            result.severity = Severity::WARNING;
                        }
                    } else {
                        result.status = TestStatus::FAIL;
                        result.actual = "SQLGetData(col=999) did not return SQL_ERROR";
                        result.severity = Severity::WARNING;
                    }
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test out-of-range column";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_execdirect_syntax_error() {
    TestResult result = make_result(
        "test_execdirect_syntax_error",
        "SQLExecDirect",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE 42000 for syntax error",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLRETURN rc = SQLExecDirect(
            stmt.get_handle(),
            (SQLCHAR*)"THIS IS NOT VALID SQL !!! @#$%",
            SQL_NTS
        );
        
        if (rc == SQL_ERROR) {
            std::string state = get_stmt_sqlstate(stmt.get_handle());
            if (state == "42000") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with 42000 (Syntax error or access violation)";
            } else if (state.substr(0, 2) == "42") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with SQLSTATE=" + state + " (42xxx class - syntax/access error)";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 42000)";
                result.severity = Severity::WARNING;
                result.suggestion = "ODBC spec requires 42000 (Syntax error) for invalid SQL";
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "Driver accepted invalid SQL without error";
            result.severity = Severity::ERR;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_bindparam_invalid_ctype() {
    TestResult result = make_result(
        "test_bindparam_invalid_ctype",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE HY003 for invalid C type",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLINTEGER value = 42;
        SQLLEN indicator = sizeof(SQLINTEGER);
        
        // Use an invalid C type (9999)
        SQLRETURN rc = SQLBindParameter(
            stmt.get_handle(),
            1,                      // parameter number
            SQL_PARAM_INPUT,        // input/output type
            9999,                   // INVALID C type
            SQL_INTEGER,            // SQL type
            0, 0,                   // column size, decimal digits
            &value,                 // value pointer
            sizeof(value),          // buffer length
            &indicator              // str_len_or_ind
        );
        
        if (rc == SQL_ERROR) {
            std::string state = get_stmt_sqlstate(stmt.get_handle());
            if (state == "HY003") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with HY003 (Invalid application buffer type)";
            } else {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid C type";
                result.suggestion = "ODBC spec requires HY003 for invalid application buffer type";
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLBindParameter accepted invalid C type 9999";
            result.severity = Severity::WARNING;
            result.suggestion = "Driver should validate C type and return HY003 for invalid values";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_getinfo_invalid_type() {
    TestResult result = make_result(
        "test_getinfo_invalid_type",
        "SQLGetInfo",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE HY096 for invalid info type",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        char buffer[256] = {0};
        SQLSMALLINT len = 0;
        
        // Use an invalid info type (65535)
        SQLRETURN rc = SQLGetInfo(
            conn_.get_handle(),
            65535,
            buffer, sizeof(buffer), &len
        );
        
        if (rc == SQL_ERROR) {
            std::string state = get_conn_sqlstate(conn_.get_handle());
            if (state == "HY096") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with HY096 (Information type out of range)";
            } else {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid info type";
                result.suggestion = "ODBC spec requires HY096 for invalid SQLGetInfo info type";
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLGetInfo accepted invalid info type 65535";
            result.severity = Severity::WARNING;
            result.suggestion = "Driver should return HY096 for unrecognized information type";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_setconnattr_invalid_attr() {
    TestResult result = make_result(
        "test_setconnattr_invalid_attr",
        "SQLSetConnectAttr",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE HY092 for invalid attribute",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLSetConnectAttr"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Use an invalid attribute ID (99999)
        SQLRETURN rc = SQLSetConnectAttr(
            conn_.get_handle(),
            99999,
            (SQLPOINTER)0,
            0
        );
        
        if (rc == SQL_ERROR) {
            std::string state = get_conn_sqlstate(conn_.get_handle());
            if (state == "HY092") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with HY092 (Invalid attribute/option identifier)";
            } else {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid attribute";
                result.suggestion = "ODBC spec requires HY092 for invalid connection attribute";
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLSetConnectAttr accepted invalid attribute 99999";
            result.severity = Severity::WARNING;
            result.suggestion = "Driver should return HY092 for unrecognized attributes";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_closecursor_no_cursor() {
    TestResult result = make_result(
        "test_closecursor_no_cursor",
        "SQLCloseCursor",
        TestStatus::PASS,
        "SQL_ERROR with SQLSTATE 24000 when no cursor open",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLCloseCursor"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Close cursor on freshly allocated statement - no cursor open
        SQLRETURN rc = SQLCloseCursor(stmt.get_handle());
        
        if (rc == SQL_ERROR) {
            std::string state = get_stmt_sqlstate(stmt.get_handle());
            if (state == "24000") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with 24000 (Invalid cursor state) - no cursor open";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 24000)";
                result.severity = Severity::WARNING;
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLCloseCursor succeeded with no open cursor";
            result.severity = Severity::WARNING;
            result.suggestion = "ODBC spec requires 24000 when closing a cursor that isn't open";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult SqlstateTests::test_connect_already_connected() {
    TestResult result = make_result(
        "test_connect_already_connected",
        "SQLDriverConnect",
        TestStatus::PASS,
        "SQL_ERROR when already connected",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDriverConnect, Connection Transitions"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // The connection is already established - try to connect again
        SQLRETURN rc = SQLDriverConnect(
            conn_.get_handle(),
            nullptr,
            (SQLCHAR*)"Driver={Mock};",
            SQL_NTS,
            nullptr, 0, nullptr,
            SQL_DRIVER_NOPROMPT
        );
        
        if (rc == SQL_ERROR) {
            std::string state = get_conn_sqlstate(conn_.get_handle());
            if (state == "08002" || state == "HY010") {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with " + state + " - correctly rejected double connect";
            } else {
                result.status = TestStatus::PASS;
                result.actual = "SQL_ERROR with SQLSTATE=" + state + " - rejected double connect";
            }
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLDriverConnect succeeded on already-connected handle";
            result.severity = Severity::ERR;
            result.suggestion = "Driver should reject connection on already-connected handle";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
