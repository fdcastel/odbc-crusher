// Error Injection Tests - Test FailOn parameter and error scenarios
#include <gtest/gtest.h>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <string>

class ErrorInjectionTest : public ::testing::Test {
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
    
    void ConnectWithConfig(const std::string& config) {
        std::string conn_str = "Driver={Mock ODBC Driver};" + config;
        SQLRETURN ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str.c_str(), 
                                        SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "Connection should succeed";
        
        // Allocate statement
        ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
    }
    
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// Test 1: FailOn=SQLExecute
TEST_F(ErrorInjectionTest, FailOnSQLExecute) {
    ConnectWithConfig("Mode=Partial;FailOn=SQLExecute;ErrorCode=42000");
    
    const char* query = "SELECT * FROM TEST";
    SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)query, SQL_NTS);
    
    EXPECT_EQ(ret, SQL_ERROR) << "SQLExecute should fail when FailOn=SQLExecute";
}

// Test 2: FailOn=SQLGetTypeInfo
TEST_F(ErrorInjectionTest, FailOnSQLGetTypeInfo) {
    ConnectWithConfig("Mode=Partial;FailOn=SQLGetTypeInfo;ErrorCode=HY000");
    
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    EXPECT_EQ(ret, SQL_ERROR) << "SQLGetTypeInfo should fail";
}

// Test 3: FailOn=SQLPrepare
TEST_F(ErrorInjectionTest, FailOnSQLPrepare) {
    ConnectWithConfig("Mode=Partial;FailOn=SQLPrepare;ErrorCode=42000");
    
    const char* query = "SELECT * FROM TEST WHERE id = ?";
    SQLRETURN ret = SQLPrepare(hstmt, (SQLCHAR*)query, SQL_NTS);
    EXPECT_EQ(ret, SQL_ERROR) << "SQLPrepare should fail";
}

// Test 4: Mode=Success - everything succeeds
TEST_F(ErrorInjectionTest, ModeSuccess) {
    ConnectWithConfig("Mode=Success;Catalog=Default");
    
    //  Just test that we can call SQLGetTypeInfo in success mode
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "SQLGetTypeInfo should succeed in Success mode";
}

// Test 5: Basic configuration acceptance
TEST_F(ErrorInjectionTest, AcceptsConfiguration) {
    // Test that various config parameters are accepted
    std::string conn_str = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;MaxConnections=5";
    SQLRETURN ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str.c_str(), 
                                    SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    EXPECT_TRUE(SQL_SUCCEEDED(ret)) << "Should accept valid configuration";
    
    ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
    EXPECT_TRUE(SQL_SUCCEEDED(ret));
}
