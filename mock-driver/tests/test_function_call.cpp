// The ODBC function-call escape `{?=CALL fn(...)}` — IMPROVEMENT_PLAN.md D13
//
// Before D13 the mock's parser recognised only a leading CALL / EXECUTE
// PROCEDURE, so `?=CALL fn(...)` — what preprocess_escape_sequences produces
// from `{?=CALL fn(...)}` — fell through to "Unsupported SQL statement type",
// and there was no callable function in the catalog to run against anyway.
// A17 and I7 were deferred behind both halves.
//
// What matters here is the parameter numbering. In the function-call form the
// leading `?` is the return value and occupies parameter 1, so the first
// argument is parameter 2. A driver that binds the first argument as
// parameter 1 is the classic defect those probes look for, so the mock has to
// model the correct numbering exactly — and MOCK_FN returns a*10 + b so that
// arguments landing in the wrong slots give a visibly wrong answer.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class FunctionCallTest : public ::testing::Test {
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
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// The whole point: parameter 1 is the return value, arguments start at 2.
TEST_F(FunctionCallTest, ReturnValueIsParameterOneAndArgumentsFollow) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"{?=CALL MOCK_FN(?, ?)}", SQL_NTS)));

    SQLINTEGER ret = -1, a = 4, b = 7;
    SQLLEN ret_ind = 0, a_ind = 0, b_ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 1, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
        &ret, 0, &ret_ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
        &a, 0, &a_ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
        &b, 0, &b_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(hstmt)));
    // 4*10 + 7. Reading the arguments one slot early would give 0*10 + 4 = 4.
    EXPECT_EQ(ret, 47) << "the return-value binding was read as an argument";
}

// The spaced spelling is the same statement.
TEST_F(FunctionCallTest, SpacedReturnMarkerParsesToo) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"{ ? = CALL MOCK_FN(?, ?) }", SQL_NTS)));

    SQLINTEGER ret = -1, a = 1, b = 2;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 1, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &ret, 0, &ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &a, 0, &ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &b, 0, &ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(hstmt)));
    EXPECT_EQ(ret, 12);
}

// A plain `{CALL proc(...)}` must keep numbering its arguments from 1 — the
// offset applies to the function form only.
TEST_F(FunctionCallTest, ProcedureCallStillNumbersArgumentsFromOne) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
        hstmt, (SQLCHAR*)"{CALL MOCK_INOUT(?, ?, ?)}", SQL_NTS)));

    SQLINTEGER n = 21, m = -1;
    char s[64] = "abc";
    SQLLEN n_ind = 0, m_ind = 0, s_ind = SQL_NTS;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &n, 0, &n_ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 2, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &m, 0, &m_ind)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
        hstmt, 3, SQL_PARAM_INPUT_OUTPUT, SQL_C_CHAR, SQL_VARCHAR, 64, 0,
        s, sizeof(s), &s_ind)));

    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(hstmt)));
    EXPECT_EQ(m, 42);
    EXPECT_STREQ(s, "ABC");
}

// The function is enumerable, so a driver can discover it the usual way.
TEST_F(FunctionCallTest, TheFunctionIsListedBySqlProcedures) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedures(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_FN", SQL_NTS)));
    int rows = 0;
    while (SQLFetch(hstmt) == SQL_SUCCESS) ++rows;
    SQLCloseCursor(hstmt);
    EXPECT_EQ(rows, 1);
}

// SQLProcedureColumns must report the return value as SQL_RETURN_VALUE, which
// is how a client learns that arguments start at parameter 2.
TEST_F(FunctionCallTest, ProcedureColumnsReportsTheReturnValueSlot) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_FN", SQL_NTS, NULL, 0)));

    bool saw_return = false;
    int rows = 0;
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        SQLSMALLINT col_type = 0;
        SQLLEN ind = 0;
        // COLUMN_TYPE is column 5 of the SQLProcedureColumns result set.
        if (SQL_SUCCEEDED(SQLGetData(hstmt, 5, SQL_C_SSHORT, &col_type, 0, &ind))
            && col_type == SQL_RETURN_VALUE) {
            saw_return = true;
        }
        ++rows;
    }
    SQLCloseCursor(hstmt);
    EXPECT_GT(rows, 0);
    EXPECT_TRUE(saw_return) << "no SQL_RETURN_VALUE column for a function";
}

}  // namespace
