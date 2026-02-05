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

// ===== Phase 1: Comprehensive Handle Management Tests =====

extern "C" {
    SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT, SQLHANDLE, SQLSMALLINT,
                                     SQLCHAR*, SQLINTEGER*, SQLCHAR*, 
                                     SQLSMALLINT, SQLSMALLINT*);
    SQLRETURN SQL_API SQLGetDiagField(SQLSMALLINT, SQLHANDLE, SQLSMALLINT,
                                       SQLSMALLINT, SQLPOINTER, SQLSMALLINT,
                                       SQLSMALLINT*);
    SQLRETURN SQL_API SQLDriverConnect(SQLHDBC, SQLHWND, SQLCHAR*, SQLSMALLINT,
                                        SQLCHAR*, SQLSMALLINT, SQLSMALLINT*, SQLUSMALLINT);
    SQLRETURN SQL_API SQLDisconnect(SQLHDBC);
}

// Multiple connection handles test
TEST_F(HandleTest, MultipleConnections) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Allocate multiple connections
    SQLHDBC hdbc1 = SQL_NULL_HDBC, hdbc2 = SQL_NULL_HDBC, hdbc3 = SQL_NULL_HDBC;
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc1);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc2);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc3);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    // Verify all are valid and different
    EXPECT_NE(hdbc1, hdbc2);
    EXPECT_NE(hdbc2, hdbc3);
    EXPECT_NE(hdbc1, hdbc3);
    
    auto* conn1 = validate_dbc_handle(hdbc1);
    auto* conn2 = validate_dbc_handle(hdbc2);
    auto* conn3 = validate_dbc_handle(hdbc3);
    
    EXPECT_NE(conn1, nullptr);
    EXPECT_NE(conn2, nullptr);
    EXPECT_NE(conn3, nullptr);
    
    // Clean up
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc3);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc2);
    SQLFreeHandle(SQL_HANDLE_DBC, hdbc1);
}

// All environment attributes test
TEST_F(HandleTest, AllEnvironmentAttributes) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Test SQL_ATTR_ODBC_VERSION
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    // Test SQL_ATTR_CONNECTION_POOLING
    ret = SQLSetEnvAttr(henv, SQL_ATTR_CONNECTION_POOLING,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_CP_ONE_PER_HENV)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLINTEGER pooling = 0;
    ret = SQLGetEnvAttr(henv, SQL_ATTR_CONNECTION_POOLING, &pooling, 0, nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(pooling, SQL_CP_ONE_PER_HENV);
    
    // Test SQL_ATTR_CP_MATCH
    ret = SQLSetEnvAttr(henv, SQL_ATTR_CP_MATCH,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_CP_RELAXED_MATCH)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLINTEGER match = 0;
    ret = SQLGetEnvAttr(henv, SQL_ATTR_CP_MATCH, &match, 0, nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(match, SQL_CP_RELAXED_MATCH);
    
    // Test SQL_ATTR_OUTPUT_NTS
    ret = SQLSetEnvAttr(henv, SQL_ATTR_OUTPUT_NTS,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_TRUE)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLINTEGER nts = 0;
    ret = SQLGetEnvAttr(henv, SQL_ATTR_OUTPUT_NTS, &nts, 0, nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(nts, SQL_TRUE);
}

// SQLGetDiagRec test
TEST_F(HandleTest, GetDiagRecTest) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    auto* env = validate_env_handle(henv);
    ASSERT_NE(env, nullptr);
    
    // Add a diagnostic record
    env->add_diagnostic("HY000", 42, "General error occurred");
    
    SQLCHAR sqlstate[6] = {0};
    SQLINTEGER native_error = 0;
    SQLCHAR message[256] = {0};
    SQLSMALLINT message_len = 0;
    
    ret = SQLGetDiagRec(SQL_HANDLE_ENV, henv, 1, sqlstate, &native_error,
                        message, sizeof(message), &message_len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(sqlstate), "HY000");
    EXPECT_EQ(native_error, 42);
    EXPECT_STREQ(reinterpret_cast<char*>(message), "General error occurred");
    EXPECT_EQ(message_len, 22);
    
    // No second record
    ret = SQLGetDiagRec(SQL_HANDLE_ENV, henv, 2, sqlstate, &native_error,
                        message, sizeof(message), &message_len);
    EXPECT_EQ(ret, SQL_NO_DATA);
}

// SQLGetDiagField test
TEST_F(HandleTest, GetDiagFieldTest) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    auto* env = validate_env_handle(henv);
    ASSERT_NE(env, nullptr);
    
    // Add two diagnostic records
    env->add_diagnostic("42S02", 1001, "Table not found");
    env->add_diagnostic("01000", 0, "Warning message");
    
    // Get header field - number of records
    SQLINTEGER num_recs = 0;
    ret = SQLGetDiagField(SQL_HANDLE_ENV, henv, 0, SQL_DIAG_NUMBER,
                          &num_recs, sizeof(num_recs), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(num_recs, 2);
    
    // Get record field - SQLSTATE
    SQLCHAR sqlstate[6] = {0};
    ret = SQLGetDiagField(SQL_HANDLE_ENV, henv, 1, SQL_DIAG_SQLSTATE,
                          sqlstate, sizeof(sqlstate), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(sqlstate), "42S02");
    
    // Get native error
    SQLINTEGER native = 0;
    ret = SQLGetDiagField(SQL_HANDLE_ENV, henv, 1, SQL_DIAG_NATIVE,
                          &native, sizeof(native), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(native, 1001);
    
    // Get second record SQLSTATE
    ret = SQLGetDiagField(SQL_HANDLE_ENV, henv, 2, SQL_DIAG_SQLSTATE,
                          sqlstate, sizeof(sqlstate), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(sqlstate), "01000");
}

// Descriptor handle allocation test
TEST_F(HandleTest, DescriptorHandleAllocation) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Allocate descriptor
    SQLHDESC hdesc = SQL_NULL_HDESC;
    ret = SQLAllocHandle(SQL_HANDLE_DESC, hdbc, &hdesc);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_TRUE(hdesc != nullptr);
    
    auto* desc = validate_desc_handle(hdesc);
    EXPECT_NE(desc, nullptr);
    EXPECT_TRUE(desc->is_valid());
    EXPECT_EQ(desc->type(), HandleType::DESC);
    
    // Free descriptor
    ret = SQLFreeHandle(SQL_HANDLE_DESC, hdesc);
    EXPECT_EQ(ret, SQL_SUCCESS);
}

// Handle type checking test
TEST_F(HandleTest, HandleTypeMismatch) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Try to use env handle where connection expected
    auto* bad_conn = validate_dbc_handle(henv);
    EXPECT_EQ(bad_conn, nullptr);
    
    // Try to use connection handle where env expected
    auto* bad_env = validate_env_handle(hdbc);
    EXPECT_EQ(bad_env, nullptr);
}

// Invalid handle type test
TEST_F(HandleTest, InvalidHandleType) {
    SQLRETURN ret = SQLAllocHandle(999, SQL_NULL_HANDLE, &henv);
    EXPECT_EQ(ret, SQL_ERROR);
    
    ret = SQLFreeHandle(999, SQL_NULL_HANDLE);
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}
