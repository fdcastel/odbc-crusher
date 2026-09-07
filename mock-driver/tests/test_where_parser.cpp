// WHERE-clause parsing and malformed-SQL robustness — IMPROVEMENT_PLAN.md D12
//
// Two claims, both of which the mock used to get wrong in the dangerous
// direction:
//
//   1. A predicate the mock cannot read must be reported, not treated as
//      "every row". `make_where_predicate` had four separate always-true
//      fallbacks, and `DELETE FROM t WHERE <unparsable>` therefore erased the
//      whole table and reported success — a driver under test would have been
//      blamed for the mock's parser.
//   2. Malformed SQL must produce a diagnostic, not undefined behaviour.
//      `front()`/`back()` ran on strings the caller could make empty.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <string>

namespace {

class WhereParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
        const char* conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn, SQL_NTS, NULL, 0, NULL,
            SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);

        Exec("CREATE TABLE W (ID INTEGER, V VARCHAR(64))");
        Exec("INSERT INTO W (ID, V) VALUES (1, 'a'), (2, 'b'), (3, 'c')");
        Exec("INSERT INTO W (ID, V) VALUES (4, NULL)");
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

    // Runs `sql` and captures its SQLSTATE *before* anything else touches the
    // statement. SQLCloseCursor on a statement with no open cursor posts 24000
    // and replaces the diagnostic queue, which is how a test ends up asserting
    // on the cleanup's error instead of the statement's.
    SQLRETURN Try(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        last_state_ = ReadState();
        SQLCloseCursor(hstmt);
        return ret;
    }

    std::string State() const { return last_state_; }

    // The SQLSTATE of the first diagnostic record, or "" if there is none.
    std::string ReadState() {
        SQLCHAR state[6] = {0};
        SQLINTEGER native = 0;
        SQLCHAR msg[512] = {0};
        SQLSMALLINT len = 0;
        if (SQLGetDiagRec(SQL_HANDLE_STMT, hstmt, 1, state, &native,
                          msg, sizeof(msg), &len) == SQL_NO_DATA) {
            return "";
        }
        return std::string(reinterpret_cast<char*>(state));
    }

    int CountRows(const std::string& sql) {
        EXPECT_TRUE(SQL_SUCCEEDED(SQLExecDirect(
            hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS))) << sql;
        int n = 0;
        while (SQLFetch(hstmt) == SQL_SUCCESS) ++n;
        SQLCloseCursor(hstmt);
        return n;
    }

    int TableSize() { return CountRows("SELECT ID FROM W"); }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    std::string last_state_;
};

// ── The headline: an unreadable predicate must not delete the table ──────

TEST_F(WhereParserTest, DeleteWithAnUnreadablePredicateErrorsAndKeepsTheRows) {
    ASSERT_EQ(TableSize(), 4);

    // LIKE is not implemented. Before D12 this fell through to "match
    // everything" and erased all four rows, returning SQL_SUCCESS.
    EXPECT_FALSE(SQL_SUCCEEDED(Try("DELETE FROM W WHERE V LIKE 'a%'")));
    EXPECT_EQ(State(), "42000");
    EXPECT_EQ(TableSize(), 4) << "the mock's parser gap deleted the table";
}

TEST_F(WhereParserTest, DeleteWithAnUnknownColumnErrorsAndKeepsTheRows) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("DELETE FROM W WHERE NOPE = 1")));
    EXPECT_EQ(State(), "42S22");
    EXPECT_EQ(TableSize(), 4);
}

TEST_F(WhereParserTest, SelectWithAnUnreadablePredicateErrors) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("SELECT ID FROM W WHERE V LIKE 'a%'")));
    EXPECT_EQ(State(), "42000");
}

TEST_F(WhereParserTest, UpdateWithAnUnreadablePredicateErrors) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("UPDATE W SET V = 'z' WHERE V LIKE 'a%'")));
    EXPECT_EQ(State(), "42000");
}

