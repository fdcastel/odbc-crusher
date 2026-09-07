// SQLGetInfo: the ANSI and W paths must agree — IMPROVEMENT_PLAN.md D49
//
// SQLGetInfoW kept its own hand-maintained list of which info types answer
// with a character string, so it could convert them. The list drifted from the
// switch it described: nine string types were missing, and each came back
// *empty* through the W path. On Windows that is every path — the driver
// manager converts an application's ANSI call into a W call before it reaches
// the driver — so `SQL_LIKE_ESCAPE_CLAUSE` read as "" in a tool that asked for
// it, and a probe checking the claim saw no claim at all.
//
// Found while giving test_like_escape_sequence something to grade (D44): the
// driver answered "Y" when asked directly and "" when asked through the tool.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace {

class GetInfoPathsTest : public ::testing::Test {
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
    }

    void TearDown() override {
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    // The ANSI answer as text, or nullopt when the type is not answered.
    bool AnsiString(SQLUSMALLINT type, std::string* out) {
        SQLCHAR buf[512] = {0};
        SQLSMALLINT len = -1;
        if (!SQL_SUCCEEDED(SQLGetInfo(hdbc, type, buf, sizeof(buf), &len))) {
            return false;
        }
        if (len < 0) return false;
        *out = std::string(reinterpret_cast<char*>(buf), static_cast<size_t>(len));
        return true;
    }

    // The same through the W entry point, decoded back to narrow text.
    bool WideString(SQLUSMALLINT type, std::string* out) {
        SQLWCHAR wbuf[512] = {0};
        SQLSMALLINT bytes = -1;
        if (!SQL_SUCCEEDED(SQLGetInfoW(hdbc, type, wbuf, sizeof(wbuf), &bytes))) {
            return false;
        }
        if (bytes < 0) return false;
        out->clear();
        const size_t chars = static_cast<size_t>(bytes) / sizeof(SQLWCHAR);
        for (size_t i = 0; i < chars && wbuf[i]; ++i) {
            out->push_back(static_cast<char>(wbuf[i] & 0x7F));
        }
        return true;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
};

// The nine that were missing from the wrapper's list, plus a few that were
// already on it so the test would notice a fix that broke the other direction.
TEST_F(GetInfoPathsTest, StringInfoTypesAreNotEmpty) {
    struct Case { SQLUSMALLINT type; const char* name; };
    static const Case kCases[] = {
        {SQL_LIKE_ESCAPE_CLAUSE,         "SQL_LIKE_ESCAPE_CLAUSE"},
        {SQL_KEYWORDS,                   "SQL_KEYWORDS"},
        {SQL_IDENTIFIER_QUOTE_CHAR,      "SQL_IDENTIFIER_QUOTE_CHAR"},
        {SQL_SEARCH_PATTERN_ESCAPE,      "SQL_SEARCH_PATTERN_ESCAPE"},
        {SQL_DRIVER_NAME,                "SQL_DRIVER_NAME"},
        {SQL_DBMS_NAME,                  "SQL_DBMS_NAME"},
        {SQL_CATALOG_NAME_SEPARATOR,     "SQL_CATALOG_NAME_SEPARATOR"},
        {SQL_ORDER_BY_COLUMNS_IN_SELECT, "SQL_ORDER_BY_COLUMNS_IN_SELECT"},
        {SQL_PROCEDURES,                 "SQL_PROCEDURES"},
        {SQL_OUTER_JOINS,                "SQL_OUTER_JOINS"},
    };
    for (const auto& c : kCases) {
        std::string ansi;
        ASSERT_TRUE(AnsiString(c.type, &ansi)) << c.name << " is not answered";
        EXPECT_FALSE(ansi.empty()) << c.name << " answered an empty string";
    }
}

// The two paths describe the same driver, so they must say the same thing.
TEST_F(GetInfoPathsTest, TheAnsiAndWidePathsAgree) {
    struct Case { SQLUSMALLINT type; const char* name; };
    static const Case kCases[] = {
        {SQL_LIKE_ESCAPE_CLAUSE,     "SQL_LIKE_ESCAPE_CLAUSE"},
        {SQL_KEYWORDS,               "SQL_KEYWORDS"},
        {SQL_IDENTIFIER_QUOTE_CHAR,  "SQL_IDENTIFIER_QUOTE_CHAR"},
        {SQL_DRIVER_NAME,            "SQL_DRIVER_NAME"},
        {SQL_DBMS_NAME,              "SQL_DBMS_NAME"},
        {SQL_PROCEDURES,             "SQL_PROCEDURES"},
    };
    for (const auto& c : kCases) {
        std::string ansi, wide;
        ASSERT_TRUE(AnsiString(c.type, &ansi)) << c.name;
        ASSERT_TRUE(WideString(c.type, &wide)) << c.name;
        EXPECT_EQ(ansi, wide)
            << c.name << ": the ANSI path says '" << ansi
            << "' and the W path says '" << wide << "'";
    }
}

// A numeric type must not be mistaken for a string by either path.
TEST_F(GetInfoPathsTest, NumericInfoTypesStayNumeric) {
    SQLUSMALLINT ansi = 0xFFFF, wide = 0xFFFF;
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfo(
        hdbc, SQL_MAX_COLUMN_NAME_LEN, &ansi, sizeof(ansi), &len)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfoW(
        hdbc, SQL_MAX_COLUMN_NAME_LEN, &wide, sizeof(wide), &len)));
    EXPECT_EQ(ansi, wide);
    EXPECT_NE(ansi, 0xFFFF) << "the numeric answer was not written";
}

}  // namespace
