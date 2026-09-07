// Handle lifetime and descriptor ownership — IMPROVEMENT_PLAN.md D8.
//
// Three latent memory-corruption paths, none of which any test reached:
//
//   (a) ~EnvironmentHandle and ~ConnectionHandle ranged over their child
//       vector while each child's destructor erased itself from that same
//       vector. Freeing an environment with two or more connections skipped
//       elements and then read freed storage.
//   (b) SQLFreeHandle(SQL_HANDLE_DESC) deleted any valid descriptor, so an
//       application that obtained an implicit one from SQLGetStmtAttr and
//       freed it left the statement's pointer dangling and ~StatementHandle
//       deleted it again.
//
// These run clean under ASan on Linux CI, which is where the first would
// have shown up as a use-after-free rather than as a silent skip.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include "driver/handles.hpp"

#include <string>
#include <vector>

using namespace mock_odbc;

namespace {

const char* kConn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";

} // namespace

// D8(a). The destructors are tested directly rather than through
// SQLFreeHandle, because the driver manager refuses to free an environment
// that still has live connections - so the path where the bug lives is not
// reachable from an application at all. It is reachable when the DM unloads
// the driver, and it was reachable from the driver's own tests, which is
// where this belongs.
//
// Two children is the smallest case that goes wrong: the first `delete`
// erases index 0, the range-for's cursor advances to index 1, and the vector
// now holds one element at index 0. The loop reads past the end and then
// dereferences freed storage.
TEST(HandleLifetime, EnvironmentDestructorHandlesSeveralConnections) {
    auto* env = new EnvironmentHandle();
    for (int i = 0; i < 4; ++i) {
        (void)new ConnectionHandle(env);   // registers itself with env
    }
    ASSERT_EQ(env->connections_.size(), 4u);

    // Under the old destructor this walked a vector shrinking underneath it.
    // ASan on Linux CI is what turns the difference into a failure rather
    // than a coin flip.
    EXPECT_NO_THROW(delete env);
}

TEST(HandleLifetime, ConnectionDestructorHandlesSeveralStatements) {
    auto* env = new EnvironmentHandle();
    auto* conn = new ConnectionHandle(env);
    for (int i = 0; i < 4; ++i) {
        (void)new StatementHandle(conn);
    }
    ASSERT_EQ(conn->statements_.size(), 4u);

    EXPECT_NO_THROW(delete conn);
    EXPECT_TRUE(env->connections_.empty())
        << "the connection removes itself from its environment";
    delete env;
}


// D8(b). An implicit descriptor belongs to the statement. Freeing it is
// HY017, and the statement must still own a live one afterwards.
class DescriptorOwnershipTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
    }
    void TearDown() override {
        if (hstmt) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc) { SQLDisconnect(hdbc); SQLFreeHandle(SQL_HANDLE_DBC, hdbc); }
        if (henv) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

TEST_F(DescriptorOwnershipTest, FreeingAnImplicitDescriptorIsRefused) {
    SQLHDESC ard = SQL_NULL_HDESC;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(hstmt, SQL_ATTR_APP_ROW_DESC,
                                             &ard, 0, nullptr)));
    ASSERT_NE(ard, nullptr);

    // Before D8(b) this returned SQL_SUCCESS and deleted the statement's own
    // descriptor, leaving ~StatementHandle to delete it a second time.
    EXPECT_EQ(SQLFreeHandle(SQL_HANDLE_DESC, ard), SQL_ERROR);

    SQLCHAR state[6] = {0};
    SQLINTEGER native = 0;
    SQLCHAR msg[256] = {0};
    SQLSMALLINT len = 0;
    SQLGetDiagRec(SQL_HANDLE_DESC, ard, 1, state, &native, msg,
                  sizeof(msg), &len);
    EXPECT_STREQ(reinterpret_cast<char*>(state), "HY017");

    // Still usable: it was not freed.
    SQLHDESC again = SQL_NULL_HDESC;
    EXPECT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(hstmt, SQL_ATTR_APP_ROW_DESC,
                                             &again, 0, nullptr)));
    EXPECT_EQ(again, ard);
}

