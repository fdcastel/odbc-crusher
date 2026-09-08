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
#include <vector>
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
            // D67: the driver's configuration is process-global, so a test
            // that connects with BufferValidation=Lenient leaves every test
            // after it running against a driver that drops its terminators.
            // Nothing noticed while D33 confined the knob to SQLGetInfo;
            // D62 broadened it to every string copy and BufferCopyLenient.
            // StrictIsUnchanged failed on the leak within the hour.
            // FaultReachTest already did this - the same reconnect, for the
            // same reason.
            SQLDisconnect(hdbc);
            const char* clean =
                "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
            SQLDriverConnect(hdbc, nullptr,
                             reinterpret_cast<SQLCHAR*>(const_cast<char*>(clean)),
                             SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
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

// ── Conversions and diagnostics found by Phase 2 probes ────────────────────
//
// Both of these are mock defects that the A21 probe fixes exposed: the probes
// had been passing unconditionally, so nothing had ever looked.

// SQL_CHAR -> SQL_C_SLONG is a *required Core* conversion. The mock used to
// fall through to its "return ANSI" branch and memcpy the raw digits into the
// caller's 4-byte SQLINTEGER: SELECT '123' read back as 3355185, which is
// 0x333231 — the ASCII bytes '1','2','3' little-endian.
TEST_F(FaultInjectionTest, CharacterCellConvertsToSignedLong) {
    Connect("");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT '123'")),
              SQL_NTS), SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);

    SQLINTEGER value = 0;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 1, SQL_C_SLONG, &value,
                                         sizeof(value), &ind)));
    EXPECT_EQ(value, 123);
    EXPECT_EQ(ind, static_cast<SQLLEN>(sizeof(SQLINTEGER)));
}

TEST_F(FaultInjectionTest, CharacterCellConvertsToDouble) {
    Connect("");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT '2.5'")),
              SQL_NTS), SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);

    SQLDOUBLE value = 0.0;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(hstmt, 1, SQL_C_DOUBLE, &value,
                                         sizeof(value), &ind)));
    EXPECT_DOUBLE_EQ(value, 2.5);
}

// A value that genuinely cannot be cast must be an error with 22018 — not a
// wrong number, which is what silently reinterpreting the bytes produced.
TEST_F(FaultInjectionTest, UnconvertibleCharacterCellReports22018) {
    Connect("");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 'abc'")),
              SQL_NTS), SQL_SUCCESS);
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);

    SQLINTEGER value = 0;
    SQLLEN ind = 0;
    EXPECT_EQ(SQLGetData(hstmt, 1, SQL_C_SLONG, &value, sizeof(value), &ind),
              SQL_ERROR);

    SQLCHAR state[6] = {0};
    SQLSMALLINT msg_len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state,
                                            nullptr, nullptr, 0, &msg_len)));
    EXPECT_STREQ(reinterpret_cast<const char*>(state), "22018");
}

// Truncating a SQLGetInfo string used to return SQL_SUCCESS_WITH_INFO with an
// empty diagnostic stack, so an application could not tell truncation from any
// other warning. This is the SQLGetInfo corner of D24.
TEST_F(FaultInjectionTest, TruncatedGetInfoPosts01004) {
    Connect("");
    char tiny[4] = {0};
    SQLSMALLINT len = 0;
    SQLRETURN ret = SQLGetInfo(hdbc, SQL_DRIVER_NAME, tiny, sizeof(tiny), &len);
    ASSERT_EQ(ret, SQL_SUCCESS_WITH_INFO) << "the value is longer than 4 bytes";
    EXPECT_GT(len, static_cast<SQLSMALLINT>(sizeof(tiny)))
        << "the reported length is the total available, not what fit";

    SQLCHAR state[6] = {0};
    SQLSMALLINT msg_len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state,
                                            nullptr, nullptr, 0, &msg_len)))
        << "truncation must leave a diagnostic behind";
    EXPECT_STREQ(reinterpret_cast<const char*>(state), "01004");
}

// SupportsArrayBind=false must decline the two array-parameter output pointers
// as well as PARAMSET_SIZE > 1 — a driver with no array execution has no use
// for a per-row status array. This is what lets A8's SKIP branch be exercised.
TEST_F(FaultInjectionTest, SupportsArrayBindFalseDeclinesTheOutputPointers) {
    Connect("SupportsArrayBind=false;");
    SQLUSMALLINT status = 0xFFFF;
    SQLULEN processed = 0;
    EXPECT_EQ(SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_STATUS_PTR, &status, 0),
              SQL_ERROR);
    EXPECT_EQ(SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0),
              SQL_ERROR);
}

