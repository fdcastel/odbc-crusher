// Tests for ODBC Handle Management
#include <gtest/gtest.h>
#include "driver/handles.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

using namespace mock_odbc;

// External declaration of ODBC functions
extern "C" {
    SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT, SQLHANDLE, SQLHANDLE*);
    SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT, SQLHANDLE);
    SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER);
    SQLRETURN SQL_API SQLGetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER*);
}

class HandleTest : public ::testing::Test {
protected:
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
    
    void SetUp() override {
        // Clean state for each test
    }
    
    void TearDown() override {
        // Clean up any allocated handles
        if (hstmt != SQL_NULL_HSTMT) {
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }
        if (hdbc != SQL_NULL_HDBC) {
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, henv);
        }
    }
};

TEST_F(HandleTest, AllocateEnvironment) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_TRUE(henv != nullptr);
}

TEST_F(HandleTest, AllocateEnvironmentWithNullOutput) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, nullptr);
    EXPECT_EQ(ret, SQL_ERROR);
}

TEST_F(HandleTest, SetODBCVersion) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, 
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
}

TEST_F(HandleTest, GetODBCVersion) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3_80)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLINTEGER version = 0;
    ret = SQLGetEnvAttr(henv, SQL_ATTR_ODBC_VERSION, &version, 0, nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(version, SQL_OV_ODBC3_80);
}

TEST_F(HandleTest, AllocateConnection) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_TRUE(hdbc != nullptr);
}

TEST_F(HandleTest, AllocateConnectionWithInvalidEnv) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, SQL_NULL_HANDLE, &hdbc);
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}

TEST_F(HandleTest, FreeEnvironmentWithConnection) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Try to free environment while connection exists - should fail
    ret = SQLFreeHandle(SQL_HANDLE_ENV, henv);
    EXPECT_EQ(ret, SQL_ERROR);
    
    // Free connection first
    ret = SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
    EXPECT_EQ(ret, SQL_SUCCESS);
    hdbc = SQL_NULL_HDBC;
    
    // Now environment can be freed
    ret = SQLFreeHandle(SQL_HANDLE_ENV, henv);
    EXPECT_EQ(ret, SQL_SUCCESS);
    henv = SQL_NULL_HENV;
}

TEST_F(HandleTest, HandleValidation) {
    // Create a valid environment
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Validate the handle
    auto* env = validate_env_handle(henv);
    EXPECT_NE(env, nullptr);
    EXPECT_TRUE(env->is_valid());
    EXPECT_EQ(env->type(), HandleType::ENV);
    
    // Invalid handle test - use NULL instead of arbitrary pointer
    auto* invalid = validate_env_handle(nullptr);
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(HandleTest, DiagnosticRecords) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    auto* env = validate_env_handle(henv);
    ASSERT_NE(env, nullptr);
    
    // Add diagnostic
    env->add_diagnostic("42000", 100, "Test error message");
    EXPECT_EQ(env->diagnostic_count(), 1u);
    
    // Get diagnostic
    const DiagnosticRecord* rec = env->get_diagnostic(1);
    ASSERT_NE(rec, nullptr);
    EXPECT_EQ(rec->sqlstate, "42000");
    EXPECT_EQ(rec->native_error, 100);
    EXPECT_EQ(rec->message, "Test error message");
    
    // Invalid record number
    EXPECT_EQ(env->get_diagnostic(0), nullptr);
    EXPECT_EQ(env->get_diagnostic(2), nullptr);
    
    // Clear diagnostics
    env->clear_diagnostics();
    EXPECT_EQ(env->diagnostic_count(), 0u);
}
