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
        test_rollback_with_open_cursor(),
        test_disconnect_rolls_back_open_transaction()   // I8
    };
}

bool TransactionTests::create_test_table() {
    // C4: this was a 90-line reimplementation of RoundTripTableGuard - the
    // same reuse probe, the same INTEGER-then-INT ladder, the same
    // DROP-and-retry, the same autocommit dance. It is the guard's job now,
    // and the two behaviours that used to live only here (reuse when the
    // user cannot CREATE, and A15's clear-on-reuse) moved into it with the
    // rest.
    //
    // The guard is RAII, but these probes create and drop across several
    // functions, so it lives in an optional member and drop_test_table()
    // resets it. Restructuring each probe around a scoped guard is a
    // separate change and is not what C4 asks for.
    table_.reset();
    table_.emplace(conn_, "ODBC_TEST_TXN", "VARCHAR(50)");
    if (!table_->ok()) {
        last_ddl_error_ = table_->last_error();
        table_.reset();
        return false;
    }
    return true;
}

void TransactionTests::drop_test_table() {
    // C4. A14: this used to force autocommit ON and never put it back - the
    // last copy of that bug. The guard's destructor uses ScopedAutocommitOn.
    table_.reset();
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
                        // A14: say what this probe needs instead of trusting create_test_table()
                        // to have put it back. That helper runs its DDL with autocommit ON and
                        // restores whatever it read on the way out - and a driver that declines
                        // the read has nothing to restore. Re-asserting is correct whichever way
                        // the read goes. (The old code got this right by accident: its broken
                        // save read a failed SQLGetConnectAttr as 0, which *is* AUTOCOMMIT_OFF,
                        // which is exactly what these three probes want.)
                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                          reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
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
                                // A27: this return code was discarded. On a
                                // failed read `count` keeps its initial 0,
                                // which this probe reads as "not committed" -
                                // right verdict, wrong reason - and which the
                                // rollback probe reads as success outright.
                                SQLRETURN get_rc =
                                    SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                               &count, sizeof(count), &indicator);

                                if (!SQL_SUCCEEDED(get_rc)) {
                                    // B3 decides SKIP vs FAIL from the SQLSTATE.
                                    report_failure(r, SQL_HANDLE_STMT,
                                                   stmt.get_handle(),
                                                   "SQLGetData(COUNT(*))");
                                } else if (count == 1) {
                                    r.actual = "Transaction committed successfully";
                                    r.status = TestStatus::PASS;
                                } else {
                                    r.actual = "Data not committed (COUNT(*) = " +
                                               std::to_string(count) + ")";
                                    r.status = TestStatus::FAIL;
                                }
                            } else {
                                // A22: no `else` here meant a COUNT(*) that
                                // returned no row left run_test's PASS default
                                // standing, with an empty `actual`. A committed
                                // INSERT that the engine will not count is not
                                // a pass.
                                r.actual = "SELECT COUNT(*) returned no row "
                                           "after a successful COMMIT";
                                r.status = TestStatus::FAIL;
                            }
                        } else {
                            // A22: name the SQLSTATE. "SQLEndTran(COMMIT)
                            // failed" told a driver author nothing.
                            r.actual = "SQLEndTran(SQL_COMMIT) rc=" +
                                       std::to_string(ret) + " [" +
                                       first_sqlstate(SQL_HANDLE_DBC,
                                                      conn_.get_handle(),
                                                      "no diagnostic") + "]";
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
                        // A14 - re-assert what we need; see test_manual_commit.
                        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                          reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
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
                                // A27: the discarded return code mattered most
                                // here. `count` starts at 0 and 0 is this
                                // probe's PASS condition, so a driver whose
                                // SQLGetData failed outright was reported as
                                // having rolled back correctly.
                                SQLRETURN get_rc =
                                    SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                               &count, sizeof(count), &indicator);

                                if (!SQL_SUCCEEDED(get_rc)) {
                                    report_failure(r, SQL_HANDLE_STMT,
                                                   stmt.get_handle(),
                                                   "SQLGetData(COUNT(*))");
                                } else if (count == 0) {
                                    r.actual = "Transaction rolled back successfully";
                                    r.status = TestStatus::PASS;
                                } else {
                                    r.actual = "Data was not rolled back (COUNT(*) = " +
                                               std::to_string(count) + ")";
                                    r.status = TestStatus::FAIL;
                                }
                            } else {
                                // A22 - see test_manual_commit.
                                r.actual = "SELECT COUNT(*) returned no row "
                                           "after a successful ROLLBACK";
                                r.status = TestStatus::FAIL;
                            }
                        } else {
                            // A22: name the SQLSTATE.
                            r.actual = "SQLEndTran(SQL_ROLLBACK) rc=" +
                                       std::to_string(ret) + " [" +
                                       first_sqlstate(SQL_HANDLE_DBC,
                                                      conn_.get_handle(),
                                                      "no diagnostic") + "]";
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
                // B1: SQL_ATTR_TXN_ISOLATION is a Core connection attribute
                // with a defined default - every connection is running at
                // *some* isolation level, so it can always be reported.
                // Which levels are *settable* is what varies.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetConnectAttr(SQL_ATTR_TXN_ISOLATION) "
                           "returned " + std::to_string(ret) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") + "]";
                r.suggestion =
                    "Every connection has a current isolation level, so this "
                    "read must succeed. Supporting more than one level is the "
                    "optional part.";
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
            // A14 - re-assert what we need; see test_manual_commit.
            SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                              reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
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
                    execute_literal_select(post, "SELECT 1");
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

