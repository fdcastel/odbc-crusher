// SilentCorruption on the array-parameter path — IMPROVEMENT_PLAN.md D46
//
// Under `SilentCorruption=MangleVarchar` the mock appends `X` to every stored
// character-column value, and 14 probes fail as a result — including every
// numeric-to-VARCHAR round-trip, which inserts through a prepared
// `INSERT INTO t (ID, VAL) VALUES (?, ?)`. A20's array probe inserts through
// the *same* statement shape with SQL_ATTR_PARAMSET_SIZE = 3 and its strings
// came back unmangled, so the array path had no fault-injection lever at all —
// nothing could catch a driver that inserts three rows with the first string
// repeated.
//
// BehaviorController is process-global, so each test here restores
// Mode=Success before returning.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Session {
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;

    explicit Session(const char* conn) {
        SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
        SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
        SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn, SQL_NTS, NULL, 0, NULL,
                         SQL_DRIVER_NOPROMPT);
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    }
    ~Session() {
        if (hstmt) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc) { SQLDisconnect(hdbc); SQLFreeHandle(SQL_HANDLE_DBC, hdbc); }
        if (henv) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Exec(const char* sql) {
        SQLExecDirect(hstmt, (SQLCHAR*)sql, SQL_NTS);
        SQLCloseCursor(hstmt);
    }

    std::vector<std::string> ReadColumn(const char* sql, SQLUSMALLINT col) {
        std::vector<std::string> out;
        if (!SQL_SUCCEEDED(SQLExecDirect(hstmt, (SQLCHAR*)sql, SQL_NTS))) return out;
        while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
            char buf[128] = {0};
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(hstmt, col, SQL_C_CHAR, buf, sizeof(buf), &ind))
                && ind != SQL_NULL_DATA) {
                out.push_back(buf);
            }
        }
        SQLCloseCursor(hstmt);
        return out;
    }
};

const char* kMangle =
    "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
    "SilentCorruption=MangleVarchar;";
const char* kClean = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";

// Put the process-global behaviour back for whatever runs next.
void RestoreCleanBehaviour() { Session restore(kClean); }

// The single-row path: this one always worked, and is here so a fix that
// broke it would be caught.
TEST(ArrayCorruption, SingleRowInsertIsMangled) {
    {
        Session s(kMangle);
        s.Exec("CREATE TABLE D46_ONE (ID INTEGER, NAME VARCHAR(32))");
        ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
            s.hstmt, (SQLCHAR*)"INSERT INTO D46_ONE (ID, NAME) VALUES (?, ?)",
            SQL_NTS)));
        SQLINTEGER id = 1;
        char name[32] = "Alice";
        SQLLEN id_ind = 0, name_ind = SQL_NTS;
        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
            &id, 0, &id_ind)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 32, 0,
            name, sizeof(name), &name_ind)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(s.hstmt)));
        SQLCloseCursor(s.hstmt);

        const auto got = s.ReadColumn("SELECT NAME FROM D46_ONE", 1);
        ASSERT_EQ(got.size(), 1u);
        EXPECT_EQ(got[0], "AliceX") << "MangleVarchar missed the single-row path";
    }
    RestoreCleanBehaviour();
}

// The array path: the same statement shape with SQL_ATTR_PARAMSET_SIZE = 3.
TEST(ArrayCorruption, ArrayInsertIsMangledToo) {
    {
        Session s(kMangle);
        s.Exec("CREATE TABLE D46_ARR (ID INTEGER, NAME VARCHAR(32))");
        ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
            s.hstmt, (SQLCHAR*)"INSERT INTO D46_ARR (ID, NAME) VALUES (?, ?)",
            SQL_NTS)));

        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
            s.hstmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)3, 0)));

        SQLINTEGER ids[3] = {100, 200, 300};
        char names[3][32] = {"Alice", "Bob", "Charlie"};
        SQLLEN id_ind[3] = {0, 0, 0};
        SQLLEN name_ind[3] = {SQL_NTS, SQL_NTS, SQL_NTS};

        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
            ids, 0, id_ind)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 32, 0,
            names, 32, name_ind)));

        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(s.hstmt)));
        SQLCloseCursor(s.hstmt);
        SQLSetStmtAttr(s.hstmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)1, 0);

        const auto got = s.ReadColumn("SELECT NAME FROM D46_ARR ORDER BY ID", 1);
        ASSERT_EQ(got.size(), 3u) << "the array insert did not store three rows";
        EXPECT_EQ(got[0], "AliceX")
            << "MangleVarchar does not reach the array-parameter insert path, "
               "so nothing can catch a driver that mishandles it";
        EXPECT_EQ(got[1], "BobX");
        EXPECT_EQ(got[2], "CharlieX");
    }
    RestoreCleanBehaviour();
}

// And the rows must be distinct: a driver that repeats the first string for
// every parameter set is exactly what this lever exists to catch.
TEST(ArrayCorruption, ArrayInsertKeepsEachParameterSetDistinct) {
    {
        Session s(kClean);
        s.Exec("CREATE TABLE D46_DISTINCT (ID INTEGER, NAME VARCHAR(32))");
        ASSERT_TRUE(SQL_SUCCEEDED(SQLPrepare(
            s.hstmt,
            (SQLCHAR*)"INSERT INTO D46_DISTINCT (ID, NAME) VALUES (?, ?)",
            SQL_NTS)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
            s.hstmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)3, 0)));

        SQLINTEGER ids[3] = {1, 2, 3};
        char names[3][32] = {"Alice", "Bob", "Charlie"};
        SQLLEN id_ind[3] = {0, 0, 0};
        SQLLEN name_ind[3] = {SQL_NTS, SQL_NTS, SQL_NTS};
        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
            ids, 0, id_ind)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLBindParameter(
            s.hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 32, 0,
            names, 32, name_ind)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecute(s.hstmt)));
        SQLCloseCursor(s.hstmt);
        SQLSetStmtAttr(s.hstmt, SQL_ATTR_PARAMSET_SIZE, (SQLPOINTER)1, 0);

        const auto got = s.ReadColumn("SELECT NAME FROM D46_DISTINCT ORDER BY ID", 1);
        ASSERT_EQ(got.size(), 3u);
        EXPECT_EQ(got[0], "Alice");
        EXPECT_EQ(got[1], "Bob");
        EXPECT_EQ(got[2], "Charlie");
    }
    RestoreCleanBehaviour();
}

}  // namespace
