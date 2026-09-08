// How far FailOn reaches — IMPROVEMENT_PLAN.md D36
//
// `should_fail` was consulted in 18 of the mock's 65 non-wrapper entry points,
// which is the root cause behind D33/D34/D35 individually: the recurring
// reason a broken probe shipped is that the fixture could not make it fail.
// A probe with no configuration that can produce a failure cannot be shown to
// detect one.
//
// This walks the entry points that had no lever and asserts each one now
// answers SQL_ERROR when named. It is deliberately a list rather than a loop
// over "everything": handle allocation and freeing are excluded by name,
// because a FailOn on SQLAllocHandle or SQLFreeHandle breaks the harness
// before a probe runs.
//
// BehaviorController is process-global, so each case connects with its own
// FailOn and the fixture restores Mode=Success afterwards.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <functional>
#include <vector>
#include <string>

namespace {

class FaultReachTest : public ::testing::Test {
protected:
    void TearDown() override {
        Close();
        // Put the process-global behaviour back for whatever runs next.
        Open("");
        Close();
    }

    // Connects with `FailOn=<fn>` (or plain Success when `fn` is empty) and
    // leaves a statement handle ready.
    void Open(const std::string& fn) {
        Close();
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        if (!fn.empty()) conn += "FailOn=" + fn + ";ErrorCode=HY000;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn.c_str(), SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
    }

    void Close() {
        if (hstmt) { SQLFreeHandle(SQL_HANDLE_STMT, hstmt); hstmt = SQL_NULL_HSTMT; }
        if (hdbc) { SQLDisconnect(hdbc); SQLFreeHandle(SQL_HANDLE_DBC, hdbc); hdbc = SQL_NULL_HDBC; }
        if (henv) { SQLFreeHandle(SQL_HANDLE_ENV, henv); henv = SQL_NULL_HENV; }
    }

    // A statement with an open result set, so the metadata calls have
    // something to describe.
    void OpenResultSet() {
        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)"SELECT 1", SQL_NTS)));
    }

    SQLHDESC Ard() {
        SQLHDESC ard = SQL_NULL_HDESC;
        SQLINTEGER len = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
            hstmt, SQL_ATTR_APP_ROW_DESC, &ard, 0, &len)));
        return ard;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── the statement metadata and cursor calls ──────────────────────────────

TEST_F(FaultReachTest, StatementCallsCanBeMadeToFail) {
    struct Case { const char* fn; };
    static const Case kCases[] = {
        {"SQLNumResultCols"}, {"SQLDescribeCol"}, {"SQLRowCount"},
        {"SQLCloseCursor"},   {"SQLMoreResults"}, {"SQLNumParams"},
        {"SQLBindCol"},       {"SQLGetStmtAttr"}, {"SQLSetStmtAttr"},
    };
    for (const auto& c : kCases) {
        Open(c.fn);
        OpenResultSet();

        SQLRETURN rc = SQL_SUCCESS;
        const std::string fn = c.fn;
        if (fn == "SQLNumResultCols") {
            SQLSMALLINT n = 0;
            rc = SQLNumResultCols(hstmt, &n);
        } else if (fn == "SQLDescribeCol") {
            char name[64] = {0};
            SQLSMALLINT nl = 0, t = 0, sc = 0, nu = 0;
            SQLULEN sz = 0;
            rc = SQLDescribeCol(hstmt, 1, (SQLCHAR*)name, sizeof(name), &nl,
                                &t, &sz, &sc, &nu);
        } else if (fn == "SQLRowCount") {
            SQLLEN n = 0;
            rc = SQLRowCount(hstmt, &n);
        } else if (fn == "SQLCloseCursor") {
            rc = SQLCloseCursor(hstmt);
        } else if (fn == "SQLMoreResults") {
            rc = SQLMoreResults(hstmt);
        } else if (fn == "SQLNumParams") {
            SQLSMALLINT n = 0;
            rc = SQLNumParams(hstmt, &n);
        } else if (fn == "SQLBindCol") {
            SQLINTEGER v = 0;
            SQLLEN ind = 0;
            rc = SQLBindCol(hstmt, 1, SQL_C_SLONG, &v, 0, &ind);
        } else if (fn == "SQLGetStmtAttr") {
            SQLULEN v = 0;
            SQLINTEGER len = 0;
            rc = SQLGetStmtAttr(hstmt, SQL_ATTR_CURSOR_TYPE, &v, sizeof(v), &len);
        } else if (fn == "SQLSetStmtAttr") {
            rc = SQLSetStmtAttr(hstmt, SQL_ATTR_NOSCAN,
                                (SQLPOINTER)SQL_NOSCAN_ON, 0);
        }
        EXPECT_EQ(rc, SQL_ERROR) << c.fn << " ignored FailOn";
        Close();
    }
}

