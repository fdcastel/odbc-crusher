#include <gtest/gtest.h>
#include "../src/tests/state_machine_tests.hpp"
#include "../src/core/odbc_environment.hpp"
#include "../src/core/odbc_connection.hpp"

using namespace odbc_crusher;

TEST(StateMachineTests, MockDriverTests) {
    core::OdbcEnvironment env;
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    // Try mock driver
    try {
        conn->connect("Driver={Mock ODBC Driver};Mode=Success;StateChecking=Strict;");
    } catch (...) {
        GTEST_SKIP() << "Mock ODBC Driver not available";
    }
    
    tests::StateMachineTests tests(*conn);
    auto results = tests.run();
    
    // Verify we have 6 tests
    EXPECT_EQ(results.size(), 6);
    
    // Check test results
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int errors = 0;
    
    for (const auto& result : results) {
        switch (result.status) {
            case tests::TestStatus::PASS: passed++; break;
            case tests::TestStatus::FAIL: failed++; break;
            case tests::TestStatus::SKIP:
            case tests::TestStatus::SKIP_UNSUPPORTED:
            case tests::TestStatus::SKIP_INCONCLUSIVE: skipped++; break;
            case tests::TestStatus::ERR: errors++; break;
        }
        
        // Print result for debugging
        std::cout << result.test_name << ": " 
                  << tests::status_to_string(result.status)
                  << " - " << result.actual
                  << std::endl;
    }
    
    // Most tests should pass or skip
    EXPECT_GT(passed, 0) << "At least some tests should pass";
    EXPECT_EQ(errors, 0) << "No tests should error";
}

TEST(StateMachineTests, RealDriverTests) {
    core::OdbcEnvironment env;
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    // Try to connect to a real driver
    bool connected = false;
    
    try {
        conn->connect("Driver={Firebird/InterBase(r) driver};Database=test.fdb;Uid=sysdba;Pwd=masterkey;");
        connected = true;
    } catch (...) {
        try {
            conn->connect("Driver={MySQL ODBC 8.0 Driver};Server=localhost;Database=test;Uid=root;Pwd=;");
            connected = true;
        } catch (...) {
            GTEST_SKIP() << "No real ODBC drivers available for testing";
        }
    }
    
    if (connected) {
        tests::StateMachineTests tests(*conn);
        auto results = tests.run();
        
        // Verify we have 6 tests
        EXPECT_EQ(results.size(), 6);
        
        // Real drivers should handle state properly
        for (const auto& result : results) {
            EXPECT_NE(result.status, tests::TestStatus::ERR) 
                << "Test should not error: " << result.test_name;
        }
    }
}
