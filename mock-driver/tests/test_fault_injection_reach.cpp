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

// ── the cursor-name pair: measured, and not assertable here ──────────────
//
// The guards are in the driver, but neither can be asserted through the
// Windows driver manager. Instrumented and confirmed: with
// FailOn=SQLSetCursorName the driver *is* reached and *does* return
// SQL_ERROR, and the application still sees SQL_SUCCESS - the DM maintains
// cursor names itself and answers both calls without surfacing the driver's
// refusal. That is the A23 class: a fact about the stack, not the driver, so
// there is nothing here a fixture change could make fail. The guards stay,
// because unixODBC forwards these; the assertion does not, because it would
// be asserting about the driver manager.

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
