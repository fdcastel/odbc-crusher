// Exception barrier tests — IMPROVEMENT_PLAN.md D9.
//
// report_entry_exception() is what every SQL_API entry point's
// function-try-block calls when something escapes its body. It runs during
// the unwind of a failure nobody anticipated, across a C ABI, so its contract
// is narrow and absolute: never throw, never crash, always return SQL_ERROR,
// and leave a diagnostic behind when there is a valid handle to leave it on.
//
// The wiring — that every entry point actually has the barrier — is checked
// separately in ci.yml, next to the mockodbc.def drift check, because it is a
// property of the source text rather than of the running driver.
#include <gtest/gtest.h>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include "driver/entry_guard.hpp"
#include "driver/handles.hpp"

#include <string>

using namespace mock_odbc;

namespace {

// Read back the first diagnostic record on a handle as (sqlstate, message).
std::pair<std::string, std::string> first_diag(SQLSMALLINT type, SQLHANDLE h) {
    SQLCHAR state[6] = {0};
    SQLCHAR msg[1024] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT msg_len = 0;
    SQLRETURN ret = SQLGetDiagRec(type, h, 1, state, &native, msg,
                                  static_cast<SQLSMALLINT>(sizeof(msg)), &msg_len);
    if (!SQL_SUCCEEDED(ret)) return {"", ""};
    return {reinterpret_cast<const char*>(state), reinterpret_cast<const char*>(msg)};
}

class EntryGuardTest : public ::testing::Test {
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
        if (hdbc != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
};

}  // namespace

TEST_F(EntryGuardTest, ReturnsSqlErrorAndPostsGeneralErrorForAStdException) {
    SQLRETURN ret = report_entry_exception(hdbc, "SQLSomething",
                                           "vector too long", false);
    EXPECT_EQ(ret, SQL_ERROR);

    auto [state, msg] = first_diag(SQL_HANDLE_DBC, hdbc);
    EXPECT_EQ(state, "HY000");
    EXPECT_NE(msg.find("SQLSomething"), std::string::npos) << msg;
    EXPECT_NE(msg.find("vector too long"), std::string::npos) << msg;
    // The message must not read as an application error — it is a mock bug.
    EXPECT_NE(msg.find("defect in the mock driver"), std::string::npos) << msg;
}

TEST_F(EntryGuardTest, PostsMemoryAllocationErrorForBadAlloc) {
    SQLRETURN ret = report_entry_exception(hdbc, "SQLExecDirect",
                                           "bad allocation", true);
    EXPECT_EQ(ret, SQL_ERROR);

    auto [state, msg] = first_diag(SQL_HANDLE_DBC, hdbc);
    EXPECT_EQ(state, "HY001");
    EXPECT_NE(msg.find("SQLExecDirect"), std::string::npos) << msg;
}

TEST_F(EntryGuardTest, HandlesANonStdExceptionWithNoMessage) {
    SQLRETURN ret = report_entry_exception(hdbc, "SQLFetch", nullptr, false);
    EXPECT_EQ(ret, SQL_ERROR);

    auto [state, msg] = first_diag(SQL_HANDLE_DBC, hdbc);
    EXPECT_EQ(state, "HY000");
    EXPECT_NE(msg.find("non-std::exception"), std::string::npos) << msg;
}

// An entry point can throw before it has a handle to post on — SQLAllocEnv
// takes only an out-parameter. The barrier must still return cleanly.
TEST_F(EntryGuardTest, NullHandleStillReturnsSqlErrorWithoutCrashing) {
    EXPECT_EQ(report_entry_exception(SQL_NULL_HANDLE, "SQLAllocEnv", "boom", false),
              SQL_ERROR);
}

TEST_F(EntryGuardTest, WorksOnAnEnvironmentHandleToo) {
    EXPECT_EQ(report_entry_exception(henv, "SQLDataSources", "boom", false),
              SQL_ERROR);
    auto [state, msg] = first_diag(SQL_HANDLE_ENV, henv);
    EXPECT_EQ(state, "HY000");
}

// The whole point is that it cannot make a bad situation worse.
TEST_F(EntryGuardTest, IsNoexcept) {
    static_assert(noexcept(report_entry_exception(nullptr, "", "", false)),
                  "the barrier must be noexcept — it runs during an unwind");
    SUCCEED();
}

// A freed handle fails is_valid(), so the barrier must skip the diagnostic
// rather than write through a dangling pointer. Exercised with a handle we
// free ourselves: HANDLE_MAGIC is cleared on destruction.
TEST_F(EntryGuardTest, SkipsTheDiagnosticOnAnInvalidHandle) {
    SQLHDBC doomed = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &doomed), SQL_SUCCESS);
    auto* raw = static_cast<OdbcHandle*>(doomed);
    ASSERT_TRUE(raw->is_valid());
    ASSERT_EQ(SQLFreeHandle(SQL_HANDLE_DBC, doomed), SQL_SUCCESS);

    // Still returns SQL_ERROR; must not fault on the freed handle.
    EXPECT_EQ(report_entry_exception(doomed, "SQLDisconnect", "boom", false),
              SQL_ERROR);
}
