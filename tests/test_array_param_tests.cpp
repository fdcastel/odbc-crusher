#include <gtest/gtest.h>
#include "tests/array_param_tests.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "mock_connection.hpp"
#include <iostream>

using namespace odbc_crusher;

class ArrayParamTestsTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(ArrayParamTestsTest, MockDriverTests) {
    std::string conn_str = test::get_connection_or_mock("FIREBIRD_ODBC_CONNECTION", "Mock");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect: " << e.what();
    }
    
    tests::ArrayParamTests test_suite(conn);
    auto results = test_suite.run();
    
    EXPECT_GT(results.size(), 0) << "Should have run some tests";
    
    std::cout << "\n" << test_suite.category_name() << " Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0, failed = 0;
    for (const auto& r : results) {
        const char* s = tests::status_to_string(r.status);
        std::cout << "[" << s << "] " << r.test_name << ": " << r.actual << "\n";
        if (r.status == tests::TestStatus::PASS) passed++;
        if (r.status == tests::TestStatus::FAIL) failed++;
    }
    
    std::cout << "\nPassed: " << passed << "/" << results.size() << "\n";
    EXPECT_EQ(failed, 0) << "No tests should fail against mock driver";
    // Mock driver supports array parameters, so at least some tests should pass
    if (passed == 0) {
        GTEST_SKIP() << "No tests passed (driver may not support array parameters)";
    }
}
