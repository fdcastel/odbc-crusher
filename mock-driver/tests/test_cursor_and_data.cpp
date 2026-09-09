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
#include <utility>

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

// ── the scroll path uses the same conversion table as SQLFetch ───────────
//
// D18: SQLFetch and SQLFetchScroll each had their own copy of the loop that
// delivers a row to the bound columns, and they drifted. SQLFetch's went
// through write_numeric_as when D11 rebuilt the conversion table; the
// SQLFetchScroll copy kept the original switch, which knew four C types and
// laid the *decimal spelling* of a number over the caller's buffer for
// anything else. An application that scrolled was talking to the pre-D11
// driver, and no test could tell.

TEST_F(CursorDataTest, FetchScrollBindsTheSameCTypesAsFetch) {
    Exec("INSERT INTO CD (ID, V) VALUES (7, 'x')");

    // SQL_C_UTINYINT is one of the types the stale switch did not know: it
    // fell through to the SQL_C_CHAR branch and wrote '7' - 0x37 - instead
    // of 7.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM CD WHERE ID = 7", SQL_NTS)));
    SQLCHAR tiny = 0;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(hstmt, 1, SQL_C_UTINYINT, &tiny,
                                         sizeof(tiny), &ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0)));
    SQLCloseCursor(hstmt);

    EXPECT_EQ(tiny, 7)
        << "the scroll path wrote the decimal spelling of the number, not the "
           "number - it is not using D11's conversion table";
    EXPECT_EQ(ind, static_cast<SQLLEN>(sizeof(SQLCHAR)));
}

// And the two paths agree, which is the property the shared loop exists for.
TEST_F(CursorDataTest, FetchAndFetchScrollDeliverTheSameBytes) {
    Exec("INSERT INTO CD (ID, V) VALUES (9, 'y')");

    auto read_with = [&](bool scroll) {
        SQLINTEGER v = -1;
        SQLLEN ind = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)"SELECT ID FROM CD WHERE ID = 9", SQL_NTS)));
        EXPECT_TRUE(SQL_SUCCEEDED(SQLBindCol(hstmt, 1, SQL_C_SLONG, &v,
                                             sizeof(v), &ind)));
        if (scroll) {
            EXPECT_TRUE(SQL_SUCCEEDED(SQLFetchScroll(hstmt, SQL_FETCH_NEXT, 0)));
        } else {
            EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
        }
        SQLCloseCursor(hstmt);
        return v;
    };
    EXPECT_EQ(read_with(false), 9);
    EXPECT_EQ(read_with(true), 9);
}

// ── D85: the continuation belongs to a result set, not to a pair of ───────
//        coordinates
//
// D37 keyed the offset on (column, row) and the comment on the state claimed
// that covered re-execute, SQLFreeStmt(SQL_CLOSE) and the catalog functions
// too, "without having to remember to reset anything". It does not. A new
// result set puts column 1 of row 0 exactly where the old one had column 1 of
// row 0, so the key is unchanged and a finished offset is carried into a
// different value - which the mock then reports as SQL_NO_DATA, leaving the
// caller's buffer untouched.
//
// Found by D83's first read-back test, which read the same column of the same
// row twice in two result sets - the shape of every "SELECT one column WHERE
// key = n" an application runs in a loop.

TEST_F(CursorDataTest, TheSameCellReadTwiceInTwoResultSetsReadsTheSameValue) {
    Exec("INSERT INTO CD (ID, V) VALUES (1, 'abc')");

    auto read_v = [&]() {
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)"SELECT V FROM CD WHERE ID = 1", SQL_NTS)));
        EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
        char buf[32] = {0};
        SQLLEN ind = 0;
        const SQLRETURN rc = SQLGetData(hstmt, 1, SQL_C_CHAR, buf,
                                        sizeof(buf), &ind);
        SQLCloseCursor(hstmt);
        return std::make_pair(rc, std::string(buf));
    };

    const auto first = read_v();
    EXPECT_EQ(first.first, SQL_SUCCESS);
    EXPECT_EQ(first.second, "abc");

    const auto second = read_v();
    EXPECT_NE(second.first, SQL_NO_DATA)
        << "the first read's exhausted offset ended the second one";
    EXPECT_EQ(second.second, "abc");
}

// The partial case, where the stale offset is not merely exhausted but points
// into the middle of a value that is no longer there.
TEST_F(CursorDataTest, AnAbandonedChunkedReadDoesNotResumeInTheNextResultSet) {
    Exec("INSERT INTO CD (ID, V) VALUES (1, 'abcdefghij')");

    // Take the first four characters and walk away without finishing.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM CD WHERE ID = 1", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    char partial[5] = {0};
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLGetData(hstmt, 1, SQL_C_CHAR, partial, sizeof(partial), &ind)));
    EXPECT_STREQ(partial, "abcd");
    SQLCloseCursor(hstmt);

    // A fresh result set starts at the beginning of the value, not at byte 4.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM CD WHERE ID = 1", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    char whole[32] = {0};
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLGetData(hstmt, 1, SQL_C_CHAR, whole, sizeof(whole), &ind)));
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(whole, "abcdefghij")
        << "the read resumed where the abandoned one stopped";
}

// A catalog result set is installed by a different function - not by the
// execute path - and had the same gap. Running SQLTables twice is an ordinary
// thing for an application to do.
//
// It reads the same cell twice and compares, rather than asserting anything
// about what the catalog holds. That matters: the first draft asserted only
// `rc != SQL_NO_DATA`, and **passed against the unfixed driver**, because an
// offset that lands inside a longer value is not exhausted - it is a valid
// position, so the call succeeds and returns the value with its leading
// characters gone. Silent truncation is the worse of the two failures and the
// weaker assertion could not see it.
TEST_F(CursorDataTest, ACatalogResultSetDoesNotInheritTheLastOnesOffset) {
    auto table_name = [&]() {
        EXPECT_TRUE(SQL_SUCCEEDED(SQLTables(hstmt, NULL, 0, NULL, 0, NULL, 0,
                                            NULL, 0)));
        EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
        char cell[128] = {0};
        SQLLEN ind = 0;
        const SQLRETURN rc = SQLGetData(hstmt, 3, SQL_C_CHAR, cell,
                                        sizeof(cell), &ind);
        EXPECT_NE(rc, SQL_NO_DATA)
            << "the previous result set's offset ended this read at once";
        SQLCloseCursor(hstmt);
        return std::string(cell);
    };

    const std::string first = table_name();
    ASSERT_FALSE(first.empty()) << "the fixture needs a non-empty TABLE_NAME";
    EXPECT_EQ(table_name(), first)
        << "the second read came back short by the first read's offset";
}

}  // namespace
