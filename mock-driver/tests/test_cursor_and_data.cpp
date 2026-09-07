// Chunked retrieval, scrolling, and data-at-execution — IMPROVEMENT_PLAN.md D17
//
// The last surfaces D17 lists with zero references in the mock's tests:
// SQLFetchScroll, SQLParamData, SQLPutData, SQLCancel, and an end-to-end
// chunked SQLGetData. D37 fixed the continuation — the mock used to restart
// from byte 0 on every call, so an application looping on 01004, which is what
// the spec tells it to do, never terminated — and that fix has had only a
// unit test of the copy helper underneath it, never a loop through the driver.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class CursorDataTest : public ::testing::Test {
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
        Exec("CREATE TABLE CD (ID INTEGER, V VARCHAR(256))");
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
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

    std::string StmtState() {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR msg[256] = {0};
        SQLSMALLINT len = 0;
        if (SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state, &native,
                          msg, sizeof(msg), &len) == SQL_NO_DATA) {
            return "";
        }
        return std::string(reinterpret_cast<char*>(state));
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── chunked SQLGetData: the loop the spec prescribes must terminate ──────

TEST_F(CursorDataTest, ALongValueIsRetrievedInChunks) {
    const std::string value(200, 'a');
    Exec("INSERT INTO CD (ID, V) VALUES (1, '" + value + "')");

    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM CD WHERE ID = 1", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));

    // The loop an application is told to write: call until it stops saying
    // "truncated". Sixteen bytes at a time, so 200 characters take many turns.
    std::string assembled;
    int rounds = 0;
    SQLRETURN rc = SQL_SUCCESS_WITH_INFO;
    while (rc == SQL_SUCCESS_WITH_INFO) {
        char buf[16] = {0};
        SQLLEN ind = 0;
        rc = SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        ASSERT_TRUE(SQL_SUCCEEDED(rc)) << "round " << rounds;
        assembled += buf;
        ASSERT_LT(++rounds, 100) << "the chunked read did not terminate";
    }
    SQLCloseCursor(hstmt);

    EXPECT_EQ(assembled, value)
        << "the chunks did not reassemble into the stored value";
    EXPECT_GT(rounds, 1) << "the value came back in one piece, so nothing was chunked";
}

// The last chunk is a plain SQL_SUCCESS, and the one before it 01004.
TEST_F(CursorDataTest, TruncationIsSignalledWith01004) {
    Exec("INSERT INTO CD (ID, V) VALUES (2, 'abcdefghij')");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM CD WHERE ID = 2", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));

    char buf[5] = {0};
    SQLLEN ind = 0;
    const SQLRETURN rc = SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
    EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO);
    EXPECT_EQ(StmtState(), "01004");
    // The indicator reports what was *available*, not what was written.
    EXPECT_EQ(ind, 10);
    SQLCloseCursor(hstmt);
}

// ── SQLFetchScroll ───────────────────────────────────────────────────────

TEST_F(CursorDataTest, FetchScrollNextWalksTheResultSet) {
    Exec("INSERT INTO CD (ID, V) VALUES (1, 'a'), (2, 'b'), (3, 'c')");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM CD ORDER BY ID", SQL_NTS)));

    int rows = 0;
    while (SQL_SUCCEEDED(SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0))) ++rows;
    SQLCloseCursor(hstmt);
    EXPECT_EQ(rows, 3) << "SQL_FETCH_NEXT did not walk the whole result set";
}

TEST_F(CursorDataTest, FetchScrollOnAClosedCursorIsAnError) {
    SQLHSTMT fresh = SQL_NULL_HSTMT;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &fresh), SQL_SUCCESS);
    EXPECT_FALSE(SQL_SUCCEEDED(SQLFetchScroll(fresh, SQL_FETCH_NEXT, 0)));
    SQLFreeHandle(SQL_HANDLE_STMT, fresh);
}

// ── data-at-execution and SQLCancel ──────────────────────────────────────
//
// These are the ones with no reference anywhere in the suite. The assertion
// is that each answers coherently rather than crashing or silently
// succeeding: a driver that has not implemented a call should say so.

TEST_F(CursorDataTest, ParamDataWithoutADataAtExecParameterIsAnError) {
    SQLPOINTER token = nullptr;
    const SQLRETURN rc = SQLParamData(hstmt, &token);
    EXPECT_FALSE(SQL_SUCCEEDED(rc))
        << "SQLParamData reported success with no data-at-execution parameter";
}

TEST_F(CursorDataTest, PutDataOutsideADataAtExecSequenceIsAnError) {
    char chunk[] = "abc";
    const SQLRETURN rc = SQLPutData(hstmt, chunk, SQL_NTS);
    EXPECT_FALSE(SQL_SUCCEEDED(rc))
        << "SQLPutData reported success outside a data-at-execution sequence";
}

TEST_F(CursorDataTest, CancelOnAnIdleStatementSucceeds) {
    // Per the spec, SQLCancel on a statement with nothing running is a no-op
    // that succeeds - the one answer that is definitely not an error.
    EXPECT_TRUE(SQL_SUCCEEDED(SQLCancel(hstmt)));
}

}  // namespace
