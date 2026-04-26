// UPDATE / DELETE / SQLRowCount + multi-tuple INSERT — IMPROVEMENT_PLAN.md §7.11
//
// Locks in §7.8 (UPDATE/DELETE row count from MockCatalog) and §7.9
// (multi-tuple INSERT VALUES). Before those fixes the parser hard-coded
// affected_rows = 1 for UPDATE/DELETE (causing the §1.8 SQLRowCount probes
// to FAIL with `1 vs N`) and the multi-tuple parser silently mangled
// `(?,?), (?,?), …` into one malformed row (causing §1.5's batch probe to
// FAIL with `count=1 expected=N`).
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <string>

class DmlTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);

        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn.c_str(),
            SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);

        Exec("CREATE TABLE T (ID INTEGER, V VARCHAR(64))");
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    void Exec(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLCloseCursor(hstmt);
    }

    SQLLEN ExecWithRowCount(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLLEN rc = -2;
        SQLRowCount(hstmt, &rc);
        SQLCloseCursor(hstmt);
        return rc;
    }

    SQLINTEGER CountRows() {
        EXPECT_EQ(SQLExecDirect(hstmt,
            (SQLCHAR*)"SELECT COUNT(*) FROM T", SQL_NTS), SQL_SUCCESS);
        SQLINTEGER count = -1;
        if (SQLFetch(hstmt) == SQL_SUCCESS) {
            SQLLEN ind = 0;
            SQLGetData(hstmt, 1, SQL_C_LONG, &count, sizeof(count), &ind);
        }
        SQLCloseCursor(hstmt);
        return count;
    }

    void Seed(int n) {
        for (int i = 1; i <= n; ++i) {
            std::string s = "INSERT INTO T (ID, V) VALUES ("
                + std::to_string(i) + ", '"
                + std::string(1, static_cast<char>('a' + i - 1)) + "')";
            Exec(s);
        }
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── UPDATE row count (§7.8) ───────────────────────────────────────────────

TEST_F(DmlTest, UpdateNoWhereCountsAllRows) {
    Seed(3);
    EXPECT_EQ(ExecWithRowCount("UPDATE T SET V = 'X'"), 3);
}

TEST_F(DmlTest, UpdateWhereEqualCountsMatching) {
    Seed(5);
    EXPECT_EQ(ExecWithRowCount("UPDATE T SET V = 'X' WHERE ID = 3"), 1);
}

TEST_F(DmlTest, UpdateWhereLessThanCountsMatching) {
    Seed(5);
    EXPECT_EQ(ExecWithRowCount("UPDATE T SET V = 'X' WHERE ID < 4"), 3);
}

// ── DELETE row count + actual deletion (§7.8) ─────────────────────────────

TEST_F(DmlTest, DeleteNoWhereCountsAndEmptiesTable) {
    Seed(4);
    EXPECT_EQ(ExecWithRowCount("DELETE FROM T"), 4);
    EXPECT_EQ(CountRows(), 0);
}

TEST_F(DmlTest, DeleteWhereLessThanRemovesMatching) {
    Seed(5);
    EXPECT_EQ(ExecWithRowCount("DELETE FROM T WHERE ID < 3"), 2);
    // Rows with ID 3, 4, 5 should remain.
    EXPECT_EQ(CountRows(), 3);
}

TEST_F(DmlTest, DeleteWhereInRemovesMatching) {
    Seed(5);
    EXPECT_EQ(ExecWithRowCount("DELETE FROM T WHERE ID IN (1, 3, 5)"), 3);
    EXPECT_EQ(CountRows(), 2);
}

// ── Multi-tuple INSERT VALUES (§7.9) ──────────────────────────────────────
//
// The killer probe was test_param_batch_then_single_row_tail. Here we
// exercise the simpler "literal multi-tuple" path that the §7.9 fix
// targets: parser splits on top-level `),(`, executor emits one MockRow
// per tuple, SQLRowCount reflects the tuple count.

TEST_F(DmlTest, MultiTupleInsertLiteralPersistsAllRows) {
    EXPECT_EQ(ExecWithRowCount(
        "INSERT INTO T (ID, V) VALUES (1, 'a'), (2, 'b'), (3, 'c')"), 3);
    EXPECT_EQ(CountRows(), 3);
}

TEST_F(DmlTest, MultiTupleInsertParameterisedPersistsAllRows) {
    SQLRETURN rc = SQLPrepare(hstmt,
        (SQLCHAR*)"INSERT INTO T (ID, V) VALUES (?, ?), (?, ?), (?, ?)", SQL_NTS);
    ASSERT_EQ(rc, SQL_SUCCESS);

    SQLINTEGER ids[3] = {10, 20, 30};
    char vals[3][8] = {"foo", "bar", "baz"};
    SQLLEN id_ind[3] = {0, 0, 0};
    SQLLEN val_ind[3] = {SQL_NTS, SQL_NTS, SQL_NTS};

    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(SQLBindParameter(hstmt, 1 + 2 * i, SQL_PARAM_INPUT,
                                   SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                   &ids[i], 0, &id_ind[i]), SQL_SUCCESS);
        ASSERT_EQ(SQLBindParameter(hstmt, 2 + 2 * i, SQL_PARAM_INPUT,
                                   SQL_C_CHAR, SQL_VARCHAR, 64, 0,
                                   vals[i], sizeof(vals[i]), &val_ind[i]),
                  SQL_SUCCESS);
    }

    ASSERT_EQ(SQLExecute(hstmt), SQL_SUCCESS);
    SQLLEN row_count = -2;
    SQLRowCount(hstmt, &row_count);
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    EXPECT_EQ(row_count, 3);
    EXPECT_EQ(CountRows(), 3);

    // Verify each row landed correctly (and not as one mangled row).
    EXPECT_EQ(SQLExecDirect(hstmt,
        (SQLCHAR*)"SELECT V FROM T WHERE ID = 20", SQL_NTS), SQL_SUCCESS);
    char buf[64] = {0};
    SQLLEN ind = 0;
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(buf, "bar");
}