// ── the descriptor API, which had no lever at all ────────────────────────

TEST_F(FaultReachTest, DescriptorCallsCanBeMadeToFail) {
    Open("SQLSetDescField");
    SQLHDESC ard = Ard();
    EXPECT_EQ(SQLSetDescField(ard, 1, SQL_DESC_TYPE,
                              (SQLPOINTER)(SQLLEN)SQL_C_SLONG, 0), SQL_ERROR);
    Close();

    Open("SQLGetDescField");
    ard = Ard();
    SQLSMALLINT t = 0;
    SQLINTEGER len = 0;
    EXPECT_EQ(SQLGetDescField(ard, 1, SQL_DESC_TYPE, &t, 0, &len), SQL_ERROR);
    Close();

    Open("SQLColAttribute");
    OpenResultSet();
    SQLLEN n = 0;
    EXPECT_EQ(SQLColAttribute(hstmt, 1, SQL_DESC_COUNT, NULL, 0, NULL, &n),
              SQL_ERROR);
    Close();
}

// ── the diagnostic readers ───────────────────────────────────────────────
//
// A23 and the error-queue probes had no way to be proven wrong without this.
// A reader that fails must not post a diagnostic of its own, so the check is
// deliberately silent.

TEST_F(FaultReachTest, DiagnosticReadersCanBeMadeToFail) {
    Open("SQLGetDiagRec");
    // Force a real diagnostic first, so there is something to read.
    SQLExecDirect(hstmt, (SQLCHAR*)"SELECT * FROM NO_SUCH_TABLE_HERE", SQL_NTS);
    SQLCHAR state[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT len = 0;
    EXPECT_EQ(SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state, &native,
                            msg, sizeof(msg), &len), SQL_ERROR);
    Close();

    Open("SQLGetDiagField");
    SQLExecDirect(hstmt, (SQLCHAR*)"SELECT * FROM NO_SUCH_TABLE_HERE", SQL_NTS);
    SQLINTEGER count = 0;
    SQLSMALLINT flen = 0;
    EXPECT_EQ(SQLGetDiagField(SQL_HANDLE_STMT, hstmt, 0, SQL_DIAG_NUMBER,
                              &count, 0, &flen), SQL_ERROR);
    Close();
}

// ── the connection and info calls ────────────────────────────────────────

TEST_F(FaultReachTest, ConnectionAndInfoCallsCanBeMadeToFail) {
    Open("SQLGetInfo");
    char buf[64] = {0};
    SQLSMALLINT len = 0;
    EXPECT_EQ(SQLGetInfo(hdbc, SQL_DBMS_NAME, buf, sizeof(buf), &len), SQL_ERROR);
    Close();

    Open("SQLGetFunctions");
    SQLUSMALLINT supported = 0;
    EXPECT_EQ(SQLGetFunctions(hdbc, SQL_API_SQLFETCH, &supported), SQL_ERROR);
    Close();

    Open("SQLSetConnectAttr");
    EXPECT_EQ(SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0), SQL_ERROR);
    Close();

    Open("SQLNativeSql");
    char out[128] = {0};
    SQLINTEGER out_len = 0;
    EXPECT_EQ(SQLNativeSql(hdbc, (SQLCHAR*)"SELECT 1", SQL_NTS,
                           (SQLCHAR*)out, sizeof(out), &out_len), SQL_ERROR);
    Close();
}

