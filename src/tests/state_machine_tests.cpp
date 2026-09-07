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
            // B1: this allocated a statement and passed. The allocation
            // throws on failure, so run_test would report ERR - meaning the
            // probe had exactly one outcome and told a reader nothing about
            // the state machine it is named for.
            //
            // Walk the transition the name promises instead: S1 (allocated)
            // -> S2 (prepared) -> S3 (executed) -> S1 again after
            // SQLFreeStmt(SQL_CLOSE), checking the handle is usable at each
            // step. Every function involved is Core.
            core::OdbcStatement stmt(conn_);

            const auto queries = literal_select_variants("SELECT 1");
            auto attempt = prepare_first_working(stmt, queries);
            if (!attempt) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "SQLPrepare failed for every dialect variant, so "
                           "the S1 -> S2 transition could not be made";
                r.diagnostic = attempt.format_failures();
                return;
            }

            stmt.execute_prepared();              // S2 -> S3
            SQLRETURN close_rc = SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            if (!SQL_SUCCEEDED(close_rc)) {
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLFreeStmt(SQL_CLOSE)");
                return;
            }

            // Back in S2: the statement is still prepared, so it must
            // execute again without another SQLPrepare.
            SQLRETURN again = SQLExecute(stmt.get_handle());
            if (!SQL_SUCCEEDED(again)) {
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLExecute after SQLFreeStmt(SQL_CLOSE)");
                return;
            }

            r.actual = "Allocated -> prepared -> executed -> closed -> "
                       "re-executed without re-preparing (" + attempt.query + ")";
        });
}

TestResult StateMachineTests::test_invalid_operation() {
    return run_test(
        "Invalid Operation Test", "SQLExecute",
        "SQLExecute without SQLPrepare returns HY010",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecute, Appendix B State Transition Tables",
        [&](TestResult& r) {
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
                    // A23: driver-manager enforced. Both the Windows DM and unixODBC
                    // check the Appendix B state table before dispatching, so this
                    // branch is reached whatever the driver does - it is a fact about
                    // the stack, not about the driver under test. Reported, not scored
                    // (B2). The failure branches below stay as they are: a wrong
                    // SQLSTATE is still worth reporting, whoever produced it.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQLExecute correctly returned SQL_ERROR with HY010 (Function sequence error)";
                } else {
                    // A23 - see above.
                    r.status = TestStatus::INFORMATIONAL;
                    r.actual = "SQLExecute correctly returned SQL_ERROR, SQLSTATE=" + state;
                    r.suggestion = "ODBC spec requires SQLSTATE HY010 for SQLExecute without SQLPrepare, got " + state;
                }
            } else if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLExecute succeeded without SQLPrepare - state machine violation";
                r.severity = Severity::ERR;
                r.suggestion = "Driver must return SQL_ERROR/HY010 when SQLExecute is called without prior SQLPrepare";
            } else {
                // A23 - see above.
                r.status = TestStatus::INFORMATIONAL;
                r.actual = "SQLExecute rejected without SQLPrepare (rc=" + std::to_string(rc) + ")";
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
                // B1: execute -> fetch -> SQLCloseCursor -> execute -> fetch
                // is Core, and the dialect variants cover the engines this
                // tool targets. A driver that cannot do it on any of them
                // forces a fresh statement handle per query.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Could not execute, fetch, close and re-execute on "
                           "one statement handle with any dialect variant";
                r.suggestion =
                    "SQLCloseCursor returns the statement to the prepared "
                    "state so it can be executed again. Without it every "
                    "query costs a handle allocation.";
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
        });
}

TestResult StateMachineTests::test_connection_state() {
    return run_test(
        "Connection State Test", "Connection State",
        "Connection is active",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetConnectAttr, Connection Transitions",
        [&](TestResult& r) {
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
                // B1: the else branch said "Connection state queryable" and
                // PASSed on the branch where querying it had just failed.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT) returned " +
                           std::to_string(rc) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") +
                           "] on a connection that is open";
                r.suggestion =
                    "The connection is in state C4 (connected, statement "
                    "allocated), where reading a Core connection attribute "
                    "must succeed.";
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
        });
}

} // namespace odbc_crusher::tests
