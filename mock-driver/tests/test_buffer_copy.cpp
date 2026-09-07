// The shared copy-with-truncation helper — IMPROVEMENT_PLAN.md D18, D23, D24.
//
// This block was written eight times across the driver and each copy got a
// different subset of the same three decisions right. These tests pin the
// decisions themselves, so a ninth site written against the helper inherits
// them instead of re-deriving them.
#include <gtest/gtest.h>

#include "utils/buffer_copy.hpp"
#include "utils/string_utils.hpp"

// GCC does not pull these in transitively the way MSVC does.
#include <cstring>

#include <string>
#include <vector>

using namespace mock_odbc;

// ── copy_chars ────────────────────────────────────────────────────────────

TEST(BufferCopy, ShortValueFitsAndIsTerminated) {
    char buf[16];
    std::memset(buf, 0x7F, sizeof(buf));

    const auto r = copy_chars("abc", 0, buf, sizeof(buf));

    EXPECT_EQ(r.rc, SQL_SUCCESS);
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.copied, 3u);
    EXPECT_EQ(r.remaining, 3);          // bytes available, not bytes written
    EXPECT_STREQ(buf, "abc");
    EXPECT_EQ(buf[4], 0x7F) << "wrote past the terminator";
}

// The buffer's last byte belongs to the terminator. A 4-byte buffer holds
// three characters, not four — the off-by-one every hand-rolled copy had to
// get right independently.
TEST(BufferCopy, ReservesOneByteForTheTerminator) {
    char buf[4];
    const auto r = copy_chars("abcdef", 0, buf, sizeof(buf));

    EXPECT_EQ(r.rc, SQL_SUCCESS_WITH_INFO);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.copied, 3u);
    EXPECT_STREQ(buf, "abc");
    EXPECT_EQ(r.remaining, 6) << "remaining is what is available, not what fit";
}

// A null pointer or a zero length is how an application asks only for the
// size. It must not be an error, and it must not consume anything.
TEST(BufferCopy, NullTargetReportsTheLengthWithoutWriting) {
    const auto r = copy_chars("abcdef", 0, nullptr, 0);
    EXPECT_EQ(r.rc, SQL_SUCCESS_WITH_INFO);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.copied, 0u);
    EXPECT_EQ(r.remaining, 6);
}

// ...but asking for the length of an empty value is not a warning.
TEST(BufferCopy, ZeroLengthAskForAnEmptyValueIsNotTruncation) {
    const auto r = copy_chars("", 0, nullptr, 0);
    EXPECT_EQ(r.rc, SQL_SUCCESS);
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.remaining, 0);
}

// The offset is what makes chunked SQLGetData work (D7/D37): each call
// continues where the last stopped, and `remaining` counts down.
TEST(BufferCopy, OffsetContinuesWhereTheLastCallStopped) {
    const std::string src = "0123456789";
    char buf[4];

    auto r = copy_chars(src, 0, buf, sizeof(buf));
    EXPECT_STREQ(buf, "012");
    EXPECT_EQ(r.remaining, 10);
    EXPECT_TRUE(r.truncated);

    r = copy_chars(src, 3, buf, sizeof(buf));
    EXPECT_STREQ(buf, "345");
    EXPECT_EQ(r.remaining, 7);

    r = copy_chars(src, 9, buf, sizeof(buf));
    EXPECT_STREQ(buf, "9");
    EXPECT_EQ(r.remaining, 1);
    EXPECT_FALSE(r.truncated) << "the last chunk is not a truncation";
}

TEST(BufferCopy, OffsetPastTheEndReportsNothingLeft) {
    char buf[8];
    const auto r = copy_chars("abc", 99, buf, sizeof(buf));
    EXPECT_EQ(r.rc, SQL_SUCCESS);
    EXPECT_EQ(r.remaining, 0);
    EXPECT_EQ(r.copied, 0u);
    EXPECT_STREQ(buf, "");
}

// ── copy_wchars ───────────────────────────────────────────────────────────

TEST(BufferCopy, WideLengthsAreInBytes) {
    SQLWCHAR buf[8];
    const auto r = copy_wchars("abc", 0, buf, sizeof(buf));

    EXPECT_EQ(r.rc, SQL_SUCCESS);
    EXPECT_EQ(r.copied, 3u);
    EXPECT_EQ(r.remaining, static_cast<SQLLEN>(3 * sizeof(SQLWCHAR)))
        << "SQL_C_WCHAR lengths are byte counts, not character counts";
    EXPECT_EQ(buf[0], 'a');
    EXPECT_EQ(buf[3], 0);
}

// A supplementary codepoint is two UTF-16 units. Splitting the pair across
// two chunks would hand the caller half a character it could never
// reassemble, so a chunk ends on a codepoint boundary even when that wastes
// a unit of the buffer.
TEST(BufferCopy, DoesNotSplitASurrogatePair) {
    const std::string emoji = "\xF0\x9F\x98\x80";   // U+1F600
    const std::string src = "a" + emoji;

    // Room for a terminator plus two units: 'a' and then half the pair.
    SQLWCHAR buf[3];
    const auto r = copy_wchars(src, 0, buf, sizeof(buf));

    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.copied, 1u) << "took 'a' and stopped rather than splitting";
    EXPECT_EQ(buf[0], 'a');
    EXPECT_EQ(buf[1], 0);
    EXPECT_EQ(r.remaining, static_cast<SQLLEN>(3 * sizeof(SQLWCHAR)))
        << "'a' plus a surrogate pair is three UTF-16 units";
}

