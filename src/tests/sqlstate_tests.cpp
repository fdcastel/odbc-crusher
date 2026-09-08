#include "sqlstate_tests.hpp"
#include "core/guarded_buffer.hpp"
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
    core::GuardedBuffer<SQLCHAR> sqlstate(6, 0);  // D62
    SQLINTEGER native = 0;
    core::GuardedBuffer<SQLCHAR> msg(256, 0);  // D62
    SQLSMALLINT msg_len = 0;
    SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1,
                                 sqlstate.data(), &native, msg.data(),
                                 msg.declared_bytes(), &msg_len);
    if (SQL_SUCCEEDED(rc)) {
        return core::sqlstate_string(sqlstate.data());
    }
    return "";
}

std::string SqlstateTests::get_conn_sqlstate(SQLHDBC hdbc) {
    core::GuardedBuffer<SQLCHAR> sqlstate(6, 0);  // D62
    SQLINTEGER native = 0;
    core::GuardedBuffer<SQLCHAR> msg(256, 0);  // D62
    SQLSMALLINT msg_len = 0;
    SQLRETURN rc = SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1,
                                 sqlstate.data(), &native, msg.data(),
                                 msg.declared_bytes(), &msg_len);
    if (SQL_SUCCEEDED(rc)) {
        return core::sqlstate_string(sqlstate.data());
    }
    return "";
}

TestResult SqlstateTests::test_execute_without_prepare() {
    return run_test(
        "test_execute_without_prepare", "SQLExecute",
        "SQL_ERROR with SQLSTATE HY010",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecute, Appendix B: State Transition Tables",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            SQLRETURN rc = SQLExecute(stmt.get_handle());

            if (rc == SQL_ERROR) {
                std::string state = get_stmt_sqlstate(stmt.get_handle());
                if (state == "HY010") {
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with HY010 (Function sequence error)";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected HY010)";
                    r.severity = Severity::WARNING;
                    r.suggestion = "ODBC spec requires HY010 for SQLExecute without SQLPrepare";
                }
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLExecute did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
                r.severity = Severity::ERR;
            }
        });
}

TestResult SqlstateTests::test_fetch_no_cursor() {
    return run_test(
        "test_fetch_no_cursor", "SQLFetch",
        "SQL_ERROR with SQLSTATE HY010 (24000 tolerated)",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLFetch, Appendix B: Statement Transitions",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // SQLFetch without any prior execute - no cursor open
            SQLRETURN rc = SQLFetch(stmt.get_handle());

            if (rc == SQL_ERROR) {
                std::string state = get_stmt_sqlstate(stmt.get_handle());
                // A26: this had the two states the wrong way round. The
                // statement here is freshly allocated with no prepare and
                // no execute, which is Appendix B state S1, and the
                // transition table gives HY010 (function sequence error)
                // for SQLFetch in S1. 24000 (invalid cursor state) is the
                // answer from S2/S3, where a statement exists but no cursor
                // is open. Many drivers return 24000 here anyway, so it
                // stays a PASS — but the suggestion no longer tells a
                // correct driver that it got it wrong.
                if (state == "HY010") {
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with HY010 (Function sequence error)";
                } else if (state == "24000") {
                    // A23 - see above.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with 24000 (Invalid cursor state) - "
                               "tolerated alternative";
                    r.suggestion = "Appendix B gives HY010 for SQLFetch on a "
                                   "statement that has never been prepared or "
                                   "executed (state S1); 24000 is the state S2/S3 "
                                   "answer. Both are widely returned.";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQL_ERROR but SQLSTATE=" + state +
                               " (expected HY010, or 24000)";
                    r.severity = Severity::WARNING;
                }
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLFetch did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
                r.severity = Severity::ERR;
            }
        });
}

TestResult SqlstateTests::test_getdata_col0_no_bookmark() {
    return run_test(
        "test_getdata_col0_no_bookmark", "SQLGetData",
        "SQL_ERROR with SQLSTATE 07009 for column 0",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Descriptor Index",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Execute a query to get a result set
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
                if (stmt.fetch()) {
                    // Now try SQLGetData with column 0 (bookmark) when bookmarks aren't enabled
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 0, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (rc == SQL_ERROR) {
                        std::string state = get_stmt_sqlstate(stmt.get_handle());
                        if (state == "07009") {
                            // A23: driver-manager enforced. Both the Windows DM and unixODBC
                            // check the Appendix B state table before dispatching, so this
                            // branch is reached whatever the driver does - it is a fact about
                            // the stack, not about the driver under test. Reported, not scored
                            // (B2). The failure branches below stay as they are: a wrong
                            // SQLSTATE is still worth reporting, whoever produced it.
                            r.status = TestStatus::INFORMATIONAL;
                            r.actual = "SQL_ERROR with 07009 (Invalid descriptor index) for column 0";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 07009)";
                            r.severity = Severity::WARNING;
                        }
                    } else {
                        r.status = TestStatus::FAIL;
                        r.actual = "SQLGetData(col=0) did not return SQL_ERROR (rc=" + std::to_string(rc) + ")";
                        r.severity = Severity::WARNING;
                        r.suggestion = "Driver should return 07009 for column 0 unless bookmarks are enabled";
                    }
                    success = true;
                    break;
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query to test column 0 access";
            }
        });
}