// I8 / PORT 9.B — SQLDisconnect with a transaction still open.
//
// ODBC is explicit: disconnecting with an open transaction rolls it back.
// A driver that commits it instead loses nothing visibly and silently
// contradicts the caller's intent, which is the class of bug this tool
// exists to name. It cannot be probed on `conn_` — the rest of the run needs
// that connection — so it opens one of its own, which is what C13 is for.
TestResult TransactionTests::test_disconnect_rolls_back_open_transaction() {
    return run_test(
        "test_disconnect_with_open_transaction", "SQLDisconnect",
        "25000 with the transaction intact, or a clean rollback",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLDisconnect",
        [&](TestResult& r) {
            auto sibling = open_sibling_connection();
            if (!sibling) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No connection string available to open a second "
                           "connection with";
                r.suggestion = "Run the tool normally; this probe cannot run "
                               "against a connection it did not open";
                return;
            }

            // The table lives on the *primary* connection, so it survives
            // whatever happens to the sibling and can be read afterwards.
            RoundTripTableGuard table(conn_, "ODBC_CRUSHER_DISCONNECT_TX",
                                      "INTEGER");
            if (!table.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create a table to test with: " +
                           table.last_error();
                return;
            }

            SQLRETURN rc = SQLSetConnectAttr(
                sibling->get_handle(), SQL_ATTR_AUTOCOMMIT,
                reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver declined SQL_AUTOCOMMIT_OFF (" +
                           first_sqlstate(SQL_HANDLE_DBC, sibling->get_handle(),
                                          "no SQLSTATE") + ")";
                return;
            }

            {
                core::OdbcStatement stmt(*sibling);
                stmt.execute("INSERT INTO " + table.name() + " VALUES (4242)");
            }

            // Disconnect without committing. Two answers are conformant and
            // the probe grades neither against the other:
            //
            //   * SQL_ERROR with 25000 — the behaviour SQLDisconnect's own
            //     documentation describes, transaction unchanged, connection
            //     still open;
            //   * success, having rolled the transaction back.
            //
            // The defect is the third: success having *committed*. That turns
            // a dropped connection into durable data nobody asked to keep,
            // and it is invisible until someone reads the table.
            rc = SQLDisconnect(sibling->get_handle());
            const std::string state =
                first_sqlstate(SQL_HANDLE_DBC, sibling->get_handle(),
                               "no SQLSTATE");

            const bool refused = !SQL_SUCCEEDED(rc);
            if (refused && state != "25000") {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLDisconnect failed with SQLSTATE " + state +
                           " rather than 25000";
                r.suggestion = "A driver that refuses to disconnect with an "
                               "open transaction must say 25000 (Invalid "
                               "transaction state), so the application can "
                               "tell this apart from a connection failure";
                return;
            }

            if (refused) {
                // The transaction is still open, exactly as 25000 promises.
                // Reading the row from another connection now would be asking
                // about isolation, not about SQLDisconnect — at READ
                // UNCOMMITTED it is visible and nothing is wrong. End the
                // transaction the documented way first, then ask whether the
                // work survived, which is the question this probe is for.
                SQLEndTran(SQL_HANDLE_DBC, sibling->get_handle(), SQL_ROLLBACK);
            }

            core::OdbcStatement check(conn_);
            check.execute("SELECT COUNT(*) FROM " + table.name() +
                          " WHERE ID = 4242");
            SQLINTEGER found = -1;
            SQLLEN ind = 0;
            if (!check.fetch() ||
                !SQL_SUCCEEDED(SQLGetData(check.get_handle(), 1, SQL_C_SLONG,
                                          &found, 0, &ind))) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not read the table back to see what "
                           "survived the disconnect";
                return;
            }

            if (found != 0) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Work that was never committed survived (found " +
                           std::to_string(found) + "; disconnect " +
                           (refused ? "refused with " + state
                                    : std::string("succeeded")) + ")";
                r.suggestion = "Uncommitted work must not become durable "
                               "because a connection closed. Roll it back on "
                               "disconnect, or refuse with 25000 and leave it "
                               "uncommitted — but do not commit it.";
                return;
            }

            r.status = TestStatus::PASS;
            r.actual = refused
                ? "Refused with 25000 and left the transaction open and "
                  "uncommitted, as SQLDisconnect documents"
                : "Disconnected and rolled the open transaction back";
        });
}

} // namespace odbc_crusher::tests
