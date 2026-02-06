#include <gtest/gtest.h>
#include "../src/tests/error_queue_tests.hpp"
#include "../src/core/odbc_environment.hpp"
#include "../src/core/odbc_connection.hpp"

using namespace odbc_crusher;

// Helper function to get connection (real or mock)
static std::unique_ptr<core::OdbcConnection> get_connection_or_mock(core::OdbcEnvironment& env) {
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    // Try mock driver with multiple error support
    try {
        conn->connect("Driver={Mock ODBC Driver};Mode=Success;ErrorCount=3;");
        return conn;
    } catch (...) {
        // Mock driver not available, skip
    }
    
    return nullptr;
}

TEST(ErrorQueueTests, MockDriverTests) {
    core::OdbcEnvironment env;
    auto conn = get_connection_or_mock(env);
    
    if (!conn) {
        GTEST_SKIP() << "Mock ODBC Driver not available";
    }
    
    tests::ErrorQueueTests tests(*conn);
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
    
    // Most tests should pass or skip (driver may not support all features)
    EXPECT_GT(passed, 0) << "At least some tests should pass";
    EXPECT_EQ(errors, 0) << "No tests should error";
}

TEST(ErrorQueueTests, RealDriverTests) {
    // This test will use whatever real drivers are available
    core::OdbcEnvironment env;
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    // Try to connect to a real driver (Firebird or MySQL)
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
        tests::ErrorQueueTests tests(*conn);
        auto results = tests.run();
        
        // Verify we have 6 tests
        EXPECT_EQ(results.size(), 6);
        
        // Real drivers should handle errors properly
        for (const auto& result : results) {
            EXPECT_NE(result.status, tests::TestStatus::ERR) 
                << "Test should not error: " << result.test_name;
        }
    }
}
