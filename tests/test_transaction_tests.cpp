#include <gtest/gtest.h>
#include "tests/transaction_tests.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include <cstdlib>
#include <iostream>

using namespace odbc_crusher;

class TransactionTestsTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }
    
    std::unique_ptr<core::OdbcEnvironment> env;
};

TEST_F(TransactionTestsTest, RunFirebirdTransactionTests) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "FIREBIRD_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    tests::TransactionTests tests(conn);
    auto results = tests.run();
    
    EXPECT_GT(results.size(), 0) << "Should have run some tests";
    
    std::cout << "\n" << tests.category_name() << " - Firebird Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0, failed = 0, skipped = 0, errors = 0;
    
    for (const auto& result : results) {
        std::string status_str;
        switch (result.status) {
            case tests::TestStatus::PASS: status_str = "PASS"; passed++; break;
            case tests::TestStatus::FAIL: status_str = "FAIL"; failed++; break;
            case tests::TestStatus::SKIP: status_str = "SKIP"; skipped++; break;
            case tests::TestStatus::ERR: status_str = "ERROR"; errors++; break;
        }
        
        std::cout << "[" << status_str << "] " << result.test_name 
                  << " - " << result.actual << " (" << result.duration.count() << " us)\n";
    }
    
    std::cout << "\nSummary: " << passed << " passed, " << failed << " failed, "
              << skipped << " skipped, " << errors << " errors\n\n";
    
    EXPECT_GT(passed + skipped, 0);
}

TEST_F(TransactionTestsTest, RunMySQLTransactionTests) {
    const char* conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "MYSQL_ODBC_CONNECTION not set";
    }
    
    core::OdbcConnection conn(*env);
    conn.connect(conn_str);
    
    tests::TransactionTests tests(conn);
    auto results = tests.run();
    
    EXPECT_GT(results.size(), 0);
    
    std::cout << "\n" << tests.category_name() << " - MySQL Results:\n";
    std::cout << "================================\n";
    
    size_t passed = 0, skipped = 0;
    for (const auto& result : results) {
        if (result.status == tests::TestStatus::PASS) passed++;
        else if (result.status == tests::TestStatus::SKIP) skipped++;
        
        std::string status_str;
        switch (result.status) {
            case tests::TestStatus::PASS: status_str = "PASS"; break;
            case tests::TestStatus::FAIL: status_str = "FAIL"; break;
            case tests::TestStatus::SKIP: status_str = "SKIP"; break;
            case tests::TestStatus::ERR: status_str = "ERROR"; break;
        }
        
        std::cout << "[" << status_str << "] " << result.test_name 
                  << " - " << result.actual << " (" << result.duration.count() << " us)\n";
    }
    
    std::cout << "\n";
    EXPECT_GT(passed + skipped, 0);
}
