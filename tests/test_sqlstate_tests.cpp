#include <gtest/gtest.h>
#include "../src/tests/sqlstate_tests.hpp"
#include "../src/core/odbc_environment.hpp"
#include "../src/core/odbc_connection.hpp"

using namespace odbc_crusher;

TEST(SqlstateTests, MockDriverTests) {
    core::OdbcEnvironment env;
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    try {
        conn->connect("Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;");
    } catch (...) {
        GTEST_SKIP() << "Mock ODBC Driver not available";
    }
    
    tests::SqlstateTests tests(*conn);
    auto results = tests.run();
    
    EXPECT_EQ(results.size(), 10);
    
    int passed = 0, errors = 0;
    for (const auto& result : results) {
        if (result.status == tests::TestStatus::PASS) passed++;
        if (result.status == tests::TestStatus::ERR) errors++;
        
        std::cout << result.test_name << ": " 
                  << tests::status_to_string(result.status)
                  << " - " << result.actual << std::endl;
    }
    
    EXPECT_GT(passed, 0) << "At least some SQLSTATE tests should pass";
    EXPECT_EQ(errors, 0) << "No SQLSTATE tests should error";
}
