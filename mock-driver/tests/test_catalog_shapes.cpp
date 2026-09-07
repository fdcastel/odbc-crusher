// Every catalog function's result-set shape — IMPROVEMENT_PLAN.md D17
//
// D17's first item: "a single 'column count matches the spec' test would have
// caught D25". D25 was SQLProcedureColumns emitting 8 of its 19 columns, and
// nothing in the suite touched any catalog function at all — so a client
// reading by ordinal, which is what the spec's column numbers are for, read
// the wrong column and no test noticed.
//
// This is that test, for all ten. Column counts and names come from the ODBC
// 3.8 reference; where the spec renames a column between versions the 3.x name
// is used, which is what the driver reports.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace {

class CatalogShapeTest : public ::testing::Test {
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

    // Checks the open result set against the spec's column list, by count and
    // by name in order. Closes the cursor.
    void ExpectShape(const char* what, const std::vector<const char*>& expected) {
        SQLSMALLINT cols = -1;
        ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols))) << what;
        EXPECT_EQ(cols, static_cast<SQLSMALLINT>(expected.size()))
            << what << " returned the wrong number of columns";

        const SQLSMALLINT n = std::min(
            cols, static_cast<SQLSMALLINT>(expected.size()));
        for (SQLSMALLINT i = 0; i < n; ++i) {
            char name[128] = {0};
            SQLSMALLINT name_len = 0, type = 0, scale = 0, nullable = 0;
            SQLULEN size = 0;
            ASSERT_TRUE(SQL_SUCCEEDED(SQLDescribeCol(
                hstmt, static_cast<SQLUSMALLINT>(i + 1), (SQLCHAR*)name,
                sizeof(name), &name_len, &type, &size, &scale, &nullable)))
                << what << " column " << (i + 1);
            EXPECT_STREQ(name, expected[static_cast<size_t>(i)])
                << what << " at ordinal " << (i + 1);
        }
        SQLCloseCursor(hstmt);
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

TEST_F(CatalogShapeTest, Tables) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTables(hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    ExpectShape("SQLTables",
                {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "TABLE_TYPE", "REMARKS"});
}

TEST_F(CatalogShapeTest, Columns) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColumns(hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    ExpectShape("SQLColumns", {
        "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "DATA_TYPE",
        "TYPE_NAME", "COLUMN_SIZE", "BUFFER_LENGTH", "DECIMAL_DIGITS",
        "NUM_PREC_RADIX", "NULLABLE", "REMARKS", "COLUMN_DEF", "SQL_DATA_TYPE",
        "SQL_DATETIME_SUB", "CHAR_OCTET_LENGTH", "ORDINAL_POSITION",
        "IS_NULLABLE"});
}

TEST_F(CatalogShapeTest, PrimaryKeys) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLPrimaryKeys(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"CUSTOMERS", SQL_NTS)));
    ExpectShape("SQLPrimaryKeys", {
        "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "KEY_SEQ",
        "PK_NAME"});
}

TEST_F(CatalogShapeTest, ForeignKeys) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLForeignKeys(
        hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0, NULL, 0,
        (SQLCHAR*)"ORDERS", SQL_NTS)));
    ExpectShape("SQLForeignKeys", {
        "PKTABLE_CAT", "PKTABLE_SCHEM", "PKTABLE_NAME", "PKCOLUMN_NAME",
        "FKTABLE_CAT", "FKTABLE_SCHEM", "FKTABLE_NAME", "FKCOLUMN_NAME",
        "KEY_SEQ", "UPDATE_RULE", "DELETE_RULE", "FK_NAME", "PK_NAME",
        "DEFERRABILITY"});
}

TEST_F(CatalogShapeTest, Statistics) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLStatistics(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"CUSTOMERS", SQL_NTS,
        SQL_INDEX_ALL, SQL_QUICK)));
    ExpectShape("SQLStatistics", {
        "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "NON_UNIQUE",
        "INDEX_QUALIFIER", "INDEX_NAME", "TYPE", "ORDINAL_POSITION",
        "COLUMN_NAME", "ASC_OR_DESC", "CARDINALITY", "PAGES",
        "FILTER_CONDITION"});
}

TEST_F(CatalogShapeTest, SpecialColumns) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLSpecialColumns(
        hstmt, SQL_BEST_ROWID, NULL, 0, NULL, 0, (SQLCHAR*)"CUSTOMERS", SQL_NTS,
        SQL_SCOPE_CURROW, SQL_NULLABLE)));
    ExpectShape("SQLSpecialColumns", {
        "SCOPE", "COLUMN_NAME", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE",
        "BUFFER_LENGTH", "DECIMAL_DIGITS", "PSEUDO_COLUMN"});
}

TEST_F(CatalogShapeTest, Procedures) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedures(hstmt, NULL, 0, NULL, 0, NULL, 0)));
    ExpectShape("SQLProcedures", {
        "PROCEDURE_CAT", "PROCEDURE_SCHEM", "PROCEDURE_NAME", "NUM_INPUT_PARAMS",
        "NUM_OUTPUT_PARAMS", "NUM_RESULT_SETS", "REMARKS", "PROCEDURE_TYPE"});
}

// The one D25 was about.
TEST_F(CatalogShapeTest, ProcedureColumns) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLProcedureColumns(
        hstmt, NULL, 0, NULL, 0, NULL, 0, NULL, 0)));
    ExpectShape("SQLProcedureColumns", {
        "PROCEDURE_CAT", "PROCEDURE_SCHEM", "PROCEDURE_NAME", "COLUMN_NAME",
        "COLUMN_TYPE", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE", "BUFFER_LENGTH",
        "DECIMAL_DIGITS", "NUM_PREC_RADIX", "NULLABLE", "REMARKS", "COLUMN_DEF",
        "SQL_DATA_TYPE", "SQL_DATETIME_SUB", "CHAR_OCTET_LENGTH",
        "ORDINAL_POSITION", "IS_NULLABLE"});
}

TEST_F(CatalogShapeTest, TablePrivileges) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLTablePrivileges(
        hstmt, NULL, 0, NULL, 0, NULL, 0)));
    ExpectShape("SQLTablePrivileges", {
        "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "GRANTOR", "GRANTEE",
        "PRIVILEGE", "IS_GRANTABLE"});
}

TEST_F(CatalogShapeTest, ColumnPrivileges) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColumnPrivileges(
        hstmt, NULL, 0, NULL, 0, (SQLCHAR*)"CUSTOMERS", SQL_NTS, NULL, 0)));
    ExpectShape("SQLColumnPrivileges", {
        "TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "GRANTOR",
        "GRANTEE", "PRIVILEGE", "IS_GRANTABLE"});
}

// SQLGetTypeInfo is a catalog function too, and its shape is fixed the same
// way. 19 columns in ODBC 3.x.
TEST_F(CatalogShapeTest, GetTypeInfo) {
    ASSERT_TRUE(SQL_SUCCEEDED(SQLGetTypeInfo(hstmt, SQL_ALL_TYPES)));
    SQLSMALLINT cols = -1;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(hstmt, &cols)));
    SQLCloseCursor(hstmt);
    EXPECT_EQ(cols, 19) << "SQLGetTypeInfo returns 19 columns in ODBC 3.x";
}

}  // namespace
