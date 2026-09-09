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

    // D83: how many rows this connection can see matching a predicate.
    // Count() answers for the whole table; an UPDATE has to be watched moving
    // rows between predicates, not appearing and disappearing.
    long CountWhere(SQLHDBC dbc, const std::string& where) {
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), SQL_SUCCESS);
        const std::string sql =
            "SELECT COUNT(*) FROM I6_T WHERE " + where;
        long n = -1;
        if (SQL_SUCCEEDED(SQLExecDirect(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS))
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


// ── I6 Stage B: isolation levels, and the configuration that lies ─────────
//
// Stage A gave every connection a buffer of uncommitted writes. What makes it
// useful to a conformance tool is being able to configure a driver that gets
// isolation *wrong* - because an honest READ UNCOMMITTED cannot fail a probe. A
// driver showing a dirty read at READ UNCOMMITTED is behaving correctly.

TEST_F(TxnIsolationTest, ReadUncommittedSeesAnotherConnectionsPendingRow) {
    SQLHDBC reader = Connect("IsolationLevel=ReadUncommitted;");

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (42)")));

    EXPECT_EQ(Count(reader), 1)
        << "a READ UNCOMMITTED connection must see another's pending row - "
           "without that, no isolation probe has a configuration that can fail";
    EXPECT_EQ(Count(b_), 0)
        << "and a READ COMMITTED one must not";

    SQLDisconnect(reader);
    SQLFreeHandle(SQL_HANDLE_DBC, reader);
}

// IsolationLevel= was not parsed at all before I6: DriverConfig::isolation_level
// existed, was assigned to the connection, and no key ever set it - the same
// silent no-op D26 fixed for FailOn. Asserted through SQLGetInfo, which is where
// an application would look.
TEST_F(TxnIsolationTest, IsolationLevelIsParsedAndReported) {
    struct Case { const char* value; SQLUINTEGER expected; };
    const Case cases[] = {
        {"ReadUncommitted", SQL_TXN_READ_UNCOMMITTED},
        {"ReadCommitted",   SQL_TXN_READ_COMMITTED},
        {"RepeatableRead",  SQL_TXN_REPEATABLE_READ},
        {"Serializable",    SQL_TXN_SERIALIZABLE},
    };
    for (const auto& c : cases) {
        SQLHDBC dbc = Connect(std::string("IsolationLevel=") + c.value + ";");
        SQLUINTEGER got = 0;
        SQLSMALLINT len = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLGetInfo(dbc, SQL_DEFAULT_TXN_ISOLATION,
                                             &got, sizeof(got), &len)));
        EXPECT_EQ(got, c.expected)
            << "IsolationLevel=" << c.value << " was not honoured";
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
    }
}

// The lever a probe needs. DirtyReads makes the driver behave as READ
// UNCOMMITTED while continuing to report READ COMMITTED - a real driver bug,
// and the only shape a "does this driver honour its isolation level?" probe can
// be failed by.
TEST_F(TxnIsolationTest, DirtyReadsLiesAboutTheIsolationLevel) {
    SQLHDBC liar = Connect("DirtyReads=true;");

    SQLUINTEGER reported = 0;
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfo(liar, SQL_DEFAULT_TXN_ISOLATION,
                                         &reported, sizeof(reported), &len)));
    EXPECT_EQ(reported, static_cast<SQLUINTEGER>(SQL_TXN_READ_COMMITTED))
        << "DirtyReads must not change what the driver *reports* - reporting "
           "the truth would remove the lie the knob exists to model";

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (77)")));
    EXPECT_EQ(Count(liar), 1)
        << "DirtyReads=true must show another connection's uncommitted row "
           "while claiming READ COMMITTED";

    SQLDisconnect(liar);
    SQLFreeHandle(SQL_HANDLE_DBC, liar);
}

// A disconnected connection's buffer must leave the registry, or a peer at READ
// UNCOMMITTED keeps reading writes belonging to nobody.
TEST_F(TxnIsolationTest, DisconnectingRemovesTheBufferFromThePeerView) {
    SQLHDBC writer = Connect();
    SQLHDBC reader = Connect("IsolationLevel=ReadUncommitted;");

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetConnectAttr(
        writer, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(writer, "INSERT INTO I6_T VALUES (5)")));
    ASSERT_EQ(Count(reader), 1);

    // Roll back first: SQLDisconnect answers 25000 with a transaction open.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, writer, SQL_ROLLBACK)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDisconnect(writer)));
    SQLFreeHandle(SQL_HANDLE_DBC, writer);

    EXPECT_EQ(Count(reader), 0)
        << "the disconnected connection's buffer is still in the peer view";

    SQLDisconnect(reader);
    SQLFreeHandle(SQL_HANDLE_DBC, reader);
}

// ── D83: UPDATE is a transactional write like the other two ───────────────
//
// Stage A buffered INSERT and DELETE, and D83 recorded the SET clause as an
// op of the same log rather than writing through the catalog. The claim was
// that buffering, rollback and merge-at-commit then come for free. These
// check the claim.

TEST_F(TxnIsolationTest, UpdateInsideATransactionIsUndoneByRollback) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (1)")));

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "UPDATE I6_T SET ID = 99 WHERE ID = 1")));
    EXPECT_EQ(CountWhere(a_, "ID = 99"), 1) << "a connection sees its own write";

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_ROLLBACK)));
    EXPECT_EQ(CountWhere(a_, "ID = 1"), 1) << "the rollback did not undo the SET";
    EXPECT_EQ(CountWhere(a_, "ID = 99"), 0);
}

TEST_F(TxnIsolationTest, AnUncommittedUpdateIsInvisibleToAnotherConnection) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (1)")));

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "UPDATE I6_T SET ID = 99 WHERE ID = 1")));

    EXPECT_EQ(CountWhere(b_, "ID = 1"), 1)
        << "B saw a change A has not committed";
    EXPECT_EQ(CountWhere(b_, "ID = 99"), 0);

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));
    EXPECT_EQ(CountWhere(b_, "ID = 99"), 1) << "and after the commit it must be";
    EXPECT_EQ(CountWhere(b_, "ID = 1"), 0);
}

// The merge property, for UPDATE. A commit replays the predicate against
// whatever is committed *then*, so a row another connection committed after
// this transaction opened is updated too - which is what a real engine at
// READ COMMITTED does, and what a snapshot-and-replace design could not.
TEST_F(TxnIsolationTest, AnUpdateCommitsAgainstTheRowsCommittedByThen) {
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (1)")));

    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "UPDATE I6_T SET ID = 99 WHERE ID < 50")));

    // B commits a second matching row while A's update is still pending.
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(b_, "INSERT INTO I6_T VALUES (2)")));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));
    EXPECT_EQ(CountWhere(b_, "ID = 99"), 2)
        << "the commit replayed against a stale snapshot instead of the "
           "rows committed by commit time";
}

// An UPDATE of a row the same transaction inserted has to see it - the ops
// replay in issue order for exactly this.
TEST_F(TxnIsolationTest, AnUpdateSeesARowTheSameTransactionInserted) {
    ManualCommit(a_);
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "INSERT INTO I6_T VALUES (7)")));
    ASSERT_TRUE(SQL_SUCCEEDED(Exec(a_, "UPDATE I6_T SET ID = 8 WHERE ID = 7")));

    EXPECT_EQ(CountWhere(a_, "ID = 8"), 1);
    EXPECT_EQ(CountWhere(a_, "ID = 7"), 0);

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, a_, SQL_COMMIT)));
    EXPECT_EQ(CountWhere(b_, "ID = 8"), 1);
}
