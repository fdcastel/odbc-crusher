// SQLGetDiagField header fields (record 0) — IMPROVEMENT_PLAN.md D22 / D14
//
// SQL_DIAG_RETURNCODE, SQL_DIAG_DYNAMIC_FUNCTION,
// SQL_DIAG_DYNAMIC_FUNCTION_CODE and SQL_DIAG_CURSOR_ROW_COUNT were declared
// on OdbcHandle and read by diagnostic_api.cpp, but written by nobody — so
// every read returned the initial value and the driver reported fiction:
// SQL_SUCCESS after an error, an empty statement name after a SELECT, and a
// cursor row count of 0 for a ten-row result set.
//
// SQL_DIAG_ROW_COUNT was zero for a different reason (D14): StatementHandle
// declared a second `row_count_` that shadowed the base class's, so
// SQLRowCount wrote one field and SQLGetDiagField read the other. The two
// contradicted each other, which is the shape this file pins down.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class DiagHeaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        const char* conn =
            "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
        Exec("CREATE TABLE H (ID INTEGER, V VARCHAR(16))");
        Exec("INSERT INTO H (ID, V) VALUES (1, 'a'), (2, 'b'), (3, 'c')");
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

    SQLRETURN Run(const std::string& sql) {
        return SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
    }

    SQLRETURN Header(SQLSMALLINT field, SQLPOINTER value, SQLSMALLINT len,
                     SQLSMALLINT* out_len) {
        return SQLGetDiagField(SQL_HANDLE_STMT, hstmt, 0, field, value, len, out_len);
    }

    SQLLEN HeaderLen(SQLSMALLINT field) {
        SQLLEN v = -12345;
        SQLSMALLINT n = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(Header(field, &v, 0, &n)));
        return v;
    }

    SQLINTEGER HeaderInt(SQLSMALLINT field) {
        SQLINTEGER v = -12345;
        SQLSMALLINT n = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(Header(field, &v, 0, &n)));
        return v;
    }

    std::string HeaderText(SQLSMALLINT field) {
        char buf[128] = {0};
        SQLSMALLINT n = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(Header(field, buf, sizeof(buf), &n)));
        return buf;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── SQL_DIAG_RETURNCODE ──────────────────────────────────────────────────

TEST_F(DiagHeaderTest, ReturnCodeIsSuccessAfterASuccessfulCall) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("SELECT ID FROM H")));
    SQLRETURN rc = SQL_ERROR;
    SQLSMALLINT n = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(Header(SQL_DIAG_RETURNCODE, &rc, 0, &n)));
    EXPECT_EQ(rc, SQL_SUCCESS);
    SQLCloseCursor(hstmt);
}

// The one that was fiction: after SQL_ERROR the field still said SQL_SUCCESS.
TEST_F(DiagHeaderTest, ReturnCodeIsErrorAfterAFailedCall) {
    ASSERT_FALSE(SQL_SUCCEEDED(Run("SELECT ID FROM NO_SUCH_TABLE")));
    SQLRETURN rc = SQL_SUCCESS;
    SQLSMALLINT n = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(Header(SQL_DIAG_RETURNCODE, &rc, 0, &n)));
    EXPECT_EQ(rc, SQL_ERROR)
        << "SQL_DIAG_RETURNCODE reported success after a failed statement";
}

// A warning is not an error, and an error is not downgraded by one.
TEST_F(DiagHeaderTest, ReturnCodeIsSuccessWithInfoAfterATruncatingRead) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("SELECT V FROM H")));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    char tiny[1] = {0};          // one byte: the terminator and nothing else
    SQLLEN ind = 0;
    const SQLRETURN got = SQLGetData(hstmt, 1, SQL_C_CHAR, tiny, sizeof(tiny), &ind);
    ASSERT_EQ(got, SQL_SUCCESS_WITH_INFO);

    SQLRETURN rc = SQL_SUCCESS;
    SQLSMALLINT n = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(Header(SQL_DIAG_RETURNCODE, &rc, 0, &n)));
    EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO);
    SQLCloseCursor(hstmt);
}

