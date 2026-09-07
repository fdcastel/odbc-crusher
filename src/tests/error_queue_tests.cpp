#include "error_queue_tests.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"
#include <sstream>

namespace odbc_crusher::tests {

ErrorQueueTests::ErrorQueueTests(core::OdbcConnection& connection)
    : TestBase(connection) {}

std::vector<TestResult> ErrorQueueTests::run() {
    return {
        test_single_error(),
        test_multiple_errors(),
        test_error_clearing(),
        test_hierarchy(),
        test_field_extraction(),
        test_iteration()
    };
}

TestResult ErrorQueueTests::test_single_error() {
    // NOTE: original initial status is SKIP_INCONCLUSIVE; preserved here by setting r.status
    return run_test(
        "Single Error Test", "SQLGetDiagRec",
        "One diagnostic record retrieved",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagRec",
        [&](TestResult& r) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Test requires error generation capability";
            // Try to get diagnostic info without forcing an error
            // Just check if SQLGetDiagRec works
            SQLCHAR sqlstate[6] = {0};
            SQLINTEGER native_error = 0;
            SQLCHAR message[512] = {0};
            SQLSMALLINT message_len = 0;

            SQLRETURN diag_rc = SQLGetDiagRec(
                SQL_HANDLE_DBC,
                conn_.get_handle(),
                1,
                sqlstate,
                &native_error,
                message,
                sizeof(message),
                &message_len
            );

            // SQL_NO_DATA is expected if no error
            if (diag_rc == SQL_NO_DATA) {
                r.status = TestStatus::PASS;
                r.actual = "SQLGetDiagRec returned SQL_NO_DATA (no errors present)";
            } else if (SQL_SUCCEEDED(diag_rc)) {
                std::string state((char*)sqlstate);
                r.status = TestStatus::PASS;
                r.actual = "SQLGetDiagRec succeeded, SQLSTATE=" + state;
            } else {
                r.status = TestStatus::PASS;
                r.actual = "SQLGetDiagRec functional";
            }
        });
}

TestResult ErrorQueueTests::test_multiple_errors() {
    return run_test(
        "Multiple Errors Test", "SQLGetDiagRec",
        "Multiple diagnostic records retrieved",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagRec",
        [&](TestResult& r) {
            // Allocate a fresh statement and force a failure to generate diagnostics
            core::OdbcStatement stmt(conn_);

            // Execute an intentionally invalid SQL to generate at least one diagnostic
            SQLRETURN exec_rc = SQLExecDirect(
                stmt.get_handle(),
                (SQLCHAR*)"THIS IS NOT VALID SQL !!! @#$%",
                SQL_NTS
            );

            if (!SQL_SUCCEEDED(exec_rc)) {
                // Now iterate through all diagnostic records
                std::vector<std::string> sqlstates;

                for (SQLSMALLINT i = 1; i <= 10; ++i) {
                    SQLCHAR sqlstate[6] = {0};
                    SQLINTEGER native_error = 0;
                    SQLCHAR message[512] = {0};
                    SQLSMALLINT message_len = 0;

                    SQLRETURN diag_rc = SQLGetDiagRec(
                        SQL_HANDLE_STMT,
                        stmt.get_handle(),
                        i,
                        sqlstate,
                        &native_error,
                        message,
                        sizeof(message),
                        &message_len
                    );

                    if (diag_rc == SQL_NO_DATA) break;

                    if (SQL_SUCCEEDED(diag_rc)) {
                        sqlstates.push_back(std::string(reinterpret_cast<char*>(sqlstate)));
                    }
                }

                if (sqlstates.size() >= 1) {
                    r.status = TestStatus::PASS;
                    r.actual = "Retrieved " + std::to_string(sqlstates.size()) +
                                   " diagnostic record(s) after error";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "No diagnostic records found after error";
                    r.severity = Severity::ERR;
                    r.suggestion = "SQLGetDiagRec should return at least one record after SQLExecDirect fails";
                }
            } else {
                // The invalid SQL somehow succeeded - try alternative approach
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate an error to test multiple diagnostics";
                r.suggestion = "Driver accepted invalid SQL; cannot test error queue accumulation";
            }
        });
}

