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
    // Use mock driver - no real database required!
    const char* conn_str = test::get_mock_connection();
    
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

TEST_F(TypeInfoTest, CollectWithDifferentResultSizes) {
    // Test with different result set sizes using mock driver
    std::string conn_str = test::get_mock_connection_with_size(50);
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    
    // Mock driver should return type information
    EXPECT_GT(info.count(), 0) << "Mock driver should return data types via SQLGetTypeInfo";
    std::cout << "Found " << info.count() << " data types with ResultSetSize=50\n";
    
    // Print summary
    std::cout << info.format_summary() << "\n";
}

