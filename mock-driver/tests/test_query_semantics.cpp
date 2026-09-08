// ORDER BY, the shared row store, and escape scanning — IMPROVEMENT_PLAN.md D13
//
// Three of the six defects D13 lists, none of which had a test:
//
//   (2) ORDER BY was read out of the tail of `where_clause`, and
//       `where_clause` was only populated when the statement had a WHERE —
//       so `SELECT ... ORDER BY x` with no WHERE came back unsorted, with no
//       error to say the clause had been ignored.
//   (4) preprocess_escape_sequences treated every `{` as the start of an
//       escape, including one inside a string literal, so
//       `SELECT '{fn NOW()}'` had its *data* rewritten. find_close_brace
//       already tracked quoting; the loop calling it did not.
//   (6) UPDATE and DELETE saw only inserted_data_ while SELECT fell back to
//       generated rows, so the two disagreed about whether a stock catalog
//       table had any rows at all.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace {

class QuerySemanticsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        const char* conn =
            "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";
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

    void Exec(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLCloseCursor(hstmt);
    }

    std::vector<std::string> Column(const std::string& sql, SQLUSMALLINT col = 1) {
        std::vector<std::string> out;
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS))) << sql;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            char buf[256] = {0};
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(hstmt, col, SQL_C_CHAR, buf,
                                         sizeof(buf), &ind))) {
                out.push_back(buf);
            }
        }
        SQLCloseCursor(hstmt);
        return out;
    }

    SQLLEN ExecWithRowCount(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLLEN rc = -2;
        SQLRowCount(hstmt, &rc);
        SQLCloseCursor(hstmt);
        return rc;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── (2) ORDER BY without a WHERE ─────────────────────────────────────────

TEST_F(QuerySemanticsTest, OrderByWorksWithoutAWhereClause) {
    Exec("CREATE TABLE O (ID INTEGER, V VARCHAR(16))");
    Exec("INSERT INTO O (ID, V) VALUES (3, 'c'), (1, 'a'), (2, 'b')");

    const auto asc = Column("SELECT ID FROM O ORDER BY ID");
    ASSERT_EQ(asc.size(), 3u);
    EXPECT_EQ(asc[0], "1");
    EXPECT_EQ(asc[1], "2");
    EXPECT_EQ(asc[2], "3");
}

TEST_F(QuerySemanticsTest, OrderByDescWorksWithoutAWhereClause) {
    Exec("CREATE TABLE O2 (ID INTEGER, V VARCHAR(16))");
    Exec("INSERT INTO O2 (ID, V) VALUES (1, 'a'), (3, 'c'), (2, 'b')");

    const auto desc = Column("SELECT ID FROM O2 ORDER BY ID DESC");
    ASSERT_EQ(desc.size(), 3u);
    EXPECT_EQ(desc[0], "3");
    EXPECT_EQ(desc[2], "1");
}

// ORDER BY alongside a WHERE has to keep working, and the WHERE must not be
// handed the ORDER BY text as part of its predicate.
TEST_F(QuerySemanticsTest, OrderByCombinesWithAWhereClause) {
    Exec("CREATE TABLE O3 (ID INTEGER, V VARCHAR(16))");
    Exec("INSERT INTO O3 (ID, V) VALUES (1, 'a'), (2, 'b'), (3, 'c'), (4, 'd')");

    const auto got = Column("SELECT ID FROM O3 WHERE ID >= 2 ORDER BY ID DESC");
    ASSERT_EQ(got.size(), 3u);
    EXPECT_EQ(got[0], "4");
    EXPECT_EQ(got[2], "2");
}

// ── (4) escapes inside a string literal are data ─────────────────────────

TEST_F(QuerySemanticsTest, EscapeSyntaxInsideALiteralIsNotRewritten) {
    const auto got = Column("SELECT '{fn UCASE(''x'')}'");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "{fn UCASE('x')}")
        << "the literal's contents were preprocessed as an escape";
}

TEST_F(QuerySemanticsTest, EscapeOutsideALiteralIsStillRewritten) {
    const auto got = Column("SELECT {fn UCASE('x')}");
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], "X");
}

// ── (6) one row store for SELECT, UPDATE and DELETE ──────────────────────