// ── the cursor-name pair ─────────────────────────────────────────────────

// ── the cursor-name pair ─────────────────────────────────────────────────
//
// SQLSetCursorName cannot be asserted through the Windows driver manager:
// instrumented and confirmed, the driver *is* reached and *does* return
// SQL_ERROR, and the application still sees SQL_SUCCESS, because the DM
// maintains cursor names itself. That is the A23 class - a fact about the
// stack rather than the driver - so the guard stays for unixODBC and the
// assertion does not. SQLGetCursorName does surface, so it is asserted.
TEST_F(FaultReachTest, GetCursorNameCanBeMadeToFail) {
    Open("SQLGetCursorName");
    char name[64] = {0};
    SQLSMALLINT len = 0;
    EXPECT_EQ(SQLGetCursorName(hstmt, (SQLCHAR*)name, sizeof(name), &len),
              SQL_ERROR);
    Close();
}

// And the guard against over-correcting: a FailOn that names one function
// must not break the others.
TEST_F(FaultReachTest, NamingOneFunctionDoesNotFailTheRest) {
    Open("SQLRowCount");
    OpenResultSet();
    SQLSMALLINT cols = 0;
    EXPECT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols)))
        << "FailOn=SQLRowCount also failed SQLNumResultCols";
    SQLLEN n = 0;
    EXPECT_EQ(SQLRowCount(hstmt, &n), SQL_ERROR);
    Close();
}

}  // namespace


// ── D78: failing a W entry point without failing its ANSI implementation ──
//
// Every `should_fail` call site names an ANSI function, because the 31 W
// wrappers convert their arguments and delegate. So `FailOn=SQLPrepare` failed
// both widths and nothing could fail `SQLPrepareW` alone.
//
// These tests are possible only in this binary. It links mock_core directly and
// has no odbc32 import, so `SQLPrepareW` and `SQLPrepare` both resolve to the
// driver's own definitions and the two can be called independently. Through a
// driver manager they cannot: a Unicode-only driver receives both widths as W,
// which is why D78's row records that the tool's own W-then-ANSI fallbacks
// still cannot be exercised end to end.

namespace {

// A wide copy of a narrow string. Same shape as the helper in
// test_unicode_wrappers.cpp - a `L"..."` literal will not do, because SQLWCHAR
// is two bytes on every platform and wchar_t is four on Linux.
std::vector<SQLWCHAR> Wide(const std::string& s) {
    std::vector<SQLWCHAR> out(s.size() + 1, 0);
    for (size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<SQLWCHAR>(static_cast<unsigned char>(s[i]));
    }
    return out;
}

// One entry per wrapper family: the W call and the ANSI call behind it.
struct WidthPair {
    const char* w_name;
    std::function<SQLRETURN(SQLHDBC, SQLHSTMT)> call_w;
    std::function<SQLRETURN(SQLHDBC, SQLHSTMT)> call_ansi;
};

std::vector<WidthPair> width_pairs() {
    return {
        {"SQLPrepareW",
         [](SQLHDBC, SQLHSTMT s) {
             auto w = Wide("SELECT 1");
             return SQLPrepareW(s, w.data(), SQL_NTS);
         },
         [](SQLHDBC, SQLHSTMT s) {
             return SQLPrepare(s, (SQLCHAR*)"SELECT 1", SQL_NTS);
         }},
        {"SQLExecDirectW",
         [](SQLHDBC, SQLHSTMT s) {
             SQLFreeStmt(s, SQL_CLOSE);
             auto w = Wide("SELECT 1");
             return SQLExecDirectW(s, w.data(), SQL_NTS);
         },
         [](SQLHDBC, SQLHSTMT s) {
             SQLFreeStmt(s, SQL_CLOSE);
             return SQLExecDirect(s, (SQLCHAR*)"SELECT 1", SQL_NTS);
         }},
        {"SQLGetInfoW",
         [](SQLHDBC c, SQLHSTMT) {
             SQLWCHAR buf[128] = {0};
             SQLSMALLINT len = 0;
             return SQLGetInfoW(c, SQL_DBMS_NAME, buf,
                                static_cast<SQLSMALLINT>(sizeof(buf)), &len);
         },
         [](SQLHDBC c, SQLHSTMT) {
             SQLCHAR buf[128] = {0};
             SQLSMALLINT len = 0;
             return SQLGetInfo(c, SQL_DBMS_NAME, buf,
                               static_cast<SQLSMALLINT>(sizeof(buf)), &len);
         }},
        {"SQLTablesW",
         [](SQLHDBC, SQLHSTMT s) {
             SQLFreeStmt(s, SQL_CLOSE);
             return SQLTablesW(s, nullptr, 0, nullptr, 0, nullptr, 0,
                               nullptr, 0);
         },
         [](SQLHDBC, SQLHSTMT s) {
             SQLFreeStmt(s, SQL_CLOSE);
             return SQLTables(s, nullptr, 0, nullptr, 0, nullptr, 0,
                              nullptr, 0);
         }},
        {"SQLNativeSqlW",
         [](SQLHDBC c, SQLHSTMT) {
             auto w = Wide("SELECT 1");
             SQLWCHAR out[256] = {0};
             SQLINTEGER out_len = 0;
             return SQLNativeSqlW(c, w.data(), SQL_NTS, out, 256, &out_len);
         },
         [](SQLHDBC c, SQLHSTMT) {
             SQLCHAR out[256] = {0};
             SQLINTEGER out_len = 0;
             return SQLNativeSql(c, (SQLCHAR*)"SELECT 1", SQL_NTS,
                                 out, 256, &out_len);
         }},
    };
}

}  // namespace