// The other half of the same rule: a descriptor the application allocated is
// its own, and freeing it must work. This is what tells the two apart, and
// it caught that SQLAllocHandle was not marking explicit descriptors as
// user-allocated at all.
TEST_F(DescriptorOwnershipTest, FreeingAnExplicitDescriptorIsAllowed) {
    SQLHDESC mine = SQL_NULL_HDESC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DESC, hdbc, &mine), SQL_SUCCESS);
    ASSERT_NE(mine, nullptr);

    SQLSMALLINT alloc_type = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetDescField(mine, 0, SQL_DESC_ALLOC_TYPE,
                                              &alloc_type, sizeof(alloc_type),
                                              nullptr)));
    EXPECT_EQ(alloc_type, SQL_DESC_ALLOC_USER)
        << "an application-allocated descriptor is USER by definition";

    EXPECT_EQ(SQLFreeHandle(SQL_HANDLE_DESC, mine), SQL_SUCCESS);
}

// ── D6: a second connect must not empty the first one's tables ────────────
//
// Every SQLDriverConnect calls MockCatalog::initialize, which cleared
// tables_, indexes_, inserted_data_ and procedures_ unconditionally — so
// connection B's connect destroyed every table and row connection A had
// created. The tool opens one connection per run, which is the only reason
// this had not bitten; a probe opening a second connection to test isolation
// would have found its own schema gone.
TEST(CatalogSharing, SecondConnectKeepsTheFirstConnectionsTables) {
    SQLHENV henv = SQL_NULL_HENV;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
    ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                            (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);

    SQLHDBC a = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &a), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
        a, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));

    SQLHSTMT sa = SQL_NULL_HSTMT;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, a, &sa), SQL_SUCCESS);
    SQLExecDirect(sa, (SQLCHAR*)"DROP TABLE D6_SHARED", SQL_NTS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        sa, (SQLCHAR*)"CREATE TABLE D6_SHARED (ID INTEGER, V VARCHAR(16))",
        SQL_NTS)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        sa, (SQLCHAR*)"INSERT INTO D6_SHARED (ID, V) VALUES (1, 'a')", SQL_NTS)));
    SQLCloseCursor(sa);

    // A second connection, same preset.
    SQLHDBC b = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &b), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
        b, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));

    // Connection A's table and its row must still be there.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        sa, (SQLCHAR*)"SELECT COUNT(*) FROM D6_SHARED", SQL_NTS)))
        << "the second connect dropped the first connection's table";
    ASSERT_EQ(SQLFetch(sa), SQL_SUCCESS);
    SQLBIGINT n = -1;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(sa, 1, SQL_C_SBIGINT, &n,
                                         sizeof(n), &ind)));
    EXPECT_EQ(n, 1) << "the second connect emptied the first one's rows";
    SQLCloseCursor(sa);

    SQLExecDirect(sa, (SQLCHAR*)"DROP TABLE D6_SHARED", SQL_NTS);
    SQLDisconnect(b);
    SQLFreeHandle(SQL_HANDLE_DBC, b);
    SQLDisconnect(a);
    SQLFreeHandle(SQL_HANDLE_DBC, a);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

// ── D47: the catalog must not outlive the last connection ────────────────
//
// D6 stopped a concurrent connect from wiping a live connection's tables by
// making initialize() a no-op for an already-loaded preset. Nothing then ever
// reset the process-global catalog, so a connection opened after the previous
// one had closed inherited its user-created tables and CREATE TABLE failed
// with "already exists". Ten of the eleven DmlTest cases failed this way when
// the suite ran on Windows, where the driver manager keeps the DLL - and its
// statics - loaded across connections.
TEST(CatalogSharing, ClosingTheLastConnectionResetsTheCatalog) {
    SQLHENV henv = SQL_NULL_HENV;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
    ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                            (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);

    auto create_once = [&]() -> SQLRETURN {
        SQLHDBC dbc = SQL_NULL_HDBC;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &dbc), SQL_SUCCESS);
        EXPECT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            dbc, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        SQLHSTMT stmt = SQL_NULL_HSTMT;
        EXPECT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt), SQL_SUCCESS);
        SQLRETURN rc = SQLExecDirect(
            stmt, (SQLCHAR*)"CREATE TABLE D47_FRESH (ID INTEGER)", SQL_NTS);
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        SQLDisconnect(dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        return rc;
    };

    EXPECT_TRUE(SQL_SUCCEEDED(create_once()));
    EXPECT_TRUE(SQL_SUCCEEDED(create_once()))
        << "the second connection inherited the first one's schema";

    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

