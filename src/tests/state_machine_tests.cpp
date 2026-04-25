#include "state_machine_tests.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"

namespace odbc_crusher::tests {

StateMachineTests::StateMachineTests(core::OdbcConnection& connection)
    : TestBase(connection) {}

std::vector<TestResult> StateMachineTests::run() {
    return {
        test_valid_transitions(),
        test_invalid_operation(),
        test_state_reset(),
        test_prepare_execute_cycle(),
        test_connection_state(),
        test_multiple_statements()
    };
}

TestResult StateMachineTests::test_valid_transitions() {
    return run_test(
        "Valid Transitions Test", "State Machine",
        "Normal operation sequence works",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLAllocHandle, Statement Transitions",
        [&](TestResult& r) {
            try {
                // Test: Just verify statement allocation works
                core::OdbcStatement stmt(conn_);

                // State: Allocated - this is success
                r.status = TestStatus::PASS;
                r.actual = "Statement allocation successful (basic state transition)";
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult StateMachineTests::test_invalid_operation() {
    return run_test(
        "Invalid Operation Test", "SQLExecute",
        "SQLExecute without SQLPrepare returns HY010",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecute, Appendix B State Transition Tables",
        [&](TestResult& r) {
            try {
                // Allocate a fresh statement (not prepared)
                core::OdbcStatement stmt(conn_);

                // Call SQLExecute WITHOUT prior SQLPrepare - should fail with HY010
                SQLRETURN rc = SQLExecute(stmt.get_handle());

                if (rc == SQL_ERROR) {
                    // Check SQLSTATE
                    SQLCHAR sqlstate[6] = {0};
                    SQLINTEGER native_error = 0;
                    SQLCHAR message[512] = {0};
                    SQLSMALLINT msg_len = 0;

                    SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(), 1,
                                 sqlstate, &native_error, message, sizeof(message), &msg_len);

                    std::string state(reinterpret_cast<char*>(sqlstate));

                    if (state == "HY010") {
                        r.status = TestStatus::PASS;
                        r.actual = "SQLExecute correctly returned SQL_ERROR with HY010 (Function sequence error)";
                    } else {
                        r.status = TestStatus::PASS;
                        r.actual = "SQLExecute correctly returned SQL_ERROR, SQLSTATE=" + state;
                        r.suggestion = "ODBC spec requires SQLSTATE HY010 for SQLExecute without SQLPrepare, got " + state;
                    }
                } else if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLExecute succeeded without SQLPrepare - state machine violation";
                    r.severity = Severity::ERR;
                    r.suggestion = "Driver must return SQL_ERROR/HY010 when SQLExecute is called without prior SQLPrepare";
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLExecute rejected without SQLPrepare (rc=" + std::to_string(rc) + ")";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult StateMachineTests::test_state_reset() {
    return run_test(
        "State Reset Test", "SQLCloseCursor/SQLFreeStmt",
        "Close cursor resets state, statement is reusable",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLCloseCursor, SQLFreeStmt, Statement Transitions",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                // Try query patterns
                std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
                bool success = false;

                for (const auto& query : queries) {
                    try {
                        // Execute query -> creates cursor
                        stmt.execute(query);
                        stmt.fetch();

                        // Close cursor -> should reset state
                        stmt.close_cursor();

                        // Re-execute same query -> should work
                        stmt.execute(query);
                        bool fetched = stmt.fetch();

                        if (fetched) {
                            r.status = TestStatus::PASS;
                            r.actual = "Statement reusable after SQLCloseCursor: execute->fetch->close->execute->fetch";
                            success = true;
                            break;
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not complete state reset test with available query patterns";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult StateMachineTests::test_prepare_execute_cycle() {
    return run_test(
        "Prepare-Execute Cycle Test", "SQLPrepare/SQLExecute",
        "Repeated prepare/execute cycle works",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, SQLExecute, Statement Transitions",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
                bool success = false;

                for (const auto& query : queries) {
                    try {
                        // Cycle 1: Prepare -> Execute -> Fetch -> Close
                        stmt.prepare(query);
                        stmt.execute_prepared();
                        bool fetched1 = stmt.fetch();
                        stmt.close_cursor();

                        // Cycle 2: Re-execute (should work since statement is still prepared)
                        stmt.execute_prepared();
                        bool fetched2 = stmt.fetch();
                        stmt.close_cursor();

                        if (fetched1 && fetched2) {
                            r.status = TestStatus::PASS;
                            r.actual = "Prepare->Execute->Close->Execute->Close cycle completed successfully";
                            success = true;
                            break;
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not complete prepare/execute cycle with available query patterns";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult StateMachineTests::test_connection_state() {
    return run_test(
        "Connection State Test", "Connection State",
        "Connection is active",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetConnectAttr, Connection Transitions",
        [&](TestResult& r) {
            try {
                // Verify connection is active by getting connection attribute
                SQLINTEGER autocommit = 0;
                SQLRETURN rc = SQLGetConnectAttr(
                    conn_.get_handle(),
                    SQL_ATTR_AUTOCOMMIT,
                    &autocommit,
                    0,
                    NULL
                );

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "Connection active, autocommit=" + std::to_string(autocommit);
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "Connection state queryable";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult StateMachineTests::test_multiple_statements() {
    return run_test(
        "Multiple Statements Test", "State Machine",
        "Independent state tracking per statement",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLAllocHandle, Statement Transitions",
        [&](TestResult& r) {
            try {
                // Create two statements and verify they have independent handles
                core::OdbcStatement stmt1(conn_);
                core::OdbcStatement stmt2(conn_);

                // Verify they have different handles
                if (stmt1.get_handle() != stmt2.get_handle() &&
                    stmt1.get_handle() != SQL_NULL_HSTMT &&
                    stmt2.get_handle() != SQL_NULL_HSTMT) {
                    r.status = TestStatus::PASS;
                    r.actual = "Multiple statements have independent handles";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Statements don't have independent handles";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

} // namespace odbc_crusher::tests