// ── SQL_DIAG_DYNAMIC_FUNCTION / _CODE ────────────────────────────────────

TEST_F(DiagHeaderTest, DynamicFunctionNamesTheStatementJustExecuted) {
    struct Case { const char* sql; const char* name; SQLINTEGER code; };
    const Case cases[] = {
        {"SELECT ID FROM H",                  "SELECT CURSOR", SQL_DIAG_SELECT_CURSOR},
        {"INSERT INTO H (ID, V) VALUES (9, 'z')", "INSERT",    SQL_DIAG_INSERT},
        {"UPDATE H SET V = 'q' WHERE ID = 9", "UPDATE WHERE",  SQL_DIAG_UPDATE_WHERE},
        {"DELETE FROM H WHERE ID = 9",        "DELETE WHERE",  SQL_DIAG_DELETE_WHERE},
    };
    for (const auto& c : cases) {
        ASSERT_TRUE(SQL_SUCCEEDED(Run(c.sql))) << c.sql;
        EXPECT_EQ(HeaderText(SQL_DIAG_DYNAMIC_FUNCTION), c.name) << c.sql;
        EXPECT_EQ(HeaderInt(SQL_DIAG_DYNAMIC_FUNCTION_CODE), c.code) << c.sql;
        SQLCloseCursor(hstmt);
    }
}

TEST_F(DiagHeaderTest, DynamicFunctionReportsCreateAndDropTable) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("CREATE TABLE H2 (ID INTEGER)")));
    EXPECT_EQ(HeaderText(SQL_DIAG_DYNAMIC_FUNCTION), "CREATE TABLE");
    EXPECT_EQ(HeaderInt(SQL_DIAG_DYNAMIC_FUNCTION_CODE), SQL_DIAG_CREATE_TABLE);
    SQLCloseCursor(hstmt);

    ASSERT_TRUE(SQL_SUCCEEDED(Run("DROP TABLE H2")));
    EXPECT_EQ(HeaderText(SQL_DIAG_DYNAMIC_FUNCTION), "DROP TABLE");
    EXPECT_EQ(HeaderInt(SQL_DIAG_DYNAMIC_FUNCTION_CODE), SQL_DIAG_DROP_TABLE);
    SQLCloseCursor(hstmt);
}

// ── SQL_DIAG_CURSOR_ROW_COUNT ────────────────────────────────────────────

TEST_F(DiagHeaderTest, CursorRowCountCountsTheRowsInTheCursor) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("SELECT ID FROM H")));
    EXPECT_EQ(HeaderLen(SQL_DIAG_CURSOR_ROW_COUNT), 3);
    SQLCloseCursor(hstmt);
}

// A statement that opens no cursor has no cursor row count.
TEST_F(DiagHeaderTest, CursorRowCountIsZeroForNonCursorStatements) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("INSERT INTO H (ID, V) VALUES (8, 'y')")));
    EXPECT_EQ(HeaderLen(SQL_DIAG_CURSOR_ROW_COUNT), 0);
    SQLCloseCursor(hstmt);
}

// ── SQL_DIAG_ROW_COUNT vs SQLRowCount (D14's shadowing) ──────────────────

TEST_F(DiagHeaderTest, DiagRowCountAgreesWithSqlRowCountAfterDml) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("DELETE FROM H WHERE ID <= 2")));
    SQLLEN from_api = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLRowCount(hstmt, &from_api)));
    EXPECT_EQ(from_api, 2);
    EXPECT_EQ(HeaderLen(SQL_DIAG_ROW_COUNT), from_api)
        << "SQL_DIAG_ROW_COUNT contradicted SQLRowCount";
    SQLCloseCursor(hstmt);
}

TEST_F(DiagHeaderTest, DiagRowCountAgreesWithSqlRowCountAfterSelect) {
    ASSERT_TRUE(SQL_SUCCEEDED(Run("SELECT ID FROM H")));
    SQLLEN from_api = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLRowCount(hstmt, &from_api)));
    EXPECT_EQ(HeaderLen(SQL_DIAG_ROW_COUNT), from_api);
    SQLCloseCursor(hstmt);
}

}  // namespace