TEST_F(FaultInjectionTest, ArrayOutputPointersAreAcceptedByDefault) {
    Connect("");
    SQLUSMALLINT status = 0xFFFF;
    SQLULEN processed = 0;
    EXPECT_EQ(SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_STATUS_PTR, &status, 0),
              SQL_SUCCESS);
    EXPECT_EQ(SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0),
              SQL_SUCCESS);
}

// D35 — a driver that warns on every row it returns. Without this there was no
// way to tell a fetch loop written `== SQL_SUCCESS` from one written with
// SQL_SUCCEEDED, which is why 14 of them in the probe suite had the former.
TEST_F(FaultInjectionTest, FetchReturnsWarningWarnsOnEveryRowButStillReturnsThem) {
    Connect("FetchReturnsWarning=true;ResultSetSize=3;");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT * FROM USERS")),
              SQL_NTS), SQL_SUCCESS);

    int rows = 0;
    SQLRETURN ret;
    while (SQL_SUCCEEDED(ret = SQLFetch(hstmt))) {
        EXPECT_EQ(ret, SQL_SUCCESS_WITH_INFO) << "every row must carry the warning";
        SQLCHAR state[6] = {0};
        SQLSMALLINT msg_len = 0;
        ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state,
                                                nullptr, nullptr, 0, &msg_len)));
        EXPECT_STREQ(reinterpret_cast<const char*>(state), "01004");
        ++rows;
        if (rows > 10) break;   // guard against a runaway loop in the mock
    }

    EXPECT_EQ(ret, SQL_NO_DATA) << "the cursor must still end properly";
    EXPECT_EQ(rows, 3) << "every row must still be delivered";
}

TEST_F(FaultInjectionTest, FetchIsQuietByDefault) {
    Connect("ResultSetSize=2;");
    ASSERT_EQ(SQLExecDirect(hstmt,
              reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT * FROM USERS")),
              SQL_NTS), SQL_SUCCESS);
    EXPECT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
}


// ── D62: how far the Lenient injection reaches ────────────────────────────
//
// D33 put the terminator-drop on SQLGetInfo alone, so the only crusher buffers
// the fixture could expose were the ones SQLGetInfo filled - which is why
// D58/D60/D61/D63/D64 each found their sites by hand and D61 claimed a
// complete set twice before it was one. The injection now sits in copy_chars,
// the copy every string return passes through, so SQLGetData, SQLDescribeCol,
// SQLColAttribute, SQLGetCursorName, SQLNativeSql and SQLGetDiagRec all carry
// it.
//
// **These tests do not go through a driver manager**, which is worth stating
// because the connection strings make it look as though they do. This binary
// links mock_core, so the linker resolves SQLDriverConnect, SQLGetData and the
// rest to the driver's own definitions ahead of any import library; the
// executable has no odbc32 import at all (`mockodbc.dll` appears in it only as
// the string SQL_DRIVER_NAME returns). So `Driver={Mock ODBC Driver}` is
// parsed by the mock itself and every call below reaches it unconverted.
//
// That is what makes the byte-exact assertions here legitimate. D53 is the
// record of what a manager does to an injected fault when there is one in the
// middle - the macOS manager rescans its own zeroed buffer, finds the NUL one
// place later and re-terminates - and that is a question for the e2e suite,
// which runs crusher as a real ODBC application. Here there is nothing
// between the write and the assertion.
//
// It also means the driver's configuration is this process's global state,
// which is why the fixture's TearDown reconnects clean (D67).

namespace {

// Read the first column of the current row into a caller buffer of `c_type`,
// and return the raw bytes - not a std::string, because the whole question is
// what sits past the value.
std::vector<char> fetch_first_column(SQLHSTMT hstmt, SQLSMALLINT c_type,
                                     size_t bytes, SQLLEN* indicator) {
    std::vector<char> buf(bytes, 0x7F);
    *indicator = 0;
    SQLRETURN rc = SQLGetData(hstmt, 1, c_type, buf.data(),
                              static_cast<SQLLEN>(bytes), indicator);
    if (!SQL_SUCCEEDED(rc)) buf.clear();
    return buf;
}

}  // namespace

TEST_F(FaultInjectionTest, LenientDropsTheTerminatorOnSQLGetData) {
    Connect("BufferValidation=Lenient;Catalog=Default;");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, reinterpret_cast<SQLCHAR*>(
            const_cast<char*>("SELECT 'HELLOWORLD'")), SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));

    SQLLEN ind = 0;
    const auto buf = fetch_first_column(hstmt, SQL_C_CHAR, 64, &ind);
    ASSERT_FALSE(buf.empty()) << "SQLGetData failed outright";

    EXPECT_GT(ind, 0) << "the value must still arrive; only its terminator "
                         "is meant to go";
    EXPECT_EQ(std::memchr(buf.data(), 0, buf.size()), nullptr)
        << "nothing sits between this call and the driver, so a NUL here "
           "means D62's injection is not on the SQLGetData path";
    EXPECT_EQ(buf[static_cast<size_t>(ind)], 'X')
        << "the filler must sit exactly where the terminator belonged";
}

