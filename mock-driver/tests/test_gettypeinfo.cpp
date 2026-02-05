// Test for SQLGetTypeInfo - Regression test for catalog function crash
#include <gtest/gtest.h>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>

class SQLGetTypeInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        SQLRETURN ret;
        
        // Allocate environment
        ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        // Set ODBC version
        ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        // Allocate connection
        ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        // Connect
        const char* conn_str = "Driver={Mock ODBC Driver};Mode=Success;";
        ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        // Allocate statement
        ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        ASSERT_EQ(ret, SQL_SUCCESS);
    }
    
    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, henv);
        }
    }
    
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

TEST_F(SQLGetTypeInfoTest, CanCallSQLGetTypeInfo) {
    // This should not crash
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQLGetTypeInfo should succeed";
}

TEST_F(SQLGetTypeInfoTest, CanFetchTypeInfoRows) {
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    ASSERT_TRUE(SQL_SUCCEEDED(ret));
    
    // Should return multiple type info rows
    int rowCount = 0;
    while ((ret = SQLFetch(hstmt)) == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        rowCount++;
        
        // Verify we can read the type name
        SQLCHAR typeName[128] = {0};
        SQLLEN indicator = 0;
        ret = SQLGetData(hstmt, 1, SQL_C_CHAR, typeName, sizeof(typeName), &indicator);
        EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQLGetData should succeed for row " << rowCount;
        EXPECT_GT(strlen((char*)typeName), 0) << "Type name should not be empty";
    }
    
    EXPECT_EQ(ret, SQL_NO_DATA) << "Should end with SQL_NO_DATA";
    EXPECT_GT(rowCount, 0) << "Should have returned at least one type";
}

TEST_F(SQLGetTypeInfoTest, CanGetSpecificType) {
    // Test with a specific SQL type
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_INTEGER);
    EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQLGetTypeInfo for SQL_INTEGER should succeed";
}
