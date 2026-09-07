// Fault-injection knob tests — IMPROVEMENT_PLAN.md D33 and D34.
//
// The mock is odbc-crusher's reference fixture: a probe that cannot be made to
// fail against it has never been shown to work. Two gaps in that fixture are
// covered here.
//
// D33 — BufferValidation was parsed into DriverConfig and never read by
// anything, so the knob README.md documents did nothing at all.
//
// D34 — every SilentCorruption mode targeted stored rows or the character
// fetch path, so nothing could make a *numeric* value come back wrong. That is
// why a probe asserting `SELECT 42` returns 42 could ship with `||` where `&&`
// was meant, and why a bound-vs-SQLGetData comparison could ship without the
// comparison, and nothing noticed.
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <cstring>
#include <string>

namespace {

class FaultInjectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                reinterpret_cast<SQLPOINTER>(
                                    static_cast<intptr_t>(SQL_OV_ODBC3)), 0),
                  SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    void Connect(const std::string& extra) {
        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;" + extra;
        SQLRETURN ret = SQLDriverConnect(
            hdbc, nullptr,
            reinterpret_cast<SQLCHAR*>(const_cast<char*>(conn.c_str())), SQL_NTS,
            nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

}  // namespace

// ── D33: BufferValidation ──────────────────────────────────────────────────

TEST_F(FaultInjectionTest, StrictBufferValidationNullTerminatesGetInfoStrings) {
    Connect("");
    char buf[256];
    std::memset(buf, 'X', sizeof(buf));
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfo(hdbc, SQL_DRIVER_NAME, buf,
                                         sizeof(buf), &len)));

    const void* nul = std::memchr(buf, '\0', sizeof(buf));
    ASSERT_NE(nul, nullptr) << "Strict must null-terminate";
    EXPECT_EQ(static_cast<const char*>(nul) - buf, len)
        << "and the reported length must match where the NUL landed";
}

TEST_F(FaultInjectionTest, LenientBufferValidationOmitsTheGetInfoTerminator) {
    Connect("BufferValidation=Lenient;");
    char buf[256];
    std::memset(buf, 'X', sizeof(buf));
    SQLSMALLINT len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetInfo(hdbc, SQL_DRIVER_NAME, buf,
                                         sizeof(buf), &len)));

    EXPECT_EQ(std::memchr(buf, '\0', sizeof(buf)), nullptr)
        << "Lenient must leave the string unterminated — that is the whole "
           "point of the knob";
    // The value itself must still be there, or the probe under test would be
    // failing for the wrong reason. SQL_DRIVER_NAME is the driver's *file*
    // name per the spec, not its friendly name.
    EXPECT_GT(len, 0);
    EXPECT_EQ(std::string(buf, static_cast<size_t>(len)), "mockodbc.dll");
}

// ── D34: numeric skew ──────────────────────────────────────────────────────

namespace {

// Read `SELECT 42` back through a bound column and through SQLGetData.
struct BothPaths {
    SQLINTEGER bound = 0;
    SQLINTEGER getdata = 0;
};

}  // namespace

TEST_F(FaultInjectionTest, NoSkewByDefaultOnEitherPath) {
    Connect("");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 42")),
              SQL_NTS), SQL_SUCCESS);

    BothPaths v;
    SQLLEN ind = 0;
    ASSERT_EQ(SQLBindCol(hstmt, 1, SQL_C_SLONG, &v.bound, sizeof(v.bound), &ind),
              SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 1, SQL_C_SLONG, &v.getdata,
                                         sizeof(v.getdata), &ind)));

    EXPECT_EQ(v.bound, 42);
    EXPECT_EQ(v.getdata, 42);
}

TEST_F(FaultInjectionTest, SkewNumericPerturbsBothPathsIdentically) {
    Connect("SilentCorruption=SkewNumeric;");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 42")),
              SQL_NTS), SQL_SUCCESS);

    BothPaths v;
    SQLLEN ind = 0;
    ASSERT_EQ(SQLBindCol(hstmt, 1, SQL_C_SLONG, &v.bound, sizeof(v.bound), &ind),
              SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 1, SQL_C_SLONG, &v.getdata,
                                         sizeof(v.getdata), &ind)));

    EXPECT_EQ(v.bound, 43) << "bound column must be skewed";
    EXPECT_EQ(v.getdata, 43) << "and SQLGetData must be skewed the same way";
}

// The distinguishing case: a driver whose two delivery paths disagree.
TEST_F(FaultInjectionTest, SkewNumericBoundMakesTheTwoPathsDisagree) {
    Connect("SilentCorruption=SkewNumericBound;");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 42")),
              SQL_NTS), SQL_SUCCESS);

    BothPaths v;
    SQLLEN ind = 0;
    ASSERT_EQ(SQLBindCol(hstmt, 1, SQL_C_SLONG, &v.bound, sizeof(v.bound), &ind),
              SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 1, SQL_C_SLONG, &v.getdata,
                                         sizeof(v.getdata), &ind)));

    EXPECT_EQ(v.bound, 43) << "bound column must be skewed";
    EXPECT_EQ(v.getdata, 42) << "SQLGetData must NOT be — that is the point";
}

// The skew must not leak into modes that have nothing to do with it, or every
// existing SilentCorruption scenario would start reporting numeric damage.
TEST_F(FaultInjectionTest, OtherCorruptionModesLeaveNumericsAlone) {
    Connect("SilentCorruption=MangleVarchar;");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 42")),
              SQL_NTS), SQL_SUCCESS);

    SQLINTEGER value = 0;
    SQLLEN ind = 0;
    ASSERT_EQ(SQLBindCol(hstmt, 1, SQL_C_SLONG, &value, sizeof(value), &ind),
              SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    EXPECT_EQ(value, 42);
}
