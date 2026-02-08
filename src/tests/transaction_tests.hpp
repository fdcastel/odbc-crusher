#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Transaction tests (Phase 8)
class TransactionTests : public TestBase {
public:
    explicit TransactionTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Transaction Tests"; }
    
private:
    TestResult test_autocommit_on();
    TestResult test_autocommit_off();
    TestResult test_manual_commit();
    TestResult test_manual_rollback();
    TestResult test_transaction_isolation_levels();
    
    // Helper to create test table
    bool create_test_table();
    void drop_test_table();
    
    // Stores the last DDL error message for reporting in skip suggestions
    std::string last_ddl_error_;
};

} // namespace odbc_crusher::tests
