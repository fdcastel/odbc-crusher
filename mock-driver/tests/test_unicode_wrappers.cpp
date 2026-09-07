// The W entry points — IMPROVEMENT_PLAN.md D17
//
// D17: "any SQL…W function at all" returned zero references from the mock's
// tests, so all of unicode_wrappers.cpp and every string-conversion helper
// were untested — despite the W functions being the only entry points Windows
// exports, which makes them the ones the driver manager actually calls.
//
// D49 is what that gap cost: the wrapper kept its own list of which
// SQLGetInfo types are strings, the list drifted, and nine came back empty or
// garbled with nothing to notice.
//
// The three things a wrapper can get wrong are length units (characters
// versus bytes), truncation signalling, and the conversion itself. This
// covers all three across the wrapper families.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace {

// Decode a NUL-terminated SQLWCHAR buffer as ASCII. Every value this file
// asks for is ASCII, so the narrow comparison is exact.
std::string Narrow(const SQLWCHAR* w, size_t max_chars) {
    std::string out;
    for (size_t i = 0; i < max_chars && w[i]; ++i) {
        out.push_back(static_cast<char>(w[i] & 0x7F));
    }
    return out;
}

class UnicodeWrapperTest : public ::testing::Test {
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

    // A wide copy of a narrow string, for passing into a W entry point.
    static std::vector<SQLWCHAR> Wide(const std::string& s) {
        std::vector<SQLWCHAR> out(s.size() + 1, 0);
        for (size_t i = 0; i < s.size(); ++i) {
            out[i] = static_cast<SQLWCHAR>(static_cast<unsigned char>(s[i]));
        }
        return out;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── a W statement really executes ────────────────────────────────────────

TEST_F(UnicodeWrapperTest, ExecDirectWRunsTheStatement) {
    auto sql = Wide("SELECT 1");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirectW(hstmt, sql.data(), SQL_NTS)));
    EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    SQLCloseCursor(hstmt);
}

TEST_F(UnicodeWrapperTest, PrepareWAndExecuteRunTheStatement) {
    auto sql = Wide("SELECT 1");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepareW(hstmt, sql.data(), SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(hstmt)));
    EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    SQLCloseCursor(hstmt);
}

// ── SQLGetInfoW: the D49 surface ─────────────────────────────────────────

TEST_F(UnicodeWrapperTest, GetInfoWReportsLengthInBytesNotCharacters) {
    SQLWCHAR buf[64] = {0};
    SQLSMALLINT bytes = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfoW(
        hdbc, SQL_DRIVER_NAME, buf, sizeof(buf), &bytes)));
    const std::string text = Narrow(buf, 64);
    EXPECT_EQ(text, "mockodbc.dll");
    // The spec is explicit: the length a W function reports is in bytes.
    EXPECT_EQ(bytes, static_cast<SQLSMALLINT>(text.size() * sizeof(SQLWCHAR)))
        << "the length looks like characters rather than bytes";
}

TEST_F(UnicodeWrapperTest, GetInfoWTruncatesWithoutRunningOffTheBuffer) {
    // Room for three wide characters including the terminator.
    SQLWCHAR buf[8];
    for (auto& c : buf) c = 0xAAAA;      // poison, so an over-write shows
    SQLSMALLINT bytes = -1;
    const SQLRETURN rc = SQLGetInfoW(
        hdbc, SQL_DRIVER_NAME, buf, 3 * sizeof(SQLWCHAR), &bytes);
    ASSERT_TRUE(SQL_SUCCEEDED(rc));
    // Whatever it wrote must be NUL-terminated inside the buffer it was given.
    EXPECT_EQ(buf[2], 0) << "the wrapper did not terminate inside the buffer";
    EXPECT_EQ(buf[3], 0xAAAA) << "the wrapper wrote past the buffer it was given";
    EXPECT_EQ(Narrow(buf, 3), "mo");
}

// ── SQLGetDiagRecW: the state is five characters, the message is text ─────