TEST_F(FaultInjectionTest, StrictTerminatesSQLGetData) {
    Connect("Catalog=Default;");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        hstmt, reinterpret_cast<SQLCHAR*>(
            const_cast<char*>("SELECT 'HELLOWORLD'")), SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(hstmt)));

    SQLLEN ind = 0;
    const auto buf = fetch_first_column(hstmt, SQL_C_CHAR, 64, &ind);
    ASSERT_FALSE(buf.empty());
    EXPECT_GT(ind, 0);
    EXPECT_EQ(buf[static_cast<size_t>(ind)], '\0')
        << "the default must terminate, or every existing caller changes "
           "meaning";
}

namespace {

// The bytes SQLGetCursorName / SQLDescribeCol / SQLNativeSql leave behind,
// as a hex string, so a difference between two runs can be reported rather
// than merely detected.
std::string hex(const char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        out += d[c >> 4];
        out += d[c & 0xF];
    }
    return out;
}

}  // namespace

// The entry points the .def exports only as W. Compared as whole buffers
// rather than by asserting "no NUL": these three return names short enough
// that where the filler lands is the only difference, and a hex dump of the
// two runs says which byte moved if one of them ever stops carrying the
// injection.
TEST_F(FaultInjectionTest, LenientReachesTheOtherStringEntryPoints) {
    struct Sample {
        std::string cursor;
        std::string column;
        std::string native;
    };

    auto sample = [this](const std::string& extra) {
        Connect(extra + "Catalog=Default;");
        Sample s;

        char cur[64];
        std::memset(cur, 0x7F, sizeof(cur));
        SQLSMALLINT cur_len = 0;
        if (SQL_SUCCEEDED(SQLGetCursorName(
                hstmt, reinterpret_cast<SQLCHAR*>(cur),
                static_cast<SQLSMALLINT>(sizeof(cur)), &cur_len))) {
            s.cursor = hex(cur, sizeof(cur));
        }

        if (SQL_SUCCEEDED(SQLExecDirect(
                hstmt, reinterpret_cast<SQLCHAR*>(
                    const_cast<char*>("SELECT NAME FROM CUSTOMERS")),
                SQL_NTS))) {
            char col[64];
            std::memset(col, 0x7F, sizeof(col));
            SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
            SQLULEN size = 0;
            if (SQL_SUCCEEDED(SQLDescribeCol(
                    hstmt, 1, reinterpret_cast<SQLCHAR*>(col),
                    static_cast<SQLSMALLINT>(sizeof(col)), &name_len, &type,
                    &size, &scale, &nullable))) {
                s.column = hex(col, sizeof(col));
            }
            SQLCloseCursor(hstmt);
        }

        char nat[64];
        std::memset(nat, 0x7F, sizeof(nat));
        SQLINTEGER nat_len = 0;
        if (SQL_SUCCEEDED(SQLNativeSql(
                hdbc, reinterpret_cast<SQLCHAR*>(
                    const_cast<char*>("SELECT {fn UCASE(NAME)} FROM CUSTOMERS")),
                SQL_NTS, reinterpret_cast<SQLCHAR*>(nat),
                static_cast<SQLINTEGER>(sizeof(nat)), &nat_len))) {
            s.native = hex(nat, sizeof(nat));
        }
        return s;
    };

    const Sample strict = sample("");
    // The fixture keeps one connection per test; start over for the second.
    if (hstmt != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        hstmt = SQL_NULL_HSTMT;
    }
    SQLDisconnect(hdbc);
    const Sample lenient = sample("BufferValidation=Lenient;");

    ASSERT_FALSE(strict.cursor.empty()) << "SQLGetCursorName gave nothing to "
                                           "compare";
    EXPECT_NE(strict.cursor, lenient.cursor)
        << "SQLGetCursorName is unchanged by BufferValidation=Lenient, so the "
           "injection is not reaching copy_chars on that path";
    ASSERT_FALSE(strict.column.empty());
    EXPECT_NE(strict.column, lenient.column)
        << "SQLDescribeCol is unchanged by BufferValidation=Lenient";
    ASSERT_FALSE(strict.native.empty());
    EXPECT_NE(strict.native, lenient.native)
        << "SQLNativeSql is unchanged by BufferValidation=Lenient";
}