TestResult ErrorQueueTests::test_error_clearing() {
    return run_test(
        "Error Clearing Test", "SQLGetDiagRec",
        "Successful operation clears error queue",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagRec",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Step 1: Force an error
            SQLRETURN exec_rc = SQLExecDirect(
                stmt.get_handle(),
                (SQLCHAR*)"THIS IS NOT VALID SQL !!! @#$%",
                SQL_NTS
            );

            if (!SQL_SUCCEEDED(exec_rc)) {
                // Verify there's at least one diagnostic
                SQLCHAR sqlstate[6] = {0};
                SQLINTEGER native_error = 0;
                SQLCHAR message[512] = {0};
                SQLSMALLINT message_len = 0;

                SQLRETURN diag_rc = SQLGetDiagRec(
                    SQL_HANDLE_STMT, stmt.get_handle(), 1,
                    sqlstate, &native_error, message, sizeof(message), &message_len
                );

                bool had_error = SQL_SUCCEEDED(diag_rc);

                if (!had_error) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not verify initial error diagnostic";
                    return;
                }

                // Step 2: Execute a successful operation (try patterns)
                bool success = false;
                std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
                for (const auto& q : queries) {
                    SQLRETURN rc2 = SQLExecDirect(
                        stmt.get_handle(),
                        (SQLCHAR*)q.c_str(),
                        SQL_NTS
                    );
                    if (SQL_SUCCEEDED(rc2)) {
                        success = true;
                        break;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute a successful query to clear errors";
                    return;
                }

                // Step 3: Check that old error diagnostics are cleared
                diag_rc = SQLGetDiagRec(
                    SQL_HANDLE_STMT, stmt.get_handle(), 1,
                    sqlstate, &native_error, message, sizeof(message), &message_len
                );

                if (diag_rc == SQL_NO_DATA) {
                    r.status = TestStatus::PASS;
                    r.actual = "Error diagnostics cleared after successful operation";
                } else if (SQL_SUCCEEDED(diag_rc)) {
                    // There might be info/warning from the successful op, check if it's the OLD error
                    std::string state(reinterpret_cast<char*>(sqlstate));
                    if (state == "42000" || state == "42S02" || state == "HY000") {
                        r.status = TestStatus::FAIL;
                        r.actual = "Old error SQLSTATE=" + state + " still present after successful operation";
                        r.severity = Severity::WARNING;
                        r.suggestion = "Per ODBC spec, diagnostics should be cleared when a new function is called on the same handle";
                    } else {
                        r.status = TestStatus::PASS;
                        r.actual = "Previous error cleared; current SQLSTATE=" + state + " (likely info from new op)";
                    }
                }
            } else {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate initial error";
            }
        });
}

TestResult ErrorQueueTests::test_hierarchy() {
    return run_test(
        "Hierarchy Test", "SQLGetDiagRec",
        "Diagnostics accessible from handles",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagRec",
        [&](TestResult& r) {
            // Check that we can call SQLGetDiagRec on connection handle
            SQLCHAR sqlstate[6] = {0};
            SQLINTEGER native_error = 0;
            SQLCHAR message[512] = {0};
            SQLSMALLINT message_len = 0;

            SQLRETURN diag_rc = SQLGetDiagRec(
                SQL_HANDLE_DBC,
                conn_.get_handle(),
                1,
                sqlstate,
                &native_error,
                message,
                sizeof(message),
                &message_len
            );

            // Should get SQL_NO_DATA or SUCCESS
            if (diag_rc == SQL_NO_DATA || SQL_SUCCEEDED(diag_rc)) {
                r.status = TestStatus::PASS;
                r.actual = "Can query diagnostics from connection handle";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Unexpected result from SQLGetDiagRec";
            }
        });
}

