// Block fetch — IMPROVEMENT_PLAN.md D11
//
// SQL_ATTR_ROW_ARRAY_SIZE was stored and ignored; SQL_ATTR_ROWS_FETCHED_PTR,
// SQL_ATTR_ROW_STATUS_PTR, SQL_ATTR_ROW_BIND_TYPE and
// SQL_ATTR_ROW_BIND_OFFSET_PTR were not even stored. SQLFetch advanced one
// row and wrote element 0 whatever the array size said — so an application
// that bound arrays of ten and asked for ten rows got one row, and because
// the driver answered SQL_SUCCESS and never wrote the fetched-rows count, it
// had no way to find out.
//
// This is also I5's precondition: eight array-parameter probes SKIP because
// the fixture could not do block fetch.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class BlockFetchTest : public ::testing::Test {
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
        Exec("CREATE TABLE BF (ID INTEGER, V VARCHAR(16))");
        Exec("INSERT INTO BF (ID, V) VALUES (1,'a'),(2,'b'),(3,'c'),(4,'d'),(5,'e')");
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

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── the attributes round-trip ────────────────────────────────────────────

TEST_F(BlockFetchTest, TheBlockFetchAttributesRoundTrip) {
    SQLULEN fetched = 0;
    SQLUSMALLINT status[4] = {0};
    SQLULEN offset = 0;

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)4, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_STATUS_PTR, status, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_BIND_OFFSET_PTR, &offset, 0)));

    SQLULEN size = 0;
    SQLULEN* fetched_back = nullptr;
    SQLUSMALLINT* status_back = nullptr;
    SQLULEN* offset_back = nullptr;
    SQLINTEGER len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, &size, sizeof(size), &len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched_back, 0, &len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_ROW_STATUS_PTR, &status_back, 0, &len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_ROW_BIND_OFFSET_PTR, &offset_back, 0, &len)));

    EXPECT_EQ(size, 4u);
    EXPECT_EQ(fetched_back, &fetched) << "SQL_ATTR_ROWS_FETCHED_PTR was discarded";
    EXPECT_EQ(status_back, status) << "SQL_ATTR_ROW_STATUS_PTR was discarded";
    EXPECT_EQ(offset_back, &offset)
        << "SQL_ATTR_ROW_BIND_OFFSET_PTR was discarded";
}

// ── one fetch fills the whole row set ────────────────────────────────────

TEST_F(BlockFetchTest, OneFetchFillsTheBoundArrays) {
    SQLINTEGER ids[3] = {-1, -1, -1};
    SQLLEN id_ind[3] = {0, 0, 0};
    SQLULEN fetched = 0;
    SQLUSMALLINT status[3] = {0xFFFF, 0xFFFF, 0xFFFF};

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)3, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_STATUS_PTR, status, 0)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM BF ORDER BY ID", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(
        hstmt, 1, SQL_C_SLONG, ids, sizeof(SQLINTEGER), id_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));

    EXPECT_EQ(fetched, 3u) << "the fetched-rows count was not written";
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2) << "only element 0 was filled - this is a row set";
    EXPECT_EQ(ids[2], 3);
    EXPECT_EQ(status[0], SQL_ROW_SUCCESS);
    EXPECT_EQ(status[2], SQL_ROW_SUCCESS);
    SQLCloseCursor(hstmt);
}

// The next fetch continues after the set it delivered, not one row on.
TEST_F(BlockFetchTest, TheNextFetchContinuesAfterTheRowSet) {
    SQLINTEGER ids[2] = {-1, -1};
    SQLLEN id_ind[2] = {0, 0};
    SQLULEN fetched = 0;

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM BF ORDER BY ID", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(
        hstmt, 1, SQL_C_SLONG, ids, sizeof(SQLINTEGER), id_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2);

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    EXPECT_EQ(ids[0], 3) << "the second fetch did not continue after the set";
    EXPECT_EQ(ids[1], 4);
    SQLCloseCursor(hstmt);
}

// A partial last set reports how many it really filled, and marks the rest.
TEST_F(BlockFetchTest, APartialLastRowSetReportsWhatItFilled) {
    SQLINTEGER ids[4] = {-1, -1, -1, -1};
    SQLLEN id_ind[4] = {0, 0, 0, 0};
    SQLULEN fetched = 0;
    SQLUSMALLINT status[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)4, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_STATUS_PTR, status, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM BF ORDER BY ID", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(
        hstmt, 1, SQL_C_SLONG, ids, sizeof(SQLINTEGER), id_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));   // rows 1-4
    EXPECT_EQ(fetched, 4u);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));   // row 5 only
    EXPECT_EQ(fetched, 1u) << "the partial set reported the wrong count";
    EXPECT_EQ(ids[0], 5);
    EXPECT_EQ(status[0], SQL_ROW_SUCCESS);
    EXPECT_EQ(status[1], SQL_ROW_NOROW) << "the unfilled rows were not marked";
    SQLCloseCursor(hstmt);
}

// Running off the end reports zero rows, not a stale count.
TEST_F(BlockFetchTest, EndOfResultSetReportsZeroFetched) {
    SQLINTEGER ids[2] = {-1, -1};
    SQLLEN id_ind[2] = {0, 0};
    SQLULEN fetched = 99;

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)2, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM BF WHERE ID = 1", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(
        hstmt, 1, SQL_C_SLONG, ids, sizeof(SQLINTEGER), id_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    EXPECT_EQ(fetched, 1u);
    EXPECT_EQ(SQLFetch(hstmt), SQL_NO_DATA);
    EXPECT_EQ(fetched, 0u) << "the count was left stale past the end";
    SQLCloseCursor(hstmt);
}

// The character path strides too, not just the numeric one.
TEST_F(BlockFetchTest, CharacterColumnsFillTheirArrayElements) {
    char names[3][16] = {{0}, {0}, {0}};
    SQLLEN name_ind[3] = {0, 0, 0};
    SQLULEN fetched = 0;

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROW_ARRAY_SIZE, (SQLPOINTER)3, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_ROWS_FETCHED_PTR, &fetched, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM BF ORDER BY ID", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(
        hstmt, 1, SQL_C_CHAR, names, 16, name_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    EXPECT_EQ(fetched, 3u);
    EXPECT_STREQ(names[0], "a");
    EXPECT_STREQ(names[1], "b") << "the character path did not stride";
    EXPECT_STREQ(names[2], "c");
    SQLCloseCursor(hstmt);
}

// A single-row fetch must behave exactly as before - the guard against
// breaking every existing caller.
TEST_F(BlockFetchTest, ASingleRowFetchIsUnchanged) {
    SQLINTEGER id = -1;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM BF ORDER BY ID", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindCol(hstmt, 1, SQL_C_SLONG, &id,
                                         sizeof(id), &ind)));
    int rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++rows;
    SQLCloseCursor(hstmt);
    EXPECT_EQ(rows, 5);
    EXPECT_EQ(id, 5);
}

}  // namespace
