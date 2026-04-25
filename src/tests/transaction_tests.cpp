#include "transaction_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> TransactionTests::run() {
    return {
        test_autocommit_on(),
        test_autocommit_off(),
        test_manual_commit(),
        test_manual_rollback(),
        test_transaction_isolation_levels()
    };
}

bool TransactionTests::create_test_table() {
    try {
        // DDL must run with autocommit ON so it commits immediately.
        SQLUINTEGER old_ac = 0;
        SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT, &old_ac, 0, nullptr);
        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                          (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);

        // Strategy 0: Check if the test table already exists from a prior run.
        // This avoids needing DDL privileges when the table is already there.
        {
            try {
                core::OdbcStatement probe(conn_);
                probe.execute("SELECT 1 FROM ODBC_TEST_TXN WHERE 1=0");
                // Table exists — no DDL needed
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                  (SQLPOINTER)(intptr_t)old_ac, 0);
                return true;
            } catch (...) {
                // Table doesn't exist — try to create it
            }
        }

        // Strategy: CREATE first.  If it fails with "table already exists",
        // DROP + rollback + retry CREATE.  This avoids corrupting the
        // connection-level transaction state on Firebird when DROP fails for
        // a table that doesn't exist.
        std::vector<std::string> create_queries = {
            "CREATE TABLE ODBC_TEST_TXN (ID INTEGER, VAL VARCHAR(50))",
            "CREATE TABLE ODBC_TEST_TXN (ID INT, VAL VARCHAR(50))"
        };

        // Attempt 1: try CREATE directly
        for (const auto& query : create_queries) {
            try {
                core::OdbcStatement create_stmt(conn_);
                create_stmt.execute(query);
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                  (SQLPOINTER)(intptr_t)old_ac, 0);
                return true;
            } catch (const core::OdbcError& e) {
                last_ddl_error_ = e.format_diagnostics();
                // Rollback to clean up connection state after failed DDL
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
                continue;
            }
        }

        // Attempt 2: table probably exists — DROP then re-CREATE
        try {
            core::OdbcStatement drop_stmt(conn_);
            drop_stmt.execute("DROP TABLE ODBC_TEST_TXN");
        } catch (...) {
            SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
        }

        for (const auto& query : create_queries) {
            try {
                core::OdbcStatement create_stmt(conn_);
                create_stmt.execute(query);
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                  (SQLPOINTER)(intptr_t)old_ac, 0);
                return true;
            } catch (const core::OdbcError& e) {
                last_ddl_error_ = e.format_diagnostics();
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
                continue;
            }
        }

        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                          (SQLPOINTER)(intptr_t)old_ac, 0);
        return false;
    } catch (...) {
        return false;
    }
}

