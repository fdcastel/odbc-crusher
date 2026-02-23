#include <gtest/gtest.h>
#include "../src/tests/descriptor_tests.hpp"
#include "../src/core/odbc_environment.hpp"
#include "../src/core/odbc_connection.hpp"

using namespace odbc_crusher;

TEST(DescriptorTests, MockDriverTests) {
    core::OdbcEnvironment env;
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    try {
        conn->connect("Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;");
    } catch (...) {
        GTEST_SKIP() << "Mock ODBC Driver not available";
    }
    
    tests::DescriptorTests tests(*conn);
    auto results = tests.run();
    
    EXPECT_EQ(results.size(), 5);
    
    int passed = 0, failed = 0, errors = 0;
    for (const auto& result : results) {
        if (result.status == tests::TestStatus::PASS) passed++;
        if (result.status == tests::TestStatus::FAIL) failed++;
        if (result.status == tests::TestStatus::ERR) errors++;
        
        std::cout << result.test_name << ": " 
                  << tests::status_to_string(result.status)
                  << " - " << result.actual << std::endl;
    }
    
    EXPECT_GT(passed, 0) << "At least some tests should pass";
    EXPECT_EQ(failed, 0) << "No tests should fail against mock driver";
    EXPECT_EQ(errors, 0) << "No tests should error";
}