TEST_F(FaultReachTest, AWEntryPointCanFailWhileItsAnsiImplementationSucceeds) {
    for (const auto& p : width_pairs()) {
        Open(p.w_name);
        EXPECT_EQ(p.call_w(hdbc, hstmt), SQL_ERROR)
            << "FailOn=" << p.w_name << " did not reach the W entry point";
        EXPECT_TRUE(SQL_SUCCEEDED(p.call_ansi(hdbc, hstmt)))
            << "FailOn=" << p.w_name << " also failed the ANSI implementation, "
               "which is the one thing D78 exists to separate";
        Close();
    }
}

// The backward-compatibility pin, as a test rather than as prose. Every e2e
// FailOn scenario names an ANSI function and reaches it *through* a W wrapper
// via the driver manager; if an ANSI name stopped matching a W-arriving call,
// that whole suite would die quietly.
TEST_F(FaultReachTest, AnAnsiFailOnStillFailsBothWidths) {
    Open("SQLPrepare");
    auto w = Wide("SELECT 1");
    EXPECT_EQ(SQLPrepareW(hstmt, w.data(), SQL_NTS), SQL_ERROR)
        << "an ANSI FailOn must still fail calls arriving through the W entry "
           "point - every e2e scenario depends on it";
    EXPECT_EQ(SQLPrepare(hstmt, (SQLCHAR*)"SELECT 1", SQL_NTS), SQL_ERROR);
}

// A marker, not a latch: pushed and popped per call, so the next W call fails
// again and the ANSI one in between does not.
TEST_F(FaultReachTest, TheWMarkerDoesNotOutliveTheCall) {
    Open("SQLPrepareW");
    auto w = Wide("SELECT 1");
    EXPECT_EQ(SQLPrepareW(hstmt, w.data(), SQL_NTS), SQL_ERROR);
    EXPECT_TRUE(SQL_SUCCEEDED(SQLPrepare(hstmt, (SQLCHAR*)"SELECT 1", SQL_NTS)));
    EXPECT_EQ(SQLPrepareW(hstmt, w.data(), SQL_NTS), SQL_ERROR)
        << "the marker did not survive its own scope, or did not come back";
}

// The D36-style over-correction guard: naming one entry point must not break
// the others.
TEST_F(FaultReachTest, NamingAWEntryPointDoesNotFailTheOthers) {
    Open("SQLPrepareW");
    auto w = Wide("SELECT 1");
    EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirectW(hstmt, w.data(), SQL_NTS)))
        << "FailOn=SQLPrepareW broke an unrelated W entry point";
}