// ── AND / OR ─────────────────────────────────────────────────────────────

// `WHERE a = 1 AND b = 2` used to parse as a single comparison of `a`
// against the *string* "1 AND b = 2": stoll stopped at the 1 and nobody
// checked how much of the token it had consumed.
TEST_F(WhereParserTest, AndNarrowsTheMatch) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID >= 2 AND ID <= 3"), 2);
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID = 1 AND V = 'a'"), 1);
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID = 1 AND V = 'b'"), 0);
}

TEST_F(WhereParserTest, OrWidensTheMatch) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID = 1 OR ID = 3"), 2);
}

TEST_F(WhereParserTest, ParenthesesGroupAgainstPrecedence) {
    // (1 or 2) and V='b' -> row 2 only. Without grouping support the whole
    // clause would be unreadable and the statement would fail.
    EXPECT_EQ(CountRows(
        "SELECT ID FROM W WHERE (ID = 1 OR ID = 2) AND V = 'b'"), 1);
}

// A column whose name contains the letters AND must not be split on.
TEST_F(WhereParserTest, AndInsideAnIdentifierIsNotAConjunction) {
    Exec("CREATE TABLE BRANDS (BRAND VARCHAR(16))");
    Exec("INSERT INTO BRANDS (BRAND) VALUES ('x')");
    EXPECT_EQ(CountRows("SELECT BRAND FROM BRANDS WHERE BRAND = 'x'"), 1);
}

// ── IS [NOT] NULL and NOT IN ─────────────────────────────────────────────

TEST_F(WhereParserTest, IsNullSelectsTheNullRow) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V IS NULL"), 1);
}

TEST_F(WhereParserTest, IsNotNullSelectsTheRest) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V IS NOT NULL"), 3);
}

TEST_F(WhereParserTest, NotInExcludes) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID NOT IN (1, 2)"), 2);
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE ID IN (1, 2)"), 2);
}

// An INSERT whose value list does not match its column list is 21S01, not a
// row. `VALUES (1,,3)` names two columns and supplies three values; the empty
// middle expression also used to reach front() on an empty string.
TEST_F(WhereParserTest, InsertValueCountMustMatchTheColumnList) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("INSERT INTO W (ID, V) VALUES (1,,3)")));
    EXPECT_EQ(State(), "21S01");
    EXPECT_EQ(TableSize(), 4) << "a malformed INSERT added a row";
}

// ── Malformed SQL must not be undefined behaviour ────────────────────────
//
// Each of these reached a front()/back() on a string the statement text
// could make empty. The assertion is only that the driver answers at all:
// what matters is that it does not read past the end of a buffer. Run under
// the ASan job these would abort before reaching the EXPECT.

TEST_F(WhereParserTest, MalformedStatementsAnswerInsteadOfCrashing) {
    const char* malformed[] = {
        "SELECT CAST( AS INTEGER)",
        "SELECT CAST(",
        "SELECT {fn YEAR()}",
        "SELECT {fn MONTH()}",
        "SELECT {fn DAYOFWEEK()}",
        "SELECT ID FROM W WHERE ID =",
        "SELECT ID FROM W WHERE = 1",
        "SELECT ID FROM W WHERE",
        "SELECT ID FROM W WHERE ID IN",
        "SELECT ID FROM W WHERE ID IS",
        "SELECT DATE ''",
        "SELECT N''",
        "DELETE FROM W WHERE ID = 'unterminated",
    };
    for (const char* sql : malformed) {
        SQLRETURN rc = Try(sql);
        // Either answer is acceptable; hanging, crashing or corrupting is not.
        EXPECT_TRUE(rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO
                    || rc == SQL_ERROR)
            << "unexpected rc " << rc << " for: " << sql;
    }
    // And the table is still intact after all of that.
    EXPECT_EQ(TableSize(), 4);
}

}  // namespace
