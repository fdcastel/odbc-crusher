// Descriptors, cursor names, connection limits — IMPROVEMENT_PLAN.md D29
//
// The theme is entry points that reported success for things they did not
// implement, which an application cannot tell from a real answer:
//
//   * SQLColAttribute returned SQL_SUCCESS with a zeroed output for every
//     field it did not handle, including ten the spec defines;
//   * SQLGetDescField and SQLSetDescField did the same, so an ARD
//     configuration for SQL_C_NUMERIC was accepted and discarded;
//   * SQLGetDescRec explicitly threw away its name output;
//   * SQLSetCursorName discarded the name and SQLGetCursorName synthesised
//     one from the handle address, so set-then-get did not round-trip and a
//     heap pointer leaked into text applications put into SQL;
//   * MaxConnections was off by one, because the constructor has already
//     pushed `this` onto the environment's list;
//   * SQLGetFunctions claimed SQLSetPos and SQLBulkOperations, both of which
//     always answer HYC00.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>

namespace {

class DescriptorContractTest : public ::testing::Test {
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
        Exec("CREATE TABLE D29 (ID INTEGER, V VARCHAR(23))");
        Exec("INSERT INTO D29 (ID, V) VALUES (1, 'a')");
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

    void OpenResultSet() {
        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)"SELECT ID, V FROM D29", SQL_NTS)));
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

// ── SQLColAttribute's missing fields ─────────────────────────────────────

TEST_F(DescriptorContractTest, ColAttributeCountReportsTheColumnCount) {
    OpenResultSet();
    SQLLEN n = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 1, SQL_DESC_COUNT, NULL, 0, NULL, &n)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(n, 2) << "SQL_DESC_COUNT said the result set had no columns";
}

TEST_F(DescriptorContractTest, ColAttributeTypeNameNamesTheType) {
    OpenResultSet();
    char buf[64] = {0};
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 1, SQL_DESC_TYPE_NAME, buf, sizeof(buf), &len, NULL)));
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(buf, "INTEGER");
}

TEST_F(DescriptorContractTest, ColAttributeLabelIsTheColumnName) {
    OpenResultSet();
    char buf[64] = {0};
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 2, SQL_DESC_LABEL, buf, sizeof(buf), &len, NULL)));
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(buf, "V");
}

TEST_F(DescriptorContractTest, ColAttributeCaseSensitiveDistinguishesTextFromNumbers) {
    OpenResultSet();
    SQLLEN id_cs = -1, v_cs = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 1, SQL_DESC_CASE_SENSITIVE, NULL, 0, NULL, &id_cs)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 2, SQL_DESC_CASE_SENSITIVE, NULL, 0, NULL, &v_cs)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(id_cs, SQL_FALSE) << "an INTEGER was reported case-sensitive";
    EXPECT_EQ(v_cs, SQL_TRUE) << "a VARCHAR was reported case-insensitive";
}

TEST_F(DescriptorContractTest, ColAttributeUnnamedReportsNamedForARealColumn) {
    OpenResultSet();
    SQLLEN unnamed = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 1, SQL_DESC_UNNAMED, NULL, 0, NULL, &unnamed)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(unnamed, SQL_NAMED);
}

TEST_F(DescriptorContractTest, ColAttributeOctetLengthUsesTheDeclaredSize) {
    OpenResultSet();
    SQLLEN octets = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 2, SQL_DESC_OCTET_LENGTH, NULL, 0, NULL, &octets)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(octets, 23) << "the declared VARCHAR(23) size was ignored";
}

TEST_F(DescriptorContractTest, AnUnknownColAttributeFieldIsHy091) {
    OpenResultSet();
    const SQLUSMALLINT kNotAField = 9999;
    SQLLEN n = 0;
    EXPECT_FALSE(SQL_SUCCEEDED(SQLColAttribute(
        hstmt, 1, kNotAField, NULL, 0, NULL, &n)));
    EXPECT_EQ(StmtState(), "HY091");
    SQLCloseCursor(hstmt);
}

// ── the descriptor record fields round-trip ──────────────────────────────
//
// Configuring an ARD is the only way to bind SQL_C_NUMERIC with a chosen
// precision and scale. The fields were accepted and discarded, so the probes
// that do this had been passing against a driver that ignored them.

TEST_F(DescriptorContractTest, ArdNumericFieldsRoundTrip) {
    SQLHDESC ard = SQL_NULL_HDESC;
    SQLINTEGER len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_APP_ROW_DESC, &ard, 0, &len)));
    ASSERT_NE(ard, nullptr);

    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetDescField(
        ard, 1, SQL_DESC_TYPE, (SQLPOINTER)(SQLLEN)SQL_C_NUMERIC, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetDescField(
        ard, 1, SQL_DESC_PRECISION, (SQLPOINTER)(SQLLEN)15, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetDescField(
        ard, 1, SQL_DESC_SCALE, (SQLPOINTER)(SQLLEN)4, 0)));

    SQLSMALLINT type = 0, precision = 0, scale = 0;
    SQLINTEGER out_len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDescField(
        ard, 1, SQL_DESC_TYPE, &type, 0, &out_len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDescField(
        ard, 1, SQL_DESC_PRECISION, &precision, 0, &out_len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDescField(
        ard, 1, SQL_DESC_SCALE, &scale, 0, &out_len)));

    EXPECT_EQ(type, SQL_C_NUMERIC);
    EXPECT_EQ(precision, 15) << "the ARD accepted a precision it did not keep";
    EXPECT_EQ(scale, 4) << "the ARD accepted a scale it did not keep";
}

