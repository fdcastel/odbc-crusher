// Performance Tests - Measure mock driver overhead
#include <gtest/gtest.h>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <chrono>
#include <string>

class PerformanceTest : public ::testing::Test {
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
        const char* conn_str = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn_str, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        
        // Allocate statement
        ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
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

// Test 1: Connection performance
TEST_F(PerformanceTest, RapidConnectDisconnect) {
    const int iterations = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        SQLHDBC test_hdbc = SQL_NULL_HDBC;
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &test_hdbc);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        
        const char* conn_str = "Driver={Mock ODBC Driver};Mode=Success;";
        ret = SQLDriverConnect(test_hdbc, NULL, (SQLCHAR*)conn_str, SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        
        SQLDisconnect(test_hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, test_hdbc);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "100 connections in " << duration.count() << "ms ("
              << (duration.count() / static_cast<double>(iterations)) << "ms average)\n";
    
    // Should be reasonably fast - less than 50ms average per connection
    EXPECT_LT(duration.count() / iterations, 50) << "Connection overhead too high";
}

// Test 2: SQLGetTypeInfo performance  
TEST_F(PerformanceTest, GetTypeInfoOverhead) {
    const int iterations = 100;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        
        // Close cursor for next iteration
        SQLCloseCursor(hstmt);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "100 SQLGetTypeInfo calls in " << duration.count() << "ms ("
              << (duration.count() / static_cast<double>(iterations)) << "ms average)\n";
    
    // Should be very fast - less than 5ms average
    EXPECT_LT(duration.count() / iterations, 5) << "SQLGetTypeInfo overhead too high";
}

// Test 3: Fetch performance with SQLGetTypeInfo
TEST_F(PerformanceTest, FetchPerformance) {
    SQLRETURN ret = SQLGetTypeInfo(hstmt, SQL_ALL_TYPES);
    ASSERT_TRUE(SQL_SUCCEEDED(ret));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    int rowCount = 0;
    while (SQLFetch(hstmt) == SQL_SUCCESS) {
        rowCount++;
        
        // Retrieve some data to make it realistic
        SQLCHAR typeName[128];
        SQLLEN indicator;
        SQLGetData(hstmt, 1, SQL_C_CHAR, typeName, sizeof(typeName), &indicator);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Fetched " << rowCount << " type info rows in " << duration.count() << "ms";
    if (rowCount > 0) {
        std::cout << " (" << (duration.count() / static_cast<double>(rowCount)) << "ms per row)";
    }
    std::cout << "\n";
    
    EXPECT_GT(rowCount, 0) << "Should fetch some rows";
    // Should be fast
    if (rowCount > 0) {
        EXPECT_LT(duration.count() / rowCount, 5) << "Fetch too slow";
    }
}

// Test 4: Handle allocation performance
TEST_F(PerformanceTest, HandleAllocationPerformance) {
    const int iterations = 1000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        SQLHSTMT test_stmt = SQL_NULL_HSTMT;
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &test_stmt);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        
        SQLFreeHandle(SQL_HANDLE_STMT, test_stmt);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "1000 handle allocations in " << duration.count() << "ms ("
              << (duration.count() / static_cast<double>(iterations)) << "ms average)\n";
    
    // Should be very fast - less than 0.2ms average (200ms total for 1000)
    EXPECT_LT(duration.count(), 200) << "Handle allocation too slow";
}
