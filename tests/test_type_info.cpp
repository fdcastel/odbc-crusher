#include <gtest/gtest.h>
#include "discovery/type_info.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "mock_connection.hpp"
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

TEST_F(TypeInfoTest, CollectMockDriverTypes) {
    // Use mock driver as default, fall back to real database if FIREBIRD_ODBC_CONNECTION is set
    std::string conn_str = test::get_connection_or_mock("FIREBIRD_ODBC_CONNECTION", "Firebird");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect (mock driver not registered?): " << e.what();
    }
    
    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Should have found some types
    EXPECT_GT(info.count(), 0);
    std::cout << "Found " << info.count() << " data types\n";
    
    // Print summary
    std::cout << info.format_summary() << "\n";
}

TEST_F(TypeInfoTest, CollectWithDifferentResultSizes) {
    // Use mock driver as default, fall back to real database if MYSQL_ODBC_CONNECTION is set
    std::string conn_str = test::get_connection_or_mock("MYSQL_ODBC_CONNECTION", "MySQL");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect (mock driver not registered?): " << e.what();
    }
    
    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Driver should return type information
    EXPECT_GT(info.count(), 0) << "Driver should return data types via SQLGetTypeInfo";
    std::cout << "Found " << info.count() << " data types\n";
    
    // Print summary
    std::cout << info.format_summary() << "\n";
}