TEST(BufferCopy, EncodesASurrogatePairWhenItFits) {
    const std::string emoji = "\xF0\x9F\x98\x80";   // U+1F600
    SQLWCHAR buf[4];
    const auto r = copy_wchars(emoji, 0, buf, sizeof(buf));

    EXPECT_FALSE(r.truncated);
    ASSERT_EQ(r.copied, 2u);
    EXPECT_EQ(buf[0], 0xD83D);
    EXPECT_EQ(buf[1], 0xDE00);
    EXPECT_EQ(buf[2], 0);
}

// ── D23: the SQLSMALLINT overflow the old helper got backwards ────────────
//
// copy_string_to_buffer used to compute `SQLSMALLINT src_len = src.length()`.
// At 32768 bytes that wraps negative, so `src_len >= buffer_length` answered
// *false* and a badly truncated value was reported as complete — the one
// case the check exists for.
TEST(BufferCopy, LongValueIsStillReportedAsTruncated) {
    const std::string big(40000, 'x');
    char buf[16];
    SQLSMALLINT reported = -1;

    const SQLRETURN rc = copy_string_to_buffer(
        big, reinterpret_cast<SQLCHAR*>(buf), sizeof(buf), &reported);

    EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO)
        << "40000 bytes into a 16-byte buffer is a truncation";
    EXPECT_GT(reported, 0) << "the reported length must not wrap negative";
    EXPECT_EQ(std::strlen(buf), 15u);
}

// ── D24: truncation is observable, not just returned ──────────────────────
//
// Every path below used to either discard copy_string_to_buffer's
// SQL_SUCCESS_WITH_INFO or hand-roll the copy and signal nothing at all. The
// return code alone is not enough: an application that sees
// SQL_SUCCESS_WITH_INFO and finds an empty diagnostic stack cannot tell
// truncation from any other warning, which is the whole point of 01004.
class TruncationDiagnosticTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn.c_str(), SQL_NTS,
            NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
    }
    void TearDown() override {
        if (hstmt) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc) { SQLDisconnect(hdbc); SQLFreeHandle(SQL_HANDLE_DBC, hdbc); }
        if (henv) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    std::string first_state(SQLSMALLINT type, SQLHANDLE h) {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR msg[256] = {0};
        SQLSMALLINT len = 0;
        if (!SQL_SUCCEEDED(SQLGetDiagRec(type, h, 1, state, &native,
                                         msg, sizeof(msg), &len))) {
            return {};
        }
        return std::string(reinterpret_cast<char*>(state));
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

TEST_F(TruncationDiagnosticTest, DescribeColPostsO1004) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT * FROM CUSTOMERS", SQL_NTS)));

    SQLCHAR name[3] = {0};      // deliberately too small
    SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
    SQLULEN size = 0;
    const SQLRETURN rc = SQLDescribeCol(hstmt, 1, name, sizeof(name),
                                        &name_len, &type, &size, &scale,
                                        &nullable);

    ASSERT_TRUE(SQL_SUCCEEDED(rc));
    EXPECT_EQ(first_state(SQL_HANDLE_STMT, hstmt), "01004")
        << "a truncated column name must be reported, not merely returned";
    SQLCloseCursor(hstmt);
}

TEST_F(TruncationDiagnosticTest, GetCursorNamePostsO1004) {
    SQLCHAR cur[3] = {0};
    SQLSMALLINT cur_len = 0;
    const SQLRETURN rc = SQLGetCursorName(hstmt, cur, sizeof(cur), &cur_len);
    EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO)
        << "a driver manager only surfaces diagnostics when the call says "
           "there are some";
    EXPECT_EQ(first_state(SQL_HANDLE_STMT, hstmt), "01004");
    EXPECT_GT(cur_len, 2) << "the reported length is what is available";
}

TEST_F(TruncationDiagnosticTest, NativeSqlPostsO1004) {
    const char* sql = "SELECT {fn UCASE('abcdefghijklmnop')}";
    SQLCHAR out[4] = {0};
    SQLINTEGER out_len = 0;
    const SQLRETURN rc = SQLNativeSql(hdbc, (SQLCHAR*)sql, SQL_NTS,
                                      out, sizeof(out), &out_len);
    ASSERT_TRUE(SQL_SUCCEEDED(rc));
    EXPECT_EQ(rc, SQL_SUCCESS_WITH_INFO);
    EXPECT_EQ(first_state(SQL_HANDLE_DBC, hdbc), "01004");
}

// D23: the numeric path of SQLGetInfoW used to memcpy without consulting the
// caller's buffer length, writing four bytes into a two-byte variable. Since
// the .def exports only SQLGetInfoW, that is the path a Unicode driver
// manager takes on Windows.
TEST_F(TruncationDiagnosticTest, GetInfoWRefusesAnUndersizedNumericBuffer) {
    struct { SQLUSMALLINT value; SQLUINTEGER guard; } probe{0, 0xAAAAAAAAu};
    SQLSMALLINT len = 0;

    const SQLRETURN rc = SQLGetInfoW(hdbc, SQL_GETDATA_EXTENSIONS,
                                     &probe.value, sizeof(probe.value), &len);

    EXPECT_EQ(rc, SQL_ERROR)
        << "a fixed-size attribute does not fit; half of it is a different "
           "number, not a smaller one";
    EXPECT_EQ(first_state(SQL_HANDLE_DBC, hdbc), "HY090");
    EXPECT_EQ(probe.guard, 0xAAAAAAAAu) << "wrote past the caller's variable";
}
