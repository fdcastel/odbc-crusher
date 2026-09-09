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
#include <cstdio>
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

    // BETWEEN is not implemented. Before D12 a clause the parser could
    // not read fell through to "match everything" and erased all four
    // rows, returning SQL_SUCCESS. (This example used to be LIKE; D42
    // implemented LIKE, so it needed a predicate that is still unread.)
    EXPECT_FALSE(SQL_SUCCEEDED(Try("DELETE FROM W WHERE V BETWEEN 'a' AND 'b'")));
    EXPECT_EQ(State(), "42000");
    EXPECT_EQ(TableSize(), 4) << "the mock's parser gap deleted the table";
}

TEST_F(WhereParserTest, DeleteWithAnUnknownColumnErrorsAndKeepsTheRows) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("DELETE FROM W WHERE NOPE = 1")));
    EXPECT_EQ(State(), "42S22");
    EXPECT_EQ(TableSize(), 4);
}

TEST_F(WhereParserTest, SelectWithAnUnreadablePredicateErrors) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("SELECT ID FROM W WHERE V BETWEEN 'a' AND 'b'")));
    EXPECT_EQ(State(), "42000");
}

TEST_F(WhereParserTest, UpdateWithAnUnreadablePredicateErrors) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("UPDATE W SET V = 'z' WHERE V BETWEEN 'a' AND 'b'")));
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

// H18: the same rule for the form that names no columns, which the test above
// did not reach. `INSERT INTO W VALUES (1)` against a two-column table used to
// succeed here — the row was padded to the table's width with NULL — while
// Firebird answers -804, PostgreSQL 42601 and MySQL 21S01. Four crusher probes
// carried exactly that statement and passed the e2e suite for their whole
// existence because this mock accepted it; against a real server the throw
// skipped their rollback and the table guard's DROP then wedged the entire
// run. A mock that is kinder than every real driver hides probe bugs rather
// than driver bugs, which is the opposite of its job.
TEST_F(WhereParserTest, InsertWithNoColumnListMustFillEveryColumn) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("INSERT INTO W VALUES (1)")))
        << "one value for a two-column table was accepted";
    EXPECT_EQ(State(), "21S01");
    EXPECT_EQ(TableSize(), 4) << "a short INSERT added a row";

    // Too many is the same mistake in the other direction.
    EXPECT_FALSE(SQL_SUCCEEDED(Try("INSERT INTO W VALUES (1, 2, 3)")));
    EXPECT_EQ(State(), "21S01");
    EXPECT_EQ(TableSize(), 4);

    // And the well-formed column-less form still works, or this check would
    // have broken every probe that uses it rather than only the wrong ones.
    EXPECT_TRUE(SQL_SUCCEEDED(Try("INSERT INTO W VALUES (99, 'ok')")));
    EXPECT_EQ(TableSize(), 5);
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


// ── D42: LIKE is evaluated, and honours an ESCAPE clause ─────────────────
//
// A LIKE predicate used to fall through the filter entirely, so every row came
// back - and the mock advertises SQL_LIKE_ESCAPE_CLAUSE while being unable to
// fail a probe that tests it.

TEST_F(WhereParserTest, LikeWithATrailingWildcardFilters) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V LIKE 'a%'"), 1);
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V LIKE '%'"), 3);
}

TEST_F(WhereParserTest, NotLikeInverts) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V NOT LIKE 'a%'"), 2);
}

TEST_F(WhereParserTest, UnderscoreMatchesOneCharacter) {
    Exec("INSERT INTO W (ID, V) VALUES (5, 'ab'), (6, 'abc')");
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V LIKE 'a_'"), 1);
}

// The point of the escape clause: with `!` named as the escape, `!_` is a
// literal underscore rather than "any character".
TEST_F(WhereParserTest, EscapeClauseMakesAWildcardLiteral) {
    Exec("INSERT INTO W (ID, V) VALUES (7, 'x_y'), (8, 'xzy')");
    EXPECT_EQ(CountRows(
        "SELECT ID FROM W WHERE V LIKE 'x!_y' ESCAPE '!'"), 1);
    // Without the escape, `_` is a wildcard and both rows match.
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE V LIKE 'x_y'"), 2);
}

// The shape the escape probe uses: both sides literal, no table involved.
TEST_F(WhereParserTest, LiteralLikeWithEscapeIsEvaluated) {
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE 'xzy' LIKE 'x!_y' ESCAPE '!'"), 0)
        << "the escaped underscore matched a 'z'";
    // A true constant predicate keeps every row; W has four.
    EXPECT_EQ(CountRows("SELECT ID FROM W WHERE 'x_y' LIKE 'x!_y' ESCAPE '!'"), 4)
        << "the escaped underscore did not match a literal underscore";
}

// ── D44 / D43: a literal SELECT evaluates its WHERE ──────────────────────
//
// `SELECT 1 WHERE <predicate>` used to parse the whole tail as one select-list
// expression, so the predicate was neither evaluated nor reported and the row
// came back whatever it said. That is the portable no-table shape a literal
// predicate probe needs.

TEST_F(WhereParserTest, ALiteralSelectEvaluatesAConstantPredicate) {
    EXPECT_EQ(CountRows("SELECT 1 WHERE 1 = 1"), 1);
    EXPECT_EQ(CountRows("SELECT 1 WHERE 1 = 0"), 0)
        << "the constant predicate was ignored";
}

TEST_F(WhereParserTest, ALiteralSelectEvaluatesALikeEscape) {
    EXPECT_EQ(CountRows("SELECT 1 WHERE 'xzy' LIKE 'x!_y' ESCAPE '!'"), 0);
    EXPECT_EQ(CountRows("SELECT 1 WHERE 'x_y' LIKE 'x!_y' ESCAPE '!'"), 1);
}

TEST_F(WhereParserTest, ALiteralSelectReportsAnUnreadablePredicate) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("SELECT 1 WHERE 'a' BETWEEN 'a' AND 'b'")));
    EXPECT_EQ(State(), "42000");
}

// D43, measured: a derived table really is unsupported, and the mock does not
// advertise it. An honest 42S02 is the right answer, and this pins it so the
// row's claim stays checkable.
TEST_F(WhereParserTest, ADerivedTableIsAnHonestTableNotFound) {
    EXPECT_FALSE(SQL_SUCCEEDED(Try("SELECT 1 FROM (SELECT 1 AS A) T1")));
    EXPECT_EQ(State(), "42S02");
}

// ...but the {oj} escape does execute, which is what D43 assumed it could
// not. The join is not evaluated - the parser takes the first table and
// ignores the rest - so this pins execution, not join semantics.
TEST_F(WhereParserTest, AnOuterJoinEscapeExecutes) {
    EXPECT_TRUE(SQL_SUCCEEDED(Try(
        "SELECT CUSTOMER_ID FROM {oj CUSTOMERS T1 LEFT OUTER JOIN CUSTOMERS T2"
        " ON T1.CUSTOMER_ID = T2.CUSTOMER_ID}")));
}


}  // namespace
