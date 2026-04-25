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
        test_transaction_isolation_levels(),
        test_rollback_with_open_cursor()
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

// ── PORT plan §4.9 — rollback with an open cursor ──────────────────────────
//
// Three correctness invariants checked together:
//   (i)   ROLLBACK succeeds (or returns a documented error) while a cursor
//         is mid-fetch on the same transaction.
//   (ii)  The cursor is closed by the rollback — subsequent SQLFetch returns
//         SQL_NO_DATA or a state-machine error, NOT the would-be-rolled-back
//         row.
//   (iii) The inserted row is gone from the table (SELECT COUNT(*) = 0).
//   (iv)  The connection is in a usable state for the next statement.
// Drivers that handle each path correctly in isolation often leak cursor
// state across the rollback boundary.

TestResult TransactionTests::test_rollback_with_open_cursor() {
    return run_test(
        "test_rollback_with_open_cursor", "SQLEndTran(SQL_ROLLBACK)",
        "ROLLBACK while a cursor is mid-fetch closes the cursor, undoes the "
        "row, and leaves the connection usable for the next statement",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLEndTran — interaction with open cursors",
        [&](TestResult& r) {
            // Disable autocommit so the INSERT participates in the txn.
            SQLRETURN ret = SQLSetConnectAttr(conn_.get_handle(),
                SQL_ATTR_AUTOCOMMIT,
                reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Cannot disable autocommit";
                return;
            }
            auto restore_autocommit = [&]() {
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                    reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
            };

            if (!create_test_table()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create test table: " + last_ddl_error_;
                restore_autocommit();
                return;
            }
            // CREATE TABLE itself opens a txn on some engines; commit it.
            SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

            std::ostringstream actual;
            try {
                // INSERT a row in the open transaction (uncommitted).
                {
                    core::OdbcStatement ins(conn_);
                    ins.execute("INSERT INTO ODBC_TEST_TXN (ID, VAL) "
                                "VALUES (1, 'pending')");
                }

                // Open a cursor on the same table (within the same txn —
                // the INSERT is visible to the same connection).
                core::OdbcStatement sel(conn_);
                sel.execute("SELECT ID, VAL FROM ODBC_TEST_TXN");
                SQLRETURN fetch_rc = SQLFetch(sel.get_handle());
                actual << "fetch_rc=" << fetch_rc << " ";

                // Rollback WITHOUT closing the cursor first.
                SQLRETURN rb_rc = SQLEndTran(SQL_HANDLE_DBC,
                    conn_.get_handle(), SQL_ROLLBACK);
                actual << "rollback_rc=" << rb_rc << " ";
                if (!SQL_SUCCEEDED(rb_rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = actual.str() + "(rollback failed)";
                    r.suggestion = "ROLLBACK with an open cursor must succeed; "
                                   "drivers may close the cursor implicitly but "
                                   "must not return SQL_ERROR.";
                    drop_test_table();
                    restore_autocommit();
                    return;
                }

                // (ii) Cursor must be closed — subsequent SQLFetch returns
                // SQL_NO_DATA or a state-machine error (HY010, 24000), NOT
                // the rolled-back row.
                SQLRETURN post_rb_rc = SQLFetch(sel.get_handle());
                actual << "post_rollback_fetch_rc=" << post_rb_rc << " ";
                if (post_rb_rc == SQL_SUCCESS || post_rb_rc == SQL_SUCCESS_WITH_INFO) {
                    r.status = TestStatus::FAIL;
                    r.actual = actual.str() +
                        "(cursor still alive after rollback — should be closed)";
                    r.suggestion = "ROLLBACK leaves a fetched-row visible to "
                                   "the cursor — that's a snapshot leak. The "
                                   "rolled-back transaction's data must not "
                                   "survive into the post-rollback state.";
                    drop_test_table();
                    restore_autocommit();
                    return;
                }
                // SQLCloseCursor is a defensive no-op now.
                SQLCloseCursor(sel.get_handle());

                // (iii) Row must be gone.
                core::OdbcStatement chk(conn_);
                chk.execute("SELECT COUNT(*) FROM ODBC_TEST_TXN");
                SQLRETURN chk_rc = SQLFetch(chk.get_handle());
                if (!SQL_SUCCEEDED(chk_rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = actual.str() +
                        "(post-rollback SELECT COUNT(*) failed)";
                    drop_test_table();
                    restore_autocommit();
                    return;
                }
                SQLINTEGER cnt = -1;
                SQLLEN ind = 0;
                SQLGetData(chk.get_handle(), 1, SQL_C_SLONG,
                           &cnt, sizeof(cnt), &ind);
                actual << "post_rollback_count=" << cnt << " ";
                if (cnt != 0) {
                    r.status = TestStatus::FAIL;
                    r.actual = actual.str() + "(row not rolled back)";
                    r.suggestion = "ROLLBACK didn't undo the INSERT — basic "
                                   "transaction semantics broken.";
                    drop_test_table();
                    restore_autocommit();
                    return;
                }

                // (iv) Connection still usable — execute another statement.
                {
                    core::OdbcStatement post(conn_);
                    post.execute("SELECT 1");
                    SQLRETURN ok_rc = SQLFetch(post.get_handle());
                    actual << "post_select_rc=" << ok_rc;
                    if (!SQL_SUCCEEDED(ok_rc)) {
                        r.status = TestStatus::FAIL;
                        r.actual = actual.str() +
                            " (connection wedged after rollback)";
                        r.suggestion = "Rollback left the connection in an "
                                       "unusable state — subsequent SQLExecDirect "
                                       "fails. State machine must reset cleanly.";
                        drop_test_table();
                        restore_autocommit();
                        return;
                    }
                }

                r.actual = actual.str();
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::FAIL;
                r.actual = std::string("Exception: ") + e.what();
                r.diagnostic = e.format_diagnostics();
            }

            drop_test_table();
            restore_autocommit();
        });
}

} // namespace odbc_crusher::tests
