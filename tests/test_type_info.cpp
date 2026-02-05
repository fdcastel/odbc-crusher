#include <gtest/gtest.h>
#include "discovery/type_info.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include <cstdlib>
#include <iostream>

using namespace odbc_crusher;

class TypeInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(TypeInfoTest, CollectFirebirdTypes) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "FIREBIRD_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Should have found some types
    EXPECT_GT(info.count(), 0);
    std::cout << "Found " << info.count() << " data types\n";
    
    // Print summary
    std::cout << info.format_summary() << "\n";
}

TEST_F(TypeInfoTest, CollectMySQLTypes) {
    const char* conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "MYSQL_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // MySQL may or may not return types via SQLGetTypeInfo
    // Don't enforce count requirement
    std::cout << "\n" << info.format_summary() << "\n";
    
    if (info.count() > 0) {
        std::cout << "MySQL driver supports SQLGetTypeInfo ✓\n";
    } else {
        std::cout << "MySQL driver does not support SQLGetTypeInfo (expected for some drivers)\n";
    }
}