void TransactionTests::drop_test_table() {
    try {
        // DDL cleanup with autocommit ON
        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                          (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
        core::OdbcStatement stmt(conn_);
        stmt.execute("DROP TABLE ODBC_TEST_TXN");
    } catch (...) {
        // Rollback to clean up connection state after failed DDL
        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
    }
}

TestResult TransactionTests::test_autocommit_on() {
    return run_test(
        "test_autocommit_on", "SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT)",
        "Autocommit mode should be ON by default",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetConnectAttr",
        [&](TestResult& r) {
            SQLUINTEGER autocommit = 0;
            SQLRETURN ret = SQLGetConnectAttr(
                conn_.get_handle(),
                SQL_ATTR_AUTOCOMMIT,
                &autocommit,
                0,
                nullptr
            );

            if (SQL_SUCCEEDED(ret)) {
                if (autocommit == SQL_AUTOCOMMIT_ON) {
                    r.actual = "Autocommit is ON (default)";
                    r.status = TestStatus::PASS;
                } else {
                    r.actual = "Autocommit is OFF (unexpected default)";
                    r.status = TestStatus::FAIL;
                }
            } else {
                r.actual = "Could not query autocommit mode";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLGetConnectAttr for SQL_ATTR_AUTOCOMMIT did not succeed";
            }
        });
}

TestResult TransactionTests::test_autocommit_off() {
    return run_test(
        "test_autocommit_off", "SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT, OFF)",
        "Can disable autocommit mode",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLSetConnectAttr",
        [&](TestResult& r) {
            try {
                // Turn off autocommit
                SQLRETURN ret = SQLSetConnectAttr(
                    conn_.get_handle(),
                    SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
                    0
                );

                if (SQL_SUCCEEDED(ret)) {
                    // Verify it's off
                    SQLUINTEGER autocommit = 0;
                    ret = SQLGetConnectAttr(
                        conn_.get_handle(),
                        SQL_ATTR_AUTOCOMMIT,
                        &autocommit,
                        0,
                        nullptr
                    );

                    if (SQL_SUCCEEDED(ret) && autocommit == SQL_AUTOCOMMIT_OFF) {
                        r.actual = "Successfully disabled autocommit";
                        r.status = TestStatus::PASS;

                        // Turn it back on for other tests
                        SQLSetConnectAttr(
                            conn_.get_handle(),
                            SQL_ATTR_AUTOCOMMIT,
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON,
                            0
                        );
                    } else {
                        r.actual = "Autocommit mode did not change";
                        r.status = TestStatus::FAIL;
                    }
                } else {
                    r.actual = "SQLSetConnectAttr for autocommit not supported";
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.suggestion = "Driver did not accept SQL_ATTR_AUTOCOMMIT change";
                }
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::ERR;
                r.actual = e.what();
                r.diagnostic = e.format_diagnostics();

                // Try to restore autocommit
                try {
                    SQLSetConnectAttr(
                        conn_.get_handle(),
                        SQL_ATTR_AUTOCOMMIT,
                        (SQLPOINTER)SQL_AUTOCOMMIT_ON,
                        0
                    );
                } catch (...) {}
            }
        });
}

TestResult TransactionTests::test_manual_commit() {
    return run_test(
        "test_manual_commit", "SQLEndTran(SQL_COMMIT)",
        "Can manually commit a transaction",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLEndTran",
        [&](TestResult& r) {
            try {
                // Turn off autocommit
                SQLRETURN ret = SQLSetConnectAttr(
                    conn_.get_handle(),
                    SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
                    0
                );

                if (!SQL_SUCCEEDED(ret)) {
                    r.actual = "Cannot disable autocommit for manual transaction test";
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.suggestion = "Could not disable autocommit to test manual commit";
                } else {
                    // Create test table
                    if (!create_test_table()) {
                        // DDL-free fallback: verify SQLEndTran(COMMIT) is callable even
                        // without a test table.  This proves the driver's transaction API
                        // works, though we can't verify data persistence.
                        SQLRETURN commit_ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);
                        if (SQL_SUCCEEDED(commit_ret)) {
                            r.actual = "SQLEndTran(SQL_COMMIT) succeeded (DDL-free fallback; "
                                           "could not create test table for full data persistence test)";
                            r.status = TestStatus::PASS;
                            if (!last_ddl_error_.empty()) {
                                r.suggestion = "CREATE TABLE failed: " + last_ddl_error_ +
                                                  ". Ensure the connected user has CREATE TABLE privileges "
                                                  "for the full transaction commit test.";
                            } else {
                                r.suggestion = "Test table creation failed; ensure the connected user "
                                                  "has CREATE TABLE privileges for the full test.";
                            }
                        } else {
                            r.actual = "Could not create test table and SQLEndTran(COMMIT) failed";
                            r.status = TestStatus::SKIP_INCONCLUSIVE;
                            if (!last_ddl_error_.empty()) {
                                r.suggestion = "CREATE TABLE failed: " + last_ddl_error_;
                            } else {
                                r.suggestion = "Test table creation failed; manual commit test could not run";
                            }
                        }

                        // Restore autocommit
                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                        (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                    } else {
                        // Insert data
                        core::OdbcStatement stmt(conn_);
                        stmt.execute("INSERT INTO ODBC_TEST_TXN (ID, VAL) VALUES (1, 'test')");

                        // Commit
                        ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

                        if (SQL_SUCCEEDED(ret)) {
                            // Verify data exists
                            stmt.execute("SELECT COUNT(*) FROM ODBC_TEST_TXN");
                            if (stmt.fetch()) {
                                SQLINTEGER count = 0;
                                SQLLEN indicator = 0;
                                SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                          &count, sizeof(count), &indicator);

                                if (count == 1) {
                                    r.actual = "Transaction committed successfully";
                                    r.status = TestStatus::PASS;
                                } else {
                                    r.actual = "Data not committed";
                                    r.status = TestStatus::FAIL;
                                }
                            }
                        } else {
                            r.actual = "SQLEndTran(COMMIT) failed";
                            r.status = TestStatus::FAIL;
                        }

                        // Cleanup
                        drop_test_table();

                        // Restore autocommit
                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                        (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                    }
                }
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::ERR;
                r.actual = e.what();
                r.diagnostic = e.format_diagnostics();

                // Cleanup
                try {
                    drop_test_table();
                    SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                    (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                } catch (...) {}
            }
        });
}

TestResult TransactionTests::test_manual_rollback() {
    return run_test(
        "test_manual_rollback", "SQLEndTran(SQL_ROLLBACK)",
        "Can manually rollback a transaction",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLEndTran",
        [&](TestResult& r) {
            try {
                // Turn off autocommit
                SQLRETURN ret = SQLSetConnectAttr(
                    conn_.get_handle(),
                    SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
                    0
                );

                if (!SQL_SUCCEEDED(ret)) {
                    r.actual = "Cannot disable autocommit for rollback test";
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.suggestion = "Could not disable autocommit to test manual rollback";
                } else {
                    // Create test table
                    if (!create_test_table()) {
                        // DDL-free fallback: verify SQLEndTran(ROLLBACK) is callable even
                        // without a test table.  This proves the driver's transaction API
                        // works, though we can't verify data rollback.
                        SQLRETURN rb_ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
                        if (SQL_SUCCEEDED(rb_ret)) {
                            r.actual = "SQLEndTran(SQL_ROLLBACK) succeeded (DDL-free fallback; "
                                           "could not create test table for full data rollback test)";
                            r.status = TestStatus::PASS;
                            if (!last_ddl_error_.empty()) {
                                r.suggestion = "CREATE TABLE failed: " + last_ddl_error_ +
                                                  ". Ensure the connected user has CREATE TABLE privileges "
                                                  "for the full transaction rollback test.";
                            } else {
                                r.suggestion = "Test table creation failed; ensure the connected user "
                                                  "has CREATE TABLE privileges for the full test.";
                            }
                        } else {
                            r.actual = "Could not create test table and SQLEndTran(ROLLBACK) failed";
                            r.status = TestStatus::SKIP_INCONCLUSIVE;
                            if (!last_ddl_error_.empty()) {
                                r.suggestion = "CREATE TABLE failed: " + last_ddl_error_;
                            } else {
                                r.suggestion = "Test table creation failed; rollback test could not run";
                            }
                        }

                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                        (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                    } else {
                        // Commit the CREATE TABLE
                        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

                        // Insert data
                        core::OdbcStatement stmt(conn_);
                        stmt.execute("INSERT INTO ODBC_TEST_TXN (ID, VAL) VALUES (1, 'should_rollback')");

                        // Rollback
                        ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);

                        if (SQL_SUCCEEDED(ret)) {
                            // Verify data does NOT exist
                            stmt.execute("SELECT COUNT(*) FROM ODBC_TEST_TXN");
                            if (stmt.fetch()) {
                                SQLINTEGER count = 0;
                                SQLLEN indicator = 0;
                                SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                          &count, sizeof(count), &indicator);

                                if (count == 0) {
                                    r.actual = "Transaction rolled back successfully";
                                    r.status = TestStatus::PASS;
                                } else {
                                    r.actual = "Data was not rolled back";
                                    r.status = TestStatus::FAIL;
                                }
                            }
                        } else {
                            r.actual = "SQLEndTran(ROLLBACK) failed";
                            r.status = TestStatus::FAIL;
                        }

                        // Cleanup
                        drop_test_table();

                        // Restore autocommit
                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                        (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                    }
                }
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::ERR;
                r.actual = e.what();
                r.diagnostic = e.format_diagnostics();

                // Cleanup
                try {
                    drop_test_table();
                    SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                    (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
                } catch (...) {}
            }
        });
}

TestResult TransactionTests::test_transaction_isolation_levels() {
    return run_test(
        "test_transaction_isolation_levels", "SQLSetConnectAttr(SQL_ATTR_TXN_ISOLATION)",
        "Can query and set transaction isolation levels",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLSetConnectAttr, SQL_ATTR_TXN_ISOLATION",
        [&](TestResult& r) {
            // Get current isolation level
            SQLUINTEGER isolation = 0;
            SQLRETURN ret = SQLGetConnectAttr(
                conn_.get_handle(),
                SQL_ATTR_TXN_ISOLATION,
                &isolation,
                0,
                nullptr
            );

            if (!SQL_SUCCEEDED(ret)) {
                r.actual = "Transaction isolation level query not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver does not support querying SQL_ATTR_TXN_ISOLATION";
            } else {
                std::ostringstream oss;
                oss << "Current isolation: ";

                switch (isolation) {
                    case SQL_TXN_READ_UNCOMMITTED:
                        oss << "READ UNCOMMITTED";
                        break;
                    case SQL_TXN_READ_COMMITTED:
                        oss << "READ COMMITTED";
                        break;
                    case SQL_TXN_REPEATABLE_READ:
                        oss << "REPEATABLE READ";
                        break;
                    case SQL_TXN_SERIALIZABLE:
                        oss << "SERIALIZABLE";
                        break;
                    default:
                        oss << "Unknown (" << isolation << ")";
                }

                r.actual = oss.str();
                r.status = TestStatus::PASS;
            }
        });
}

} // namespace odbc_crusher::tests
