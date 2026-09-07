// Catalog shape and pattern semantics — IMPROVEMENT_PLAN.md D25
//
//   * SQLProcedureColumns emitted 8 of the 19 columns the spec defines, so a
//     client reading by ordinal — which is what the spec's column numbers are
//     for — read the wrong column or ran off the end of the result set.
//   * matches_pattern ignored the `\` escape the driver advertises through
//     SQLGetInfo(SQL_SEARCH_PATTERN_ESCAPE), so asking for a name containing
//     a literal `%` returned every name.
//   * SQL_ATTR_METADATA_ID had no implementation anywhere, so a client that
//     set it still got pattern matching — the one thing it exists to disable.
//   * The W wrappers turned an empty-string argument into a null pointer at
//     ten sites, erasing the distinction SQLTables' three enumeration modes
//     are built on. None of the three was implemented.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace {

class CatalogSemanticsTest : public ::testing::Test {
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

    // Values of one column across every row of the open result set.
    std::vector<std::string> ColumnValues(SQLUSMALLINT col) {
        std::vector<std::string> out;
        while (SQL_SUCCEEDED(SQLFetch(hstmt))) {
            char buf[256] = {0};
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(hstmt, col, SQL_C_CHAR, buf,
                                         sizeof(buf), &ind))) {
                out.push_back(ind == SQL_NULL_DATA ? std::string("<null>") : buf);
            }
        }
        SQLCloseCursor(hstmt);
        return out;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── SQLProcedureColumns has all 19 columns ───────────────────────────────

TEST_F(CatalogSemanticsTest, ProcedureColumnsHasAllNineteenColumns) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_INOUT", SQL_NTS, NULL, 0)));
    SQLSMALLINT cols = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(cols, 19) << "SQLProcedureColumns is defined to return 19 columns";
}

TEST_F(CatalogSemanticsTest, ProcedureColumnsNamesEveryColumnInSpecOrder) {
    static const char* kExpected[] = {
        "PROCEDURE_CAT", "PROCEDURE_SCHEM", "PROCEDURE_NAME", "COLUMN_NAME",
        "COLUMN_TYPE", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE",
        "BUFFER_LENGTH", "DECIMAL_DIGITS", "NUM_PREC_RADIX", "NULLABLE",
        "REMARKS", "COLUMN_DEF", "SQL_DATA_TYPE", "SQL_DATETIME_SUB",
        "CHAR_OCTET_LENGTH", "ORDINAL_POSITION", "IS_NULLABLE"
    };
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_INOUT", SQL_NTS, NULL, 0)));
    for (SQLUSMALLINT i = 0; i < 19; ++i) {
        char name[64] = {0};
        SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
        SQLULEN size = 0;
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDescribeCol(
            hstmt, static_cast<SQLUSMALLINT>(i + 1), (SQLCHAR*)name,
            sizeof(name), &name_len, &type, &size, &scale, &nullable)))
            << "column " << (i + 1);
        EXPECT_STREQ(name, kExpected[i]) << "at ordinal " << (i + 1);
    }
    SQLCloseCursor(hstmt);
}

// ORDINAL_POSITION is the parameter's position in the procedure, and a
// function's return value is position 0 by definition.
TEST_F(CatalogSemanticsTest, ProcedureColumnsNumbersParametersFromOne) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_INOUT", SQL_NTS, NULL, 0)));
    const auto ordinals = ColumnValues(18);
    ASSERT_EQ(ordinals.size(), 3u);
    EXPECT_EQ(ordinals[0], "1");
    EXPECT_EQ(ordinals[2], "3");
}

TEST_F(CatalogSemanticsTest, AFunctionsReturnValueIsOrdinalZero) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"MOCK_FN", SQL_NTS, NULL, 0)));
    const auto ordinals = ColumnValues(18);
    ASSERT_GE(ordinals.size(), 1u);
    EXPECT_EQ(ordinals[0], "0") << "the return value is not parameter 1";
}

// ── the search-pattern escape ────────────────────────────────────────────

TEST_F(CatalogSemanticsTest, EscapedWildcardMatchesTheLiteralCharacter) {
    Exec("CREATE TABLE PCT_A (ID INTEGER)");     // no literal % in the name
    Exec("CREATE TABLE P_B (ID INTEGER)");

    // `P\_B` asks for the one name that literally contains an underscore.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"P\\_B", SQL_NTS, NULL, 0)));
    const auto names = ColumnValues(3);
    EXPECT_EQ(names.size(), 1u)
        << "the escape was ignored, so `_` matched any character";
    if (names.size() == 1) EXPECT_EQ(names[0], "P_B");
}

