// Transaction isolation — IMPROVEMENT_PLAN.md I6.
//
// Until I6 the mock had no uncommitted state at all. Every write landed in the
// process-global row store immediately, and ROLLBACK "undid" it by calling
// clear_inserted_data(), which deletes every row of every table for every
// connection.
//
// RollbackDoesNotTouchAnotherConnectionsRows is the bug, written first and
// watched to fail: connection B commits a row, connection A rolls back
// something unrelated, and B's row is gone.
//
// Two connections in one environment work in-process here for the same reason
// test_handle_lifetime.cpp's CatalogSharing tests do: this binary links
// mock_core directly, so there is no driver manager in the way.
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <string>

namespace {

class TxnIsolationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv),
                  SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        a_ = Connect();
        b_ = Connect();
        // Both connections must be open before the table is made, or the last
        // one out resets the catalog (D47) between them.
        Exec(a_, "CREATE TABLE I6_T (ID INTEGER)");
    }

    void TearDown() override {
        for (SQLHDBC* c : {&a_, &b_}) {
            if (*c == SQL_NULL_HDBC) continue;
            SQLSetConnectAttr(*c, SQL_ATTR_AUTOCOMMIT,
                              (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            SQLEndTran(SQL_HANDLE_DBC, *c, SQL_ROLLBACK);
            SQLDisconnect(*c);
            SQLFreeHandle(SQL_HANDLE_DBC, *c);
            *c = SQL_NULL_HDBC;
        }
        if (henv) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    SQLHDBC Connect(const std::string& extra = "") {
        SQLHDBC dbc = SQL_NULL_HDBC;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &dbc), SQL_SUCCESS);
        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;"
                           "Catalog=Default;" + extra;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            dbc, nullptr, (SQLCHAR*)conn.c_str(), SQL_NTS,
            nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT)));
        return dbc;
    }

    // Returns the return code so a caller can assert on it; most callers only
    // need it to have worked.
    SQLRETURN Exec(SQLHDBC dbc, const std::string& sql) {
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), SQL_SUCCESS);
        const SQLRETURN rc =
            SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return rc;
    }

    void ManualCommit(SQLHDBC dbc) {
        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetConnectAttr(
            dbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0)));
    }

    // How many rows this connection can see in I6_T.
    long Count(SQLHDBC dbc) {
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), SQL_SUCCESS);
        long n = -1;
        if (SQL_SUCCEEDED(SQLExecDirect(
                stmt, (SQLCHAR*)"SELECT COUNT(*) FROM I6_T", SQL_NTS))
            && SQL_SUCCEEDED(SQLFetch(stmt))) {
            SQLINTEGER v = 0;
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(stmt, 1, SQL_C_SLONG, &v,
                                         sizeof(v), &ind))) {
                n = static_cast<long>(v);
            }
        }
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return n;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC a_ = SQL_NULL_HDBC;
    SQLHDBC b_ = SQL_NULL_HDBC;
};

}  // namespace

// The bug. Written first and verified to fail before I6: ROLLBACK called
// clear_inserted_data(), which empties the row store for every table and every
// connection - so one connection rolling back destroyed data another had
// already committed.
TEST_F(TxnIsolationTest, RollbackDoesNotTouchAnotherConnectionsRows) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(b_, "INSERT INTO I6_T VALUES (1)")));
    ASSERT_EQ(Count(b_), 1) << "B's own committed row";

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (2)")));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_ROLLBACK)));

    EXPECT_EQ(Count(b_), 1)
        << "A's rollback destroyed a row B had committed. That is what "
           "clear_inserted_data() did: it emptied every table for every "
           "connection.";
}

TEST_F(TxnIsolationTest, UncommittedRowsAreInvisibleToAnotherConnection) {
    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (10)")));

    EXPECT_EQ(Count(a_), 1) << "a connection always sees its own writes";
    EXPECT_EQ(Count(b_), 0)
        << "an uncommitted row was visible to another connection at READ "
           "COMMITTED - which is the dirty read I6 exists to make detectable";

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));
    EXPECT_EQ(Count(b_), 1) << "and after the commit it must be there";
}

// Pins the op-log design against a snapshot-and-replace one: two connections
// writing to the same table must both land, not the later overwriting the
// earlier.
TEST_F(TxnIsolationTest, CommitMergesRatherThanReplaces) {
    ManualCommit(a_);
    ManualCommit(b_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (1)")));
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(b_, "INSERT INTO I6_T VALUES (2)")));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, b_, SQL_COMMIT)));

    EXPECT_EQ(Count(a_), 2)
        << "one commit replaced the other's work instead of merging with it";
}

// The tombstone path: a DELETE of a *committed* row inside a transaction is
// buffered like an INSERT, and rolling back brings the row back.
TEST_F(TxnIsolationTest, DeleteInsideATransactionIsUndoneByRollback) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (5)")));
    ASSERT_EQ(Count(a_), 1);

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "DELETE FROM I6_T WHERE ID = 5")));
    EXPECT_EQ(Count(a_), 0) << "the deleting transaction must not see the row";
    EXPECT_EQ(Count(b_), 1) << "but nobody else has lost it yet";

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_ROLLBACK)));
    EXPECT_EQ(Count(a_), 1) << "rolling back a DELETE must bring the row back";
}

TEST_F(TxnIsolationTest, CommittedDeleteIsVisibleToEveryone) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (7)")));
    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "DELETE FROM I6_T WHERE ID = 7")));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));

    EXPECT_EQ(Count(b_), 0) << "a committed DELETE has to reach other connections";
}

// SQLDisconnect answers 25000 with an open transaction and leaves the
// connection alone (D28). The write set has to survive that refusal, or the
// application cannot then roll back what it was told it still had.
TEST_F(TxnIsolationTest, RefusedDisconnectKeepsTheWriteSet) {
    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (99)")));

    EXPECT_EQ(SQLDisconnect(a_), SQL_ERROR)
        << "disconnecting with an open transaction must answer 25000";
    EXPECT_EQ(Count(a_), 1)
        << "the refused disconnect threw away the transaction's work";

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_ROLLBACK)));
    EXPECT_EQ(Count(a_), 0);
}

// Autocommit ON is the path every other suite in this binary runs on, and it
// must be exactly what it always was: writes land immediately, and a rollback
// has nothing to undo. Before I6 that needed a special case (D28's
// `had_transaction` guard); now it falls out of the buffer being empty.
TEST_F(TxnIsolationTest, RollbackInAutocommitModeChangesNothing) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (3)")));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_ROLLBACK)));
    EXPECT_EQ(Count(a_), 1)
        << "a rollback in autocommit mode deleted a committed row";
}
