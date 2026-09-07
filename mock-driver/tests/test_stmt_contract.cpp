// Statement-level ODBC contract — IMPROVEMENT_PLAN.md D14
//
// A cluster of small contract violations, each of which let the driver give
// an answer an application cannot distinguish from a legitimate one:
//
//   * SQLNumParams counted `?` inside string literals;
//   * the statement length was truncated from SQLINTEGER to SQLSMALLINT, so a
//     statement of 32 KB or more became "Empty SQL statement";
//   * a SELECT matching no rows opened no cursor, so the SQLCloseCursor an
//     application is told to make then failed 24000;
//   * SQL_CLOSE left the statement "executed", so a later SQLFetch answered
//     SQL_NO_DATA — "no more rows" — instead of 24000, "no cursor";
//   * SQLRowCount answered 0 before the statement had been executed, and
//     reported the result-set size for a SELECT rather than -1;
//   * SQLNumResultCols and SQLDescribeCol described nothing after SQLPrepare;
//   * SQLDescribeCol discarded the parser's column sizes and hard-coded one
//     per type;
//   * unknown statement attributes were accepted, and four standard ones
//     were never implemented at all.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class StmtContractTest : public ::testing::Test {
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
        Exec("CREATE TABLE S (ID INTEGER, V VARCHAR(37))");
        Exec("INSERT INTO S (ID, V) VALUES (1, 'a'), (2, 'b')");
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

    std::string StateOf(SQLHSTMT h) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR msg[256] = {0};
        SQLSMALLINT len = 0;
        if (SQLGetDiagRec(SQL_HANDLE_STMT, h, 1, state, &native,
                          msg, sizeof(msg), &len) == SQL_NO_DATA) {
            return "";
        }
        return std::string(reinterpret_cast<char*>(state));
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── SQLNumParams ─────────────────────────────────────────────────────────

TEST_F(StmtContractTest, NumParamsIgnoresMarkersInsideStringLiterals) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"SELECT ID FROM S WHERE V = '?' OR V = ?", SQL_NTS)));
    SQLSMALLINT n = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumParams(hstmt, &n)));
    EXPECT_EQ(n, 1) << "a `?` inside a string literal was counted as a parameter";
}

TEST_F(StmtContractTest, NumParamsCountsRealMarkers) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"INSERT INTO S (ID, V) VALUES (?, ?)", SQL_NTS)));
    SQLSMALLINT n = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumParams(hstmt, &n)));
    EXPECT_EQ(n, 2);
}

// ── the statement length is an SQLINTEGER ────────────────────────────────

TEST_F(StmtContractTest, AStatementLongerThanASqlSmallintIsNotTruncated) {
    // 40 000 bytes of padding inside a comment-free literal. As an
    // SQLSMALLINT the length wraps negative and sql_to_string returned "".
    std::string big = "SELECT '" + std::string(40000, 'x') + "' AS BIG";
    const SQLRETURN rc = SQLPrepare(
        hstmt, (SQLCHAR*)big.c_str(), static_cast<SQLINTEGER>(big.size()));
    EXPECT_TRUE(SQL_SUCCEEDED(rc))
        << "a 40 KB statement was rejected; state=" << StateOf(hstmt);
}

// ── a cursor opens for a result set, not for rows ────────────────────────

TEST_F(StmtContractTest, AZeroRowSelectStillOpensACursor) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM S WHERE ID = 999", SQL_NTS)));
    EXPECT_EQ(SQLFetch(hstmt), SQL_NO_DATA);
    EXPECT_TRUE(SQL_SUCCEEDED(SQLCloseCursor(hstmt)))
        << "closing the cursor of an empty result set failed; state="
        << StateOf(hstmt);
}

// ── SQL_CLOSE ends the statement, not just the rows ──────────────────────

TEST_F(StmtContractTest, FetchAfterCloseIsInvalidCursorStateNotNoData) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM S", SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFreeStmt(hstmt, SQL_CLOSE)));

    const SQLRETURN rc = SQLFetch(hstmt);
    EXPECT_EQ(rc, SQL_ERROR) << "SQLFetch after SQL_CLOSE answered " << rc;
    EXPECT_EQ(StateOf(hstmt), "24000");
}

// ── SQLRowCount ──────────────────────────────────────────────────────────

TEST_F(StmtContractTest, RowCountBeforeExecuteIsAFunctionSequenceError) {
    SQLHSTMT fresh = SQL_NULL_HSTMT;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &fresh), SQL_SUCCESS);
    SQLLEN n = -12345;
    EXPECT_FALSE(SQL_SUCCEEDED(SQLRowCount(fresh, &n)));
    EXPECT_EQ(StateOf(fresh), "HY010");
    SQLFreeHandle(SQL_HANDLE_STMT, fresh);
}