// ── D5: the catalog must not hand out pointers into its own vectors ──────
//
// find_table returned `const MockTable*` into `tables_`, and execute_query
// held it across the whole executor while another connection's CREATE TABLE
// pushed onto that vector and reallocated it. A use-after-free, not a race on
// a value - and one the sanitiser job would catch only if something exercised
// it, which nothing did.
//
// The shape below is that sequence: connection A starts a query against a
// table, connection B creates enough tables to force the vector to grow, and A
// keeps reading. It passes trivially now that find_table copies; under ASan,
// against the pointer-returning version, it is the arrangement that trips.
TEST(CatalogSharing, CreatingTablesWhileAnotherConnectionQueriesIsSafe) {
    SQLHENV henv = SQL_NULL_HENV;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
    ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                            (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);

    SQLHDBC a = SQL_NULL_HDBC, b = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &a), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
        a, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &b), SQL_SUCCESS);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
        b, NULL, (SQLCHAR*)kConn, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));

    SQLHSTMT sa = SQL_NULL_HSTMT, sb = SQL_NULL_HSTMT;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, a, &sa), SQL_SUCCESS);
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, b, &sb), SQL_SUCCESS);

    SQLExecDirect(sa, (SQLCHAR*)"DROP TABLE D5_BASE", SQL_NTS);
    SQLCloseCursor(sa);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        sa, (SQLCHAR*)"CREATE TABLE D5_BASE (ID INTEGER, V VARCHAR(16))",
        SQL_NTS)));
    SQLCloseCursor(sa);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
        sa, (SQLCHAR*)"INSERT INTO D5_BASE (ID, V) VALUES (1, 'a'), (2, 'b')",
        SQL_NTS)));
    SQLCloseCursor(sa);

    // B grows the table vector well past any small-capacity reserve while A
    // reads. Interleaved, so a query is in flight across several reallocations.
    for (int i = 0; i < 32; ++i) {
        const std::string ddl =
            "CREATE TABLE D5_GROW_" + std::to_string(i) + " (ID INTEGER)";
        SQLExecDirect(sb, (SQLCHAR*)ddl.c_str(), SQL_NTS);
        SQLCloseCursor(sb);

        ASSERT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            sa, (SQLCHAR*)"SELECT ID, V FROM D5_BASE ORDER BY ID", SQL_NTS)))
            << "iteration " << i;
        int rows = 0;
        while (SQL_SUCCEEDED(SQLFetch(sa))) {
            char v[32] = {0};
            SQLLEN ind = 0;
            ASSERT_TRUE(SQL_SUCCEEDED(SQLGetData(sa, 2, SQL_C_CHAR, v,
                                                 sizeof(v), &ind)))
                << "iteration " << i;
            ++rows;
        }
        SQLCloseCursor(sa);
        ASSERT_EQ(rows, 2) << "iteration " << i;
    }

    for (int i = 0; i < 32; ++i) {
        const std::string ddl = "DROP TABLE D5_GROW_" + std::to_string(i);
        SQLExecDirect(sb, (SQLCHAR*)ddl.c_str(), SQL_NTS);
        SQLCloseCursor(sb);
    }
    SQLExecDirect(sa, (SQLCHAR*)"DROP TABLE D5_BASE", SQL_NTS);
    SQLCloseCursor(sa);

    SQLFreeHandle(SQL_HANDLE_STMT, sb);
    SQLFreeHandle(SQL_HANDLE_STMT, sa);
    SQLDisconnect(b);
    SQLFreeHandle(SQL_HANDLE_DBC, b);
    SQLDisconnect(a);
    SQLFreeHandle(SQL_HANDLE_DBC, a);
    SQLFreeHandle(SQL_HANDLE_ENV, henv);
}
