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

    // PORT plan §4.9 — cross-state interactions between transaction
    // and cursor lifecycle. Drivers that handle each path correctly in
    // isolation often leak state when both fire at once.
    TestResult test_rollback_with_open_cursor();

    // Stores the last DDL error message for reporting in skip suggestions
    std::string last_ddl_error_;

protected:
    // Table lifecycle. `protected` rather than `private` so a test can
    // exercise the reuse path directly - A15's fix (clearing a table left
    // behind by a crashed run) is otherwise only reachable by crashing a
    // previous run, which is not something a test can arrange.
    bool create_test_table();
    void drop_test_table();
};

} // namespace odbc_crusher::tests
