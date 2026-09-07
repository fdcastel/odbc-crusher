// Constants in the select list — IMPROVEMENT_PLAN.md D41.
//
// `SELECT 1 FROM T` used to come back as 42S22 "Column not found: 1", because
// every select-list item was looked up as a column name. Two things depended
// on it and could not be exercised against this driver as a result:
//
//   * `SELECT 1 FROM <table> WHERE 1=0` — the existence probe both
//     create_test_table() helpers run before reusing a table left behind by
//     an earlier run, which made A15's reuse path unreachable here;
//   * every dialect variant A2 builds — `SELECT <expr> FROM RDB$DATABASE`
//     for Firebird and `SELECT <expr> FROM DUAL` for Oracle — all of which
//     are constants selected from a table.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <string>

class LiteralSelectTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);

        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn.c_str(),
            SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);

        Exec("CREATE TABLE T (ID INTEGER, V VARCHAR(64))");
        Exec("INSERT INTO T (ID, V) VALUES (1, 'a')");
        Exec("INSERT INTO T (ID, V) VALUES (2, 'b')");
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

    // Fetch column `col` of every row as text.
    std::vector<std::string> SelectText(const std::string& sql,
                                        SQLUSMALLINT col = 1) {
        std::vector<std::string> out;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS))) << sql;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            char buf[128] = {0};
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(hstmt, col, SQL_C_CHAR, buf,
                                         sizeof(buf), &ind))) {
                out.push_back(buf);
            }
        }
        SQLCloseCursor(hstmt);
        return out;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// The exact statement create_test_table() uses to decide whether a table from
// an earlier run is reusable. It must succeed and return no rows.
TEST_F(LiteralSelectTest, ExistenceProbeSucceedsAndReturnsNoRows) {
    SQLRETURN ret = SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT 1 FROM T WHERE 1=0", SQL_NTS);
    EXPECT_TRUE(SQL_SUCCEEDED(ret))
        << "SELECT 1 FROM T WHERE 1=0 must not be a 42S22";
    EXPECT_EQ(SQLFetch(hstmt), SQL_NO_DATA) << "WHERE 1=0 must match nothing";
    SQLCloseCursor(hstmt);
}

// The same statement against a table that does not exist must still fail —
// the whole point of the probe is to distinguish the two.
TEST_F(LiteralSelectTest, ExistenceProbeStillFailsForAMissingTable) {
    SQLRETURN ret = SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT 1 FROM NO_SUCH_TABLE WHERE 1=0", SQL_NTS);
    EXPECT_FALSE(SQL_SUCCEEDED(ret));
    SQLCloseCursor(hstmt);
}

TEST_F(LiteralSelectTest, IntegerConstantIsReturnedForEveryRow) {
    EXPECT_EQ(SelectText("SELECT 1 FROM T"),
              (std::vector<std::string>{"1", "1"}));
}

TEST_F(LiteralSelectTest, StringConstantIsReturnedForEveryRow) {
    EXPECT_EQ(SelectText("SELECT 'x' FROM T"),
              (std::vector<std::string>{"x", "x"}));
}

// A constant alongside real columns must keep its position. The old
// projection built a list of column indices and dropped anything that was not
// a column, which shifted every later column one place left.
TEST_F(LiteralSelectTest, ConstantKeepsItsPositionAmongRealColumns) {
    EXPECT_EQ(SelectText("SELECT ID, 7, V FROM T", 1),
              (std::vector<std::string>{"1", "2"}));
    EXPECT_EQ(SelectText("SELECT ID, 7, V FROM T", 2),
              (std::vector<std::string>{"7", "7"}));
    EXPECT_EQ(SelectText("SELECT ID, 7, V FROM T", 3),
              (std::vector<std::string>{"a", "b"}));
}

// A genuine typo must still be a 42S22 — the literal handling must not turn
// every unknown name into a silently-succeeding constant.
TEST_F(LiteralSelectTest, UnknownColumnIsStillAnError) {
    SQLRETURN ret = SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT NOSUCHCOLUMN FROM T", SQL_NTS);
    EXPECT_FALSE(SQL_SUCCEEDED(ret));

    SQLCHAR state[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT len = 0;
    SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state, &native, msg,
                  sizeof(msg), &len);
    EXPECT_STREQ((const char*)state, "42S22");
    SQLCloseCursor(hstmt);
}

// The other side of the constant predicate: `WHERE 1=1` must not filter
// anything out. A rule that emptied the result whenever the left side was not
// a column would pass the 1=0 test and be wrong.
TEST_F(LiteralSelectTest, TrueConstantPredicateKeepsEveryRow) {
    EXPECT_EQ(SelectText("SELECT ID FROM T WHERE 1=1"),
              (std::vector<std::string>{"1", "2"}));
}

// And a column predicate must still work — the constant handling sits in
// front of it.
TEST_F(LiteralSelectTest, ColumnPredicateStillFilters) {
    EXPECT_EQ(SelectText("SELECT V FROM T WHERE ID = 2"),
              (std::vector<std::string>{"b"}));
}

// ── SQLDescribeParam describes the column, not a default — D45 ────────────
//
// It used to answer VARCHAR(255) for every parameter of every statement, so
// the three SQLDescribeParam probes could not tell a correct driver from one
// with no idea what its own parameters are. That is why they printed
// `expected` beside `actual` and never compared them (B1) — comparing would
// have failed the reference driver.
TEST_F(LiteralSelectTest, DescribeParamReportsTheTargetColumnsType) {
    Exec("CREATE TABLE P (N INTEGER, S VARCHAR(64))");

    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"INSERT INTO P (N, S) VALUES (?, ?)", SQL_NTS)));

    SQLSMALLINT type = 0, scale = 0, nullable = 0;
    SQLULEN size = 0;

    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLDescribeParam(hstmt, 1, &type, &size, &scale, &nullable)));
    EXPECT_EQ(type, SQL_INTEGER) << "parameter 1 is bound to an INTEGER column";

    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLDescribeParam(hstmt, 2, &type, &size, &scale, &nullable)));
    EXPECT_EQ(type, SQL_VARCHAR) << "parameter 2 is bound to a VARCHAR column";
    EXPECT_EQ(size, 64u) << "column_size must be the column's length, not 255";

    SQLCloseCursor(hstmt);
    Exec("DROP TABLE P");
}

// A statement the narrow parse does not recognise keeps the old
// VARCHAR(255) answer, which is a legal thing for a driver to say when it
// cannot infer more. This pins that the fallback still exists rather than
// the lookup failing outright.
TEST_F(LiteralSelectTest, DescribeParamFallsBackForAnUnparsedStatement) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"SELECT ID FROM T WHERE ID = ?", SQL_NTS)));

    SQLSMALLINT type = 0, scale = 0, nullable = 0;
    SQLULEN size = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLDescribeParam(hstmt, 1, &type, &size, &scale, &nullable)));
    EXPECT_EQ(type, SQL_VARCHAR);
    EXPECT_EQ(size, 255u);
    SQLCloseCursor(hstmt);
}