// A stock catalog table: SELECT used to generate rows on demand while
// UPDATE/DELETE looked in an empty store and reported 0.
TEST_F(QuerySemanticsTest, DmlAndSelectAgreeOnAStockTablesRowCount) {
    const int selected = static_cast<int>(Column("SELECT CUSTOMER_ID FROM CUSTOMERS").size());
    ASSERT_GT(selected, 0) << "the stock table generated no rows to compare against";

    const SQLLEN deleted = ExecWithRowCount("DELETE FROM CUSTOMERS");
    EXPECT_EQ(deleted, selected)
        << "DELETE saw an empty table while SELECT saw " << selected << " rows";
}

// And once emptied it stays empty, instead of regenerating on the next read.
TEST_F(QuerySemanticsTest, AnEmptiedStockTableStaysEmpty) {
    ASSERT_GT(Column("SELECT USER_ID FROM USERS").size(), 0u);
    ExecWithRowCount("DELETE FROM USERS");
    EXPECT_EQ(Column("SELECT USER_ID FROM USERS").size(), 0u)
        << "the deleted rows came back";
}

TEST_F(QuerySemanticsTest, UpdateCountsTheSameRowsSelectReturns) {
    const int selected = static_cast<int>(Column("SELECT PRODUCT_ID FROM PRODUCTS").size());
    ASSERT_GT(selected, 0);
    EXPECT_EQ(ExecWithRowCount("UPDATE PRODUCTS SET NAME = 'x'"), selected);
}

// ── D65: COUNT(*) ignored the WHERE clause ───────────────────────────────
//
// `SELECT COUNT(*) FROM t WHERE ID = 7` returned the number of rows in the
// table. Not an approximation — the wrong question answered confidently,
// which a caller cannot detect. Found writing I8's probes, both of which read
// a count back to see whether uncommitted work had become durable and got the
// table's size instead.

TEST_F(QuerySemanticsTest, CountHonoursTheWhereClause) {
    Exec("CREATE TABLE C1 (ID INTEGER, V VARCHAR(16))");
    Exec("INSERT INTO C1 VALUES (1, 'a')");
    Exec("INSERT INTO C1 VALUES (2, 'b')");
    Exec("INSERT INTO C1 VALUES (3, 'c')");

    EXPECT_EQ(Column("SELECT COUNT(*) FROM C1"),
              (std::vector<std::string>{"3"}));

    // The bug: each of these used to answer "3".
    EXPECT_EQ(Column("SELECT COUNT(*) FROM C1 WHERE ID = 2"),
              (std::vector<std::string>{"1"}));
    EXPECT_EQ(Column("SELECT COUNT(*) FROM C1 WHERE ID > 1"),
              (std::vector<std::string>{"2"}));
    // A predicate that matches nothing must count nothing — the case an
    // "is it gone?" check depends on, and the one the old code could never
    // report.
    EXPECT_EQ(Column("SELECT COUNT(*) FROM C1 WHERE ID = 4242"),
              (std::vector<std::string>{"0"}));

    Exec("DROP TABLE C1");
}

TEST_F(QuerySemanticsTest, CountAgreesWithTheRowsSelectReturns) {
    // The two paths read the same store now. They did not before: COUNT(*)
    // returned early, so on a stock catalog table it reported ResultSetSize
    // while SELECT materialised and filtered.
    const auto rows = Column("SELECT CUSTOMER_ID FROM CUSTOMERS WHERE CUSTOMER_ID = 3");
    const auto counted = Column("SELECT COUNT(*) FROM CUSTOMERS WHERE CUSTOMER_ID = 3");
    ASSERT_EQ(counted.size(), 1u);
    EXPECT_EQ(counted[0], std::to_string(rows.size()));
}

TEST_F(QuerySemanticsTest, CountRefusesAPredicateItCannotEvaluate) {
    // D12's rule, which COUNT(*) was bypassing entirely: a filter that is not
    // understood is an error, not an excuse to answer without it. A count
    // derived from a predicate nobody evaluated is exactly the D65 bug again.
    Exec("CREATE TABLE C2 (ID INTEGER, V VARCHAR(16))");
    Exec("INSERT INTO C2 VALUES (1, 'a')");

    SQLRETURN ret = SQLExecDirect(
        hstmt, (SQLCHAR*)"SELECT COUNT(*) FROM C2 WHERE ID BETWEEN 1 AND 9",
        SQL_NTS);
    EXPECT_EQ(ret, SQL_ERROR)
        << "an unreadable predicate must not silently become no predicate";
    SQLCloseCursor(hstmt);

    Exec("DROP TABLE C2");
}

}  // namespace