TestResult SqlstateTests::test_getdata_col_out_of_range() {
    return run_test(
        "test_getdata_col_out_of_range", "SQLGetData",
        "SQL_ERROR with SQLSTATE 07009 for column > num_cols",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Descriptor Index",
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
                if (stmt.fetch()) {
                    // Try a column way beyond what exists
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 999, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (rc == SQL_ERROR) {
                        std::string state = get_stmt_sqlstate(stmt.get_handle());
                        if (state == "07009") {
                            r.status = TestStatus::PASS;
                            r.actual = "SQL_ERROR with 07009 (Invalid descriptor index) for column 999";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 07009)";
                            r.severity = Severity::WARNING;
                        }
                    } else {
                        r.status = TestStatus::FAIL;
                        r.actual = "SQLGetData(col=999) did not return SQL_ERROR";
                        r.severity = Severity::WARNING;
                    }
                    success = true;
                    break;
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query to test out-of-range column";
            }
        });
}

TestResult SqlstateTests::test_execdirect_syntax_error() {
    return run_test(
        "test_execdirect_syntax_error", "SQLExecDirect",
        "SQL_ERROR with SQLSTATE 42000 for syntax error",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLExecDirect",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            SQLRETURN rc = SQLExecDirect(
                stmt.get_handle(),
                (SQLCHAR*)kInvalidSql,
                SQL_NTS
            );

            if (rc == SQL_ERROR) {
                std::string state = get_stmt_sqlstate(stmt.get_handle());
                if (state == "42000") {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with 42000 (Syntax error or access violation)";
                } else if (state.substr(0, 2) == "42") {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with SQLSTATE=" + state + " (42xxx class - syntax/access error)";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 42000)";
                    r.severity = Severity::WARNING;
                    r.suggestion = "ODBC spec requires 42000 (Syntax error) for invalid SQL";
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "Driver accepted invalid SQL without error";
                r.severity = Severity::ERR;
            }
        });
}

TestResult SqlstateTests::test_bindparam_invalid_ctype() {
    return run_test(
        "test_bindparam_invalid_ctype", "SQLBindParameter",
        "SQL_ERROR with SQLSTATE HY003 for invalid C type",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLBindParameter",
        [&](TestResult& r) {
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
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with HY003 (Invalid application buffer type)";
                } else {
                    // A23 - see above.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid C type";
                    r.suggestion = "ODBC spec requires HY003 for invalid application buffer type";
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLBindParameter accepted invalid C type 9999";
                r.severity = Severity::WARNING;
                r.suggestion = "Driver should validate C type and return HY003 for invalid values";
            }
        });
}

TestResult SqlstateTests::test_getinfo_invalid_type() {
    return run_test(
        "test_getinfo_invalid_type", "SQLGetInfo",
        "SQL_ERROR with SQLSTATE HY096 for invalid info type",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetInfo",
        [&](TestResult& r) {
            // D61: guarded, like every other SQLGetInfo string path. This
            // one asks for an info type no driver knows, so the buffer should
            // come back untouched - but "should" is the assumption the guard
            // exists to stop the tool from making.
            constexpr size_t kCapacity = 256;
            core::GuardedBuffer<char> buffer(kCapacity, '\0');
            SQLSMALLINT len = 0;

            // Use an invalid info type (65535)
            SQLRETURN rc = SQLGetInfo(
                conn_.get_handle(),
                65535,
                buffer.data(), static_cast<SQLSMALLINT>(kCapacity), &len
            );

            if (rc == SQL_ERROR) {
                std::string state = get_conn_sqlstate(conn_.get_handle());
                if (state == "HY096") {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with HY096 (Information type out of range)";
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid info type";
                    r.suggestion = "ODBC spec requires HY096 for invalid SQLGetInfo info type";
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetInfo accepted invalid info type 65535";
                r.severity = Severity::WARNING;
                r.suggestion = "Driver should return HY096 for unrecognized information type";
            }
        });
}

TestResult SqlstateTests::test_setconnattr_invalid_attr() {
    return run_test(
        "test_setconnattr_invalid_attr", "SQLSetConnectAttr",
        "SQL_ERROR with SQLSTATE HY092 for invalid attribute",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLSetConnectAttr",
        [&](TestResult& r) {
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
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with HY092 (Invalid attribute/option identifier)";
                } else {
                    // A23 - see above.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with SQLSTATE=" + state + " for invalid attribute";
                    r.suggestion = "ODBC spec requires HY092 for invalid connection attribute";
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLSetConnectAttr accepted invalid attribute 99999";
                r.severity = Severity::WARNING;
                r.suggestion = "Driver should return HY092 for unrecognized attributes";
            }
        });
}

TestResult SqlstateTests::test_closecursor_no_cursor() {
    return run_test(
        "test_closecursor_no_cursor", "SQLCloseCursor",
        "SQL_ERROR with SQLSTATE 24000 when no cursor open",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCloseCursor",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Close cursor on freshly allocated statement - no cursor open
            SQLRETURN rc = SQLCloseCursor(stmt.get_handle());

            if (rc == SQL_ERROR) {
                std::string state = get_stmt_sqlstate(stmt.get_handle());
                if (state == "24000") {
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQL_ERROR with 24000 (Invalid cursor state) - no cursor open";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQL_ERROR but SQLSTATE=" + state + " (expected 24000)";
                    r.severity = Severity::WARNING;
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLCloseCursor succeeded with no open cursor";
                r.severity = Severity::WARNING;
                r.suggestion = "ODBC spec requires 24000 when closing a cursor that isn't open";
            }
        });
}

TestResult SqlstateTests::test_connect_already_connected() {
    return run_test(
        "test_connect_already_connected", "SQLDriverConnect",
        "SQL_ERROR when already connected",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLDriverConnect, Connection Transitions",
        [&](TestResult& r) {
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
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with " + state + " - correctly rejected double connect";
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR with SQLSTATE=" + state + " - rejected double connect";
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLDriverConnect succeeded on already-connected handle";
                r.severity = Severity::ERR;
                r.suggestion = "Driver should reject connection on already-connected handle";
            }
        });
}

} // namespace odbc_crusher::tests
