// Transaction state — IMPROVEMENT_PLAN.md D28
//
// ConnectionHandle had no notion of being mid-transaction, which had four
// visible consequences:
//
//   * SQLDisconnect could never return 25000, so an application that forgot
//     to commit was told everything was fine;
//   * SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT) merely stored the value instead
//     of committing on OFF -> ON as the spec requires;
//   * SQLEndTran treated any fType that was not SQL_ROLLBACK as a commit, so
//     a typo in the caller's argument silently committed;
//   * SQLEndTran(SQL_ROLLBACK) called clear_inserted_data() even in
//     autocommit-ON mode, where a rollback should do nothing at all — the
//     mock whose purpose is validating "the rows persisted" assertions was
//     itself deleting the rows.
//
// Its should_fail check also ran before handle validation, so a garbage
// handle came back SQL_ERROR rather than SQL_INVALID_HANDLE.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class TransactionStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        const char* conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
        Exec("CREATE TABLE T28 (ID INTEGER)");
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            // Leave the connection in a state that can be disconnected even
            // if a test left a transaction open.
            SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
                              (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK);
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    void Exec(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLCloseCursor(hstmt);
    }

    std::string ConnState() {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR msg[256] = {0};
        SQLSMALLINT len = 0;
        if (SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state, &native,
                          msg, sizeof(msg), &len) == SQL_NO_DATA) {
            return "";
        }
        return std::string(reinterpret_cast<char*>(state));
    }

    int CountRows() {
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)"SELECT ID FROM T28", SQL_NTS)));
        int n = 0;
        while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++n;
        SQLCloseCursor(hstmt);
        return n;
    }

    void ManualCommit() {
        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetConnectAttr(
            hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0)));
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── a rollback in autocommit mode must do nothing ────────────────────────

TEST_F(TransactionStateTest, RollbackInAutocommitModeKeepsTheRows) {
    Exec("INSERT INTO T28 (ID) VALUES (1), (2)");
    ASSERT_EQ(CountRows(), 2);

    // Autocommit is on, so there is no transaction to roll back.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK)));
    EXPECT_EQ(CountRows(), 2)
        << "a rollback with no open transaction discarded committed rows";
}

// ── and a rollback of a real transaction must discard it ─────────────────

TEST_F(TransactionStateTest, RollbackOfAnOpenTransactionDiscardsIt) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (1), (2)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK)));
    EXPECT_EQ(CountRows(), 0);
}

// ── SQLDisconnect with a transaction open ────────────────────────────────

TEST_F(TransactionStateTest, DisconnectWithAnOpenTransactionIs25000) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (1)");

    EXPECT_FALSE(SQL_SUCCEEDED(SQLDisconnect(hdbc)))
        << "disconnecting mid-transaction was allowed";
    EXPECT_EQ(ConnState(), "25000");

    // Clean up so TearDown can disconnect.
    SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK);
}

TEST_F(TransactionStateTest, DisconnectAfterCommitSucceeds) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (1)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT)));
    EXPECT_TRUE(SQL_SUCCEEDED(SQLDisconnect(hdbc)));

    // TearDown would otherwise disconnect an already-disconnected handle.
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    hdbc = SQL_NULL_HDBC;
}

// ── autocommit OFF -> ON commits the open transaction ────────────────────

TEST_F(TransactionStateTest, TurningAutocommitBackOnCommits) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (1), (2)");

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetConnectAttr(
        hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0)));

    // The transaction is finished, so disconnecting is fine...
    EXPECT_TRUE(SQL_SUCCEEDED(SQLDisconnect(hdbc)))
        << "the transaction was left open after autocommit was restored";
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    hdbc = SQL_NULL_HDBC;
}

// ...and the rows it committed survive a later rollback.
TEST_F(TransactionStateTest, RowsCommittedByRestoringAutocommitSurvive) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (1), (2)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetConnectAttr(
        hdbc, SQL_ATTR_AUTOCOMMIT, (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_ROLLBACK)));
    EXPECT_EQ(CountRows(), 2)
        << "restoring autocommit did not commit, so the rollback took the rows";
}

// ── SQLEndTran argument and handle validation ────────────────────────────

TEST_F(TransactionStateTest, AnInvalidCompletionTypeIsHy012) {
    const SQLSMALLINT kNotACompletionType = 99;
    EXPECT_FALSE(SQL_SUCCEEDED(
        SQLEndTran(SQL_HANDLE_DBC, hdbc, kNotACompletionType)));
    EXPECT_EQ(ConnState(), "HY012")
        << "an unrecognised completion type was treated as a commit";
}

TEST_F(TransactionStateTest, AGarbageHandleIsInvalidHandleNotError) {
    // A statement handle is not a valid target for SQLEndTran.
    EXPECT_EQ(SQLEndTran(SQL_HANDLE_STMT, hstmt, SQL_COMMIT), SQL_INVALID_HANDLE);
}

// The ordering matters only when fault injection is armed: should_fail used
// to run *before* handle validation, so a bad handle came back SQL_ERROR -
// the one return code that says "the handle was fine, the call was not".
// BehaviorController is process-global, so this test restores Mode=Success
// before it returns.
TEST(TransactionOrdering, HandleValidationPrecedesFaultInjection) {
    SQLHENV env = SQL_NULL_HENV;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env), SQL_SUCCESS);
    ASSERT_EQ(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                            (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);

    auto connect = [&](const char* cs, SQLHDBC* out) {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, env, out), SQL_SUCCESS);
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            *out, NULL, (SQLCHAR*)cs, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
    };

    SQLHDBC dbc = SQL_NULL_HDBC;
    connect("Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLEndTran;"
            "ErrorCode=40001;Catalog=Default;", &dbc);
    SQLHSTMT stmt = SQL_NULL_HSTMT;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), SQL_SUCCESS);

    // Armed fault injection must not turn a bad handle into SQL_ERROR.
    EXPECT_EQ(SQLEndTran(SQL_HANDLE_STMT, stmt, SQL_COMMIT), SQL_INVALID_HANDLE);
    // ...and a good handle still gets the injected failure.
    EXPECT_EQ(SQLEndTran(SQL_HANDLE_DBC, dbc, SQL_COMMIT), SQL_ERROR);

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    SQLDisconnect(dbc);
    SQLFreeHandle(SQL_HANDLE_DBC, dbc);

    // Restore the process-global behaviour for whatever runs next.
    SQLHDBC restore = SQL_NULL_HDBC;
    connect("Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;", &restore);
    SQLDisconnect(restore);
    SQLFreeHandle(SQL_HANDLE_DBC, restore);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
}

TEST_F(TransactionStateTest, CommitStillSucceedsAndKeepsTheRows) {
    ManualCommit();
    Exec("INSERT INTO T28 (ID) VALUES (7)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLEndTran(SQL_HANDLE_DBC, hdbc, SQL_COMMIT)));
    EXPECT_EQ(CountRows(), 1);
}

}  // namespace
