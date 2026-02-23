#include <gtest/gtest.h>
#include "../src/tests/buffer_validation_tests.hpp"
#include "../src/core/odbc_environment.hpp"
#include "../src/core/odbc_connection.hpp"

using namespace odbc_crusher;

// Helper function to get connection (real or mock)
static std::unique_ptr<core::OdbcConnection> get_connection_or_mock(core::OdbcEnvironment& env) {
    auto conn = std::make_unique<core::OdbcConnection>(env);
    
    // Try mock driver first
    try {
        conn->connect("Driver={Mock ODBC Driver};Mode=Success;BufferValidation=Strict;");
        return conn;
    } catch (...) {
        // Mock driver not available, skip
    }
    
    return nullptr;
}

TEST(BufferValidationTests, MockDriverTests) {
    core::OdbcEnvironment env;
    auto conn = get_connection_or_mock(env);
    
    if (!conn) {
        GTEST_SKIP() << "Mock ODBC Driver not available";
    }
    
    tests::BufferValidationTests tests(*conn);
    auto results = tests.run();
    
    // Verify we have 5 tests
    EXPECT_EQ(results.size(), 5);
    
    // Check test results
    int passed = 0;
    int failed = 0;
    int skipped = 0;
    int errors = 0;
    
    for (const auto& result : results) {
        switch (result.status) {
            case tests::TestStatus::PASS: passed++; break;
            case tests::TestStatus::FAIL: failed++; break;
            case tests::TestStatus::SKIP: skipped++; break;
            case tests::TestStatus::ERR: errors++; break;
        }
        
        // Print result for debugging
        std::cout << result.test_name << ": " 
                  << (result.status == tests::TestStatus::PASS ? "PASS" :
                      result.status == tests::TestStatus::FAIL ? "FAIL" :
                      result.status == tests::TestStatus::SKIP ? "SKIP" : "ERROR")
                  << std::endl;
    }
    
    // All tests should pass with mock driver
    EXPECT_GT(passed, 0) << "At least some tests should pass";
    EXPECT_EQ(failed, 0) << "No tests should fail against mock driver";
    EXPECT_EQ(errors, 0) << "No tests should error";
}

TEST(BufferValidationTests, RealDriverTests) {
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
        tests::BufferValidationTests tests(*conn);
        auto results = tests.run();
        
        // Verify we have 5 tests
        EXPECT_EQ(results.size(), 5);
        
        // Real drivers might not pass all buffer validation tests
        // (that's the point of these tests - to find driver bugs!)
        // So we just verify tests run without crashing
        for (const auto& result : results) {
            EXPECT_NE(result.status, tests::TestStatus::ERR) 
                << "Test should not error: " << result.test_name;
        }
    }
}