TEST_F(DescriptorContractTest, AnUnknownDescriptorFieldIsHy091) {
    SQLHDESC ard = SQL_NULL_HDESC;
    SQLINTEGER len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_APP_ROW_DESC, &ard, 0, &len)));

    const SQLSMALLINT kNotAField = 9999;
    EXPECT_FALSE(SQL_SUCCEEDED(SQLSetDescField(
        ard, 1, kNotAField, (SQLPOINTER)(SQLLEN)1, 0)));

    SQLCHAR state[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT msg_len = 0;
    ASSERT_NE(SQLGetDiagRec(SQL_HANDLE_DESC, ard, 1, state, &native,
                            msg, sizeof(msg), &msg_len), SQL_NO_DATA);
    EXPECT_STREQ(reinterpret_cast<char*>(state), "HY091");
}

// ── the cursor name round-trips ──────────────────────────────────────────

TEST_F(DescriptorContractTest, CursorNameRoundTrips) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetCursorName(
        hstmt, (SQLCHAR*)"MY_CURSOR", SQL_NTS)));
    char buf[64] = {0};
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetCursorName(
        hstmt, (SQLCHAR*)buf, sizeof(buf), &len)));
    EXPECT_STREQ(buf, "MY_CURSOR")
        << "the name set was discarded and one was synthesised instead";
}

// The generated fallback must not expose an address.
TEST_F(DescriptorContractTest, TheGeneratedCursorNameDoesNotLeakAPointer) {
    char buf[64] = {0};
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetCursorName(
        hstmt, (SQLCHAR*)buf, sizeof(buf), &len)));
    const std::string name(buf);
    ASSERT_EQ(name.rfind("SQL_CUR", 0), 0u);
    // A handle address renders as a very long number; an ordinal does not.
    EXPECT_LT(name.size(), 16u)
        << "the cursor name looks like it contains a heap address: " << name;
}

// ── SQLGetFunctions tells the truth ──────────────────────────────────────

TEST_F(DescriptorContractTest, UnimplementedFunctionsAreNotClaimed) {
    SQLUSMALLINT supported = SQL_TRUE;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetFunctions(hdbc, SQL_API_SQLSETPOS, &supported)));
    EXPECT_EQ(supported, SQL_FALSE)
        << "SQLSetPos is claimed but always answers HYC00";

    supported = SQL_TRUE;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetFunctions(
        hdbc, SQL_API_SQLBULKOPERATIONS, &supported)));
    EXPECT_EQ(supported, SQL_FALSE)
        << "SQLBulkOperations is claimed but always answers HYC00";
}

TEST_F(DescriptorContractTest, ImplementedFunctionsAreStillClaimed) {
    SQLUSMALLINT supported = SQL_FALSE;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetFunctions(hdbc, SQL_API_SQLFETCH, &supported)));
    EXPECT_EQ(supported, SQL_TRUE);
}

// ── SQLGetInfo answers the types that used to be HY096 ───────────────────

TEST_F(DescriptorContractTest, PreviouslyUnansweredInfoTypesAreAnswered) {
    struct Case { SQLUSMALLINT type; const char* name; bool is_string; };
    const Case cases[] = {
        {SQL_QUOTED_IDENTIFIER_CASE,          "SQL_QUOTED_IDENTIFIER_CASE", false},
        {SQL_CORRELATION_NAME,                "SQL_CORRELATION_NAME", false},
        {SQL_MAX_STATEMENT_LEN,               "SQL_MAX_STATEMENT_LEN", false},
        {SQL_STATIC_CURSOR_ATTRIBUTES2,       "SQL_STATIC_CURSOR_ATTRIBUTES2", false},
        {SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2, "SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES2", false},
        {SQL_KEYWORDS,                        "SQL_KEYWORDS", true},
    };
    for (const auto& c : cases) {
        char buf[512] = {0};
        SQLSMALLINT len = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLGetInfo(
            hdbc, c.type, buf, sizeof(buf), &len)))
            << c.name << " still answers HY096";
    }
}

// ── MaxConnections counts established connections ────────────────────────
//
// The check was `env->connections_.size() >= max`, but the ConnectionHandle
// constructor has already pushed `this` - so MaxConnections=1 rejected the
// very first connect. BehaviorController is process-global, so this test
// restores Mode=Success before it returns.
TEST(ConnectionBudget, MaxConnectionsOfOneAllowsTheFirstConnection) {
    SQLHENV env = SQL_NULL_HENV;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env), SQL_SUCCESS);
    ASSERT_EQ(SQLSetEnvAttr(env, SQL_ATTR_ODBC_VERSION,
                            (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);

    const char* limited =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;MaxConnections=1;";

    SQLHDBC first = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, env, &first), SQL_SUCCESS);
    EXPECT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
        first, NULL, (SQLCHAR*)limited, SQL_NTS, NULL, 0, NULL,
        SQL_DRIVER_NOPROMPT)))
        << "MaxConnections=1 rejected the first connection";

    // ...and the second is refused, which is the point of the limit.
    SQLHDBC second = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, env, &second), SQL_SUCCESS);
    EXPECT_FALSE(SQL_SUCCEEDED(SQLDriverConnect(
        second, NULL, (SQLCHAR*)limited, SQL_NTS, NULL, 0, NULL,
        SQL_DRIVER_NOPROMPT)));

    SQLFreeHandle(SQL_HANDLE_DBC, second);
    SQLDisconnect(first);
    SQLFreeHandle(SQL_HANDLE_DBC, first);

    SQLHDBC restore = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, env, &restore), SQL_SUCCESS);
    SQLDriverConnect(restore, NULL,
                     (SQLCHAR*)"Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;",
                     SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    SQLDisconnect(restore);
    SQLFreeHandle(SQL_HANDLE_DBC, restore);
    SQLFreeHandle(SQL_HANDLE_ENV, env);
}

}  // namespace