TEST_F(CatalogSemanticsTest, UnescapedUnderscoreStillMatchesAnyCharacter) {
    Exec("CREATE TABLE Q_B (ID INTEGER)");
    Exec("CREATE TABLE QXB (ID INTEGER)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"Q_B", SQL_NTS, NULL, 0)));
    EXPECT_EQ(ColumnValues(3).size(), 2u);
}

// ── SQL_ATTR_METADATA_ID ─────────────────────────────────────────────────

TEST_F(CatalogSemanticsTest, MetadataIdRoundTrips) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0)));
    SQLULEN got = SQL_FALSE;
    SQLINTEGER len = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetStmtAttr(
        hstmt, SQL_ATTR_METADATA_ID, &got, sizeof(got), &len)));
    EXPECT_EQ(got, (SQLULEN)SQL_TRUE);
}

TEST_F(CatalogSemanticsTest, MetadataIdTurnsOffPatternMatching) {
    Exec("CREATE TABLE M_B (ID INTEGER)");
    Exec("CREATE TABLE MXB (ID INTEGER)");

    // As a pattern, `M_B` matches both.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"M_B", SQL_NTS, NULL, 0)));
    EXPECT_EQ(ColumnValues(3).size(), 2u);

    // As an identifier, it matches only the name that is spelled that way.
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"M_B", SQL_NTS, NULL, 0)));
    const auto names = ColumnValues(3);
    EXPECT_EQ(names.size(), 1u)
        << "SQL_ATTR_METADATA_ID did not turn off pattern matching";
    if (names.size() == 1) EXPECT_EQ(names[0], "M_B");
}

// Identifiers are case-insensitive unless quoted.
TEST_F(CatalogSemanticsTest, MetadataIdComparesIdentifiersCaseInsensitively) {
    Exec("CREATE TABLE MIXEDCASE (ID INTEGER)");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSetStmtAttr(
        hstmt, SQL_ATTR_METADATA_ID, (SQLPOINTER)SQL_TRUE, 0)));
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"mixedcase", SQL_NTS, NULL, 0)));
    EXPECT_EQ(ColumnValues(3).size(), 1u);
}

// ── SQLTables' enumeration modes ─────────────────────────────────────────
//
// Each is signalled by "%" in one argument and an *empty string* in the
// others. The W wrappers used to turn an empty string into a null pointer, so
// none of these could be told from an ordinary query.

TEST_F(CatalogSemanticsTest, AllCatalogsModeReturnsCatalogNamesOnly) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt,
        (SQLCHAR*)"%", SQL_NTS,
        (SQLCHAR*)"",  SQL_NTS,
        (SQLCHAR*)"",  SQL_NTS,
        NULL, 0)));
    const auto names = ColumnValues(3);     // TABLE_NAME
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "<null>") << "SQL_ALL_CATALOGS returned table rows";
}

TEST_F(CatalogSemanticsTest, AllSchemasModeReturnsSchemaNamesOnly) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt,
        (SQLCHAR*)"",  SQL_NTS,
        (SQLCHAR*)"%", SQL_NTS,
        (SQLCHAR*)"",  SQL_NTS,
        NULL, 0)));
    const auto names = ColumnValues(3);
    ASSERT_EQ(names.size(), 1u);
    EXPECT_EQ(names[0], "<null>");
}

TEST_F(CatalogSemanticsTest, AllTableTypesModeReturnsTypeNamesOnly) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt,
        (SQLCHAR*)"",  SQL_NTS,
        (SQLCHAR*)"",  SQL_NTS,
        (SQLCHAR*)"",  SQL_NTS,
        (SQLCHAR*)"%", SQL_NTS)));
    const auto types = ColumnValues(4);     // TABLE_TYPE
    ASSERT_GT(types.size(), 0u);
    for (const auto& t : types) {
        EXPECT_NE(t, "<null>") << "a type row with no type";
    }
}

// A null pointer is still "no filter", which is what every ordinary caller
// passes and what must not change.
TEST_F(CatalogSemanticsTest, NullArgumentsStillMeanNoFilter) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(
        hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    EXPECT_GT(ColumnValues(3).size(), 1u);
}

}  // namespace
