#include <gtest/gtest.h>
#include "tests/escape_sequence_tests.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "mock_connection.hpp"
#include <iostream>

using namespace odbc_crusher;

class EscapeSequenceTestsTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(EscapeSequenceTestsTest, MockDriverTests) {
    std::string conn_str = test::get_connection_or_mock("FIREBIRD_ODBC_CONNECTION", "Mock");
    
    core::OdbcConnection conn(*env);
    try {
        conn.connect(conn_str);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Could not connect: " << e.what();
    }
    
    tests::EscapeSequenceTests test_suite(conn);
    auto results = test_suite.run();
    
    EXPECT_GT(results.size(), 0) << "Should have run some tests";
    
    std::cout << "\n" << test_suite.category_name() << " Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0, failed = 0, skipped = 0, errors = 0;
    for (const auto& r : results) {
        const char* s = tests::status_to_string(r.status);
        std::cout << "[" << s << "] " << r.test_name << ": " << r.actual << "\n";
        switch (r.status) {
            case tests::TestStatus::PASS: passed++; break;
            case tests::TestStatus::FAIL: failed++; break;
            case tests::TestStatus::SKIP:
            case tests::TestStatus::SKIP_UNSUPPORTED:
            case tests::TestStatus::SKIP_INCONCLUSIVE: skipped++; break;
            case tests::TestStatus::ERR: errors++; break;
        }
    }
    
    std::cout << "\nPassed: " << passed << ", Failed: " << failed
              << ", Skipped: " << skipped << ", Errors: " << errors << "\n";
    EXPECT_EQ(errors, 0) << "No errors should occur";
    EXPECT_GT(passed, 0) << "At least some escape sequence tests should pass";
}
