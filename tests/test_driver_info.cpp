#include <gtest/gtest.h>
#include "discovery/driver_info.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include <cstdlib>
#include <iostream>

using namespace odbc_crusher;

class DriverInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(DriverInfoTest, CollectFirebirdInfo) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "FIREBIRD_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::DriverInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Driver should have a name
    EXPECT_TRUE(info.driver_name().has_value());
    std::cout << "Driver: " << *info.driver_name() << "\n";
    
    // DBMS should have a name
    EXPECT_TRUE(info.dbms_name().has_value());
    std::cout << "DBMS: " << *info.dbms_name() << "\n";
    
    // Should have version info
    EXPECT_TRUE(info.driver_version().has_value());
    EXPECT_TRUE(info.dbms_version().has_value());
    
    // Print summary
    std::cout << "\n" << info.format_summary() << "\n";
}

TEST_F(DriverInfoTest, CollectMySQLInfo) {
    const char* conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "MYSQL_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::DriverInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    EXPECT_TRUE(info.driver_name().has_value());
    EXPECT_TRUE(info.dbms_name().has_value());
    
    std::cout << "\n" << info.format_summary() << "\n";
}
