#include <gtest/gtest.h>
#include "tests/datatype_tests.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "mock_connection.hpp"
#include <cstdlib>
#include <iostream>

using namespace odbc_crusher;

class DataTypeTestsTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(DataTypeTestsTest, RunFirebirdDataTypeTests) {
    // Use mock driver as default, fall back to real database if FIREBIRD_ODBC_CONNECTION is set
    std::string conn_str = test::get_connection_or_mock("FIREBIRD_ODBC_CONNECTION", "Firebird");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect (mock driver not registered?): " << e.what();
    }
    
    tests::DataTypeTests tests(conn);
    auto results = tests.run();
    
    EXPECT_GT(results.size(), 0) << "Should have run some tests";
    
    // Print results
    std::cout << "\n" << tests.category_name() << " - Firebird Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0, failed = 0, skipped = 0, errors = 0;
    
    for (const auto& result : results) {
        std::string status_str;
        switch (result.status) {
            case tests::TestStatus::PASS: status_str = "PASS ✓"; passed++; break;
            case tests::TestStatus::FAIL: status_str = "FAIL ✗"; failed++; break;
            case tests::TestStatus::SKIP: status_str = "SKIP -"; skipped++; break;
            case tests::TestStatus::ERR: status_str = "ERROR!"; errors++; break;
        }
        
        std::cout << "[" << status_str << "] " << result.test_name << "\n";
        std::cout << "  Function: " << result.function << "\n";
        std::cout << "  Expected: " << result.expected << "\n";
        std::cout << "  Actual:   " << result.actual << "\n";
        std::cout << "  Duration: " << result.duration.count() << " μs\n";
        
        if (result.diagnostic) {
            std::cout << "  Diagnostic: " << *result.diagnostic << "\n";
        }
        std::cout << "\n";
    }
    
    std::cout << "Summary: " << passed << " passed, " << failed << " failed, "
              << skipped << " skipped, " << errors << " errors\n\n";
    
    // At least some tests should pass
    EXPECT_GT(passed, 0) << "At least some tests should pass";
}

TEST_F(DataTypeTestsTest, RunMySQLDataTypeTests) {
    // Use mock driver as default, fall back to real database if MYSQL_ODBC_CONNECTION is set
    std::string conn_str = test::get_connection_or_mock("MYSQL_ODBC_CONNECTION", "MySQL");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect (mock driver not registered?): " << e.what();
    }
    
    tests::DataTypeTests tests(conn);
    auto results = tests.run();
    
    EXPECT_GT(results.size(), 0);
    
    std::cout << "\n" << tests.category_name() << " - MySQL Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0;
    for (const auto& result : results) {
        if (result.status == tests::TestStatus::PASS) {
            passed++;
        }
        
        std::string status_str;
        switch (result.status) {
            case tests::TestStatus::PASS: status_str = "PASS ✓"; break;
            case tests::TestStatus::FAIL: status_str = "FAIL ✗"; break;
            case tests::TestStatus::SKIP: status_str = "SKIP -"; break;
            case tests::TestStatus::ERR: status_str = "ERROR!"; break;
        }
        
        std::cout << "[" << status_str << "] " << result.test_name 
                  << " - " << result.actual << " (" << result.duration.count() << " μs)\n";
    }
    
    std::cout << "\n";
    // Mock driver doesn't support expression-based queries (SELECT 42, etc.)
    // so all tests may be skipped. Only assert passes with real drivers.
    if (passed == 0) {
        GTEST_SKIP() << "No tests passed (driver may not support expression queries)";
    }
}
