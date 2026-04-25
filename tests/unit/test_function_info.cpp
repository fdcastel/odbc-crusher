#include <gtest/gtest.h>
#include "discovery/function_info.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include <cstdlib>
#include <iostream>

using namespace odbc_crusher;

class FunctionInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(FunctionInfoTest, CollectFirebirdFunctions) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "FIREBIRD_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::FunctionInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Should have found some functions
    EXPECT_GT(info.supported_count(), 0);
    std::cout << "Supported: " << info.supported_count() << " functions\n";
    
    // Core functions should be supported
    EXPECT_TRUE(info.is_supported(SQL_API_SQLCONNECT));
    EXPECT_TRUE(info.is_supported(SQL_API_SQLDRIVERCONNECT));
    EXPECT_TRUE(info.is_supported(SQL_API_SQLEXECDIRECT));
    
    // Print summary
    std::cout << info.format_summary() << "\n";
}

TEST_F(FunctionInfoTest, CollectMySQLFunctions) {
    const char* conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "MYSQL_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::FunctionInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    EXPECT_GT(info.supported_count(), 0);
    std::cout << "\n" << info.format_summary() << "\n";
}
