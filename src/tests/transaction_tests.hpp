#pragma once

#include "test_base.hpp"

#include <optional>

namespace odbc_crusher::tests {

// Transaction tests (Phase 8)
class TransactionTests : public TestBase {
public:
    // C13: this category takes the connection string, because
    // test_disconnect_rolls_back_open_transaction needs a connection it is
    // allowed to disconnect. Declaring the two-argument constructor is how a
    // category asks for one.
    explicit TransactionTests(core::OdbcConnection& conn,
                              const std::string& connection_string = {})
        : TestBase(conn, connection_string) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Transaction Tests"; }
    
private:
    TestResult test_autocommit_on();
    TestResult test_autocommit_off();
    TestResult test_manual_commit();
    TestResult test_manual_rollback();
    TestResult test_disconnect_rolls_back_open_transaction();   // I8
    TestResult test_uncommitted_row_isolation();                // I6
    TestResult test_transaction_isolation_levels();

    // PORT plan §4.9 — cross-state interactions between transaction
    // and cursor lifecycle. Drivers that handle each path correctly in
    // isolation often leak state when both fire at once.
    TestResult test_rollback_with_open_cursor();

    // Stores the last DDL error message for reporting in skip suggestions
    std::string last_ddl_error_;

    // C4: the table's lifetime. RoundTripTableGuard is RAII, but these probes
    // create and drop it across several functions, so it is held here and
    // drop_test_table() resets it.
    std::optional<RoundTripTableGuard> table_;

protected:
    // Table lifecycle. `protected` rather than `private` so a test can
    // exercise the reuse path directly - A15's fix (clearing a table left
    // behind by a crashed run) is otherwise only reachable by crashing a
    // previous run, which is not something a test can arrange.
    bool create_test_table();
    void drop_test_table();
};

} // namespace odbc_crusher::tests