TEST_F(StmtContractTest, RowCountIsMinusOneForACursorStatement) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT ID FROM S", SQL_NTS)));
    SQLLEN n = -12345;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLRowCount(hstmt, &n)));
    EXPECT_EQ(n, -1) << "SQLRowCount reported the result-set size for a SELECT";
    SQLCloseCursor(hstmt);
}

TEST_F(StmtContractTest, RowCountStillCountsAffectedRowsForDml) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"DELETE FROM S WHERE ID = 1", SQL_NTS)));
    SQLLEN n = -12345;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLRowCount(hstmt, &n)));
    EXPECT_EQ(n, 1);
    SQLCloseCursor(hstmt);
}

// ── result metadata is available after SQLPrepare ────────────────────────

TEST_F(StmtContractTest, NumResultColsIsAnsweredAfterPrepare) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"SELECT ID, V FROM S", SQL_NTS)));
    SQLSMALLINT cols = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols)));
    EXPECT_EQ(cols, 2) << "a prepared SELECT described no columns";
}

TEST_F(StmtContractTest, DescribeColIsAnsweredAfterPrepare) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"SELECT ID, V FROM S", SQL_NTS)));
    char name[64] = {0};
    SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
    SQLULEN size = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDescribeCol(
        hstmt, 1, (SQLCHAR*)name, sizeof(name), &name_len,
        &type, &size, &scale, &nullable)));
    EXPECT_STREQ(name, "ID");
    EXPECT_EQ(type, SQL_INTEGER);
}

// A parameterised SELECT must describe too: the WHERE has no bearing on the
// column list, and at prepare time it still holds unsubstituted markers.
TEST_F(StmtContractTest, DescribeColWorksForAParameterisedPreparedSelect) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"SELECT ID, V FROM S WHERE ID = ?", SQL_NTS)));
    SQLSMALLINT cols = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols)));
    EXPECT_EQ(cols, 2);
}

// ── SQLDescribeCol reports the size the DDL gave ─────────────────────────

TEST_F(StmtContractTest, DescribeColReportsTheDeclaredColumnSize) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT V FROM S", SQL_NTS)));
    char name[64] = {0};
    SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
    SQLULEN size = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDescribeCol(
        hstmt, 1, (SQLCHAR*)name, sizeof(name), &name_len,
        &type, &size, &scale, &nullable)));
    SQLCloseCursor(hstmt);
    // The table declares VARCHAR(37); the hard-coded per-type default was 255.
    EXPECT_EQ(size, 37u) << "SQLDescribeCol ignored the declared column size";
}

// ── statement attributes ─────────────────────────────────────────────────

TEST_F(StmtContractTest, UnknownAttributeIsRejectedRatherThanAccepted) {
    const SQLINTEGER kNotAnAttribute = 999999;
    EXPECT_FALSE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, kNotAnAttribute, (SQLPOINTER)1, 0)));
    EXPECT_EQ(StateOf(hstmt), "HY092");

    SQLULEN v = 0;
    SQLINTEGER len = 0;
    EXPECT_FALSE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, kNotAnAttribute, &v, sizeof(v), &len)));
    EXPECT_EQ(StateOf(hstmt), "HY092");
}

TEST_F(StmtContractTest, TheFourStandardAttributesRoundTrip) {
    struct Case { SQLINTEGER attr; SQLULEN value; const char* name; };
    const Case cases[] = {
        {SQL_ATTR_MAX_LENGTH,    1024,           "SQL_ATTR_MAX_LENGTH"},
        {SQL_ATTR_NOSCAN,        SQL_NOSCAN_ON,  "SQL_ATTR_NOSCAN"},
        {SQL_ATTR_RETRIEVE_DATA, SQL_RD_OFF,     "SQL_ATTR_RETRIEVE_DATA"},
    };
    for (const auto& c : cases) {
        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
            hstmt, c.attr, (SQLPOINTER)(SQLULEN)c.value, 0))) << c.name;
        SQLULEN got = 0;
        SQLINTEGER len = 0;
        ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
            hstmt, c.attr, &got, sizeof(got), &len))) << c.name;
        EXPECT_EQ(got, c.value) << c.name << " did not round-trip";
    }
}

// Asking for a scrollable cursor and then being told the cursor is
// forward-only is the contradiction a conformance probe looks for.
TEST_F(StmtContractTest, ScrollableCursorAttributeAgreesWithCursorType) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_CURSOR_SCROLLABLE, (SQLPOINTER)SQL_SCROLLABLE, 0)));

    SQLULEN scrollable = 0, cursor_type = 0;
    SQLINTEGER len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_CURSOR_SCROLLABLE, &scrollable, sizeof(scrollable), &len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_CURSOR_TYPE, &cursor_type, sizeof(cursor_type), &len)));

    EXPECT_EQ(scrollable, (SQLULEN)SQL_SCROLLABLE);
    EXPECT_NE(cursor_type, (SQLULEN)SQL_CURSOR_FORWARD_ONLY)
        << "the cursor stayed forward-only after being made scrollable";
}

}  // namespace