TEST_F(UnicodeWrapperTest, GetDiagRecWReturnsTheStateAndMessage) {
    auto bad = Wide("SELECT * FROM NO_SUCH_TABLE_AT_ALL");
    ASSERT_FALSE(SQL_SUCCEEDED(SQLExecDirectW(hstmt, bad.data(), SQL_NTS)));

    SQLWCHAR state[6] = {0};
    SQLINTEGER native = 0;
    SQLWCHAR msg[512] = {0};
    SQLSMALLINT msg_len = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDiagRecW(
        SQL_HANDLE_STMT, hstmt, 1, state, &native, msg, 512, &msg_len)));
    EXPECT_EQ(Narrow(state, 6).size(), 5u) << "SQLSTATE is five characters";
    EXPECT_FALSE(Narrow(msg, 512).empty()) << "the message came back empty";
    // SQLGetDiagRec reports the message length in *characters*, unlike
    // SQLGetInfo's bytes - the two conventions are the classic wrapper bug.
    EXPECT_EQ(msg_len, static_cast<SQLSMALLINT>(Narrow(msg, 512).size()));
}

// ── the catalog W wrappers ───────────────────────────────────────────────

TEST_F(UnicodeWrapperTest, TablesWFindsTheSameTablesAsTheAnsiPath) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    int ansi_rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++ansi_rows;
    SQLCloseCursor(hstmt);

    ASSERT_TRUE(SQL_SUCCEEDED(SQLTablesW(hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    int wide_rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++wide_rows;
    SQLCloseCursor(hstmt);

    EXPECT_GT(ansi_rows, 0);
    EXPECT_EQ(ansi_rows, wide_rows)
        << "the W catalog wrapper found a different number of tables";
}

TEST_F(UnicodeWrapperTest, TablesWHonoursAWideNameFilter) {
    auto name = Wide("CUSTOMERS");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTablesW(
        hstmt, NULL, 0, NULL, 0, name.data(), SQL_NTS, NULL, 0)));
    int rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++rows;
    SQLCloseCursor(hstmt);
    EXPECT_EQ(rows, 1) << "the wide table-name filter was not applied";
}

TEST_F(UnicodeWrapperTest, ColumnsWHonoursAWideNameFilter) {
    auto table = Wide("CUSTOMERS");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColumnsW(
        hstmt, NULL, 0, NULL, 0, table.data(), SQL_NTS, NULL, 0)));
    int rows = 0;
    while (SQL_SUCCEEDED(SQLFetch(hstmt))) ++rows;
    SQLCloseCursor(hstmt);
    EXPECT_GT(rows, 0) << "SQLColumnsW found no columns for a table that has them";
}

// D25's null-versus-empty distinction has to survive the W wrapper, which is
// where it used to be destroyed: `X.empty() ? nullptr : ...` at 37 argument
// positions collapsed the two.
TEST_F(UnicodeWrapperTest, AnEmptyWideArgumentIsNotANullPointer) {
    SQLWCHAR empty[1] = {0};
    auto percent = Wide("%");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTablesW(
        hstmt,
        percent.data(), SQL_NTS,   // catalog = "%"
        empty, SQL_NTS,            // schema  = ""
        empty, SQL_NTS,            // table   = ""
        NULL, 0)));
    // SQL_ALL_CATALOGS: catalog names only, so TABLE_NAME is NULL.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));
    char name[64] = {0};
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 3, SQL_C_CHAR, name, sizeof(name), &ind)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(ind, SQL_NULL_DATA)
        << "the empty-string arguments reached the driver as null pointers, so "
           "this was an ordinary table query rather than SQL_ALL_CATALOGS";
}

// ── SQLGetCursorNameW round-trips through SQLSetCursorNameW ──────────────

TEST_F(UnicodeWrapperTest, CursorNameRoundTripsThroughTheWidePath) {
    auto name = Wide("WIDE_CURSOR");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetCursorNameW(hstmt, name.data(), SQL_NTS)));
    SQLWCHAR out[64] = {0};
    SQLSMALLINT chars = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetCursorNameW(hstmt, out, 64, &chars)));
    EXPECT_EQ(Narrow(out, 64), "WIDE_CURSOR");
}

}  // namespace