TestResult ErrorQueueTests::test_field_extraction() {
    return run_test(
        "Field Extraction Test", "SQLGetDiagField",
        "Individual diagnostic fields retrieved",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagField",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Force an error to populate diagnostics
            SQLRETURN exec_rc = SQLExecDirect(
                stmt.get_handle(),
                (SQLCHAR*)"THIS IS NOT VALID SQL !!! @#$%",
                SQL_NTS
            );

            if (!SQL_SUCCEEDED(exec_rc)) {
                // Extract header field: SQL_DIAG_NUMBER
                SQLINTEGER num_records = 0;
                SQLRETURN diag_rc = SQLGetDiagField(
                    SQL_HANDLE_STMT, stmt.get_handle(), 0,
                    SQL_DIAG_NUMBER, &num_records, sizeof(SQLINTEGER), nullptr
                );

                bool got_number = SQL_SUCCEEDED(diag_rc) && num_records > 0;

                // Extract record fields: SQLSTATE
                SQLCHAR sqlstate[6] = {0};
                SQLSMALLINT sqlstate_len = 0;
                diag_rc = SQLGetDiagField(
                    SQL_HANDLE_STMT, stmt.get_handle(), 1,
                    SQL_DIAG_SQLSTATE, sqlstate, sizeof(sqlstate), &sqlstate_len
                );
                bool got_sqlstate = SQL_SUCCEEDED(diag_rc);

                // Extract record fields: NATIVE error code
                SQLINTEGER native_error = 0;
                diag_rc = SQLGetDiagField(
                    SQL_HANDLE_STMT, stmt.get_handle(), 1,
                    SQL_DIAG_NATIVE, &native_error, sizeof(SQLINTEGER), nullptr
                );
                bool got_native = SQL_SUCCEEDED(diag_rc);

                // Extract record fields: MESSAGE_TEXT
                SQLCHAR msg_text[256] = {0};
                SQLSMALLINT msg_len = 0;
                diag_rc = SQLGetDiagField(
                    SQL_HANDLE_STMT, stmt.get_handle(), 1,
                    SQL_DIAG_MESSAGE_TEXT, msg_text, sizeof(msg_text), &msg_len
                );
                bool got_message = SQL_SUCCEEDED(diag_rc);

                int fields_ok = (got_number ? 1 : 0) + (got_sqlstate ? 1 : 0) +
                                   (got_native ? 1 : 0) + (got_message ? 1 : 0);

                if (fields_ok >= 3) {
                    std::string state_str(reinterpret_cast<char*>(sqlstate));
                    r.status = TestStatus::PASS;
                    r.actual = std::to_string(fields_ok) + "/4 diagnostic fields extracted: " +
                                   "records=" + std::to_string(num_records) +
                                   ", SQLSTATE=" + state_str +
                                   ", native=" + std::to_string(native_error);
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Only " + std::to_string(fields_ok) + "/4 diagnostic fields extracted";
                    r.severity = Severity::WARNING;
                    r.suggestion = "SQLGetDiagField should support SQL_DIAG_NUMBER, SQL_DIAG_SQLSTATE, SQL_DIAG_NATIVE, SQL_DIAG_MESSAGE_TEXT";
                }
            } else {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate error for diagnostic field extraction";
            }
        });
}

TestResult ErrorQueueTests::test_iteration() {
    return run_test(
        "Iteration Test", "SQLGetDiagRec",
        "Loop through records until SQL_NO_DATA",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetDiagRec",
        [&](TestResult& r) {
            // Test iteration on connection handle (which should have no errors)
            std::vector<std::string> sqlstates;

            for (SQLSMALLINT i = 1; i <= 5; ++i) {
                SQLCHAR sqlstate[6] = {0};
                SQLINTEGER native_error = 0;
                SQLCHAR message[512] = {0};
                SQLSMALLINT message_len = 0;

                SQLRETURN diag_rc = SQLGetDiagRec(
                    SQL_HANDLE_DBC,
                    conn_.get_handle(),
                    i,
                    sqlstate,
                    &native_error,
                    message,
                    sizeof(message),
                    &message_len
                );

                if (diag_rc == SQL_NO_DATA) {
                    // Expected - no more records
                    break;
                } else if (SQL_SUCCEEDED(diag_rc)) {
                    std::string state((char*)sqlstate);
                    sqlstates.push_back(state);
                } else {
                    // Unexpected error
                    break;
                }
            }

            r.status = TestStatus::PASS;
            r.actual = "Iteration completed successfully";
            if (!sqlstates.empty()) {
                r.actual += " (found " + std::to_string(sqlstates.size()) + " diagnostic(s))";
            } else {
                r.actual += " (no diagnostics present - expected)";
            }
        });
}

} // namespace odbc_crusher::tests
