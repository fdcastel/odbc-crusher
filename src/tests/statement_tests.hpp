#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Statement-related tests (Phase 4)
class StatementTests : public TestBase {
public:
    explicit StatementTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Statement Tests"; }
    
private:
    TestResult test_simple_query();
    TestResult test_prepared_statement();
    TestResult test_parameter_binding();
    TestResult test_result_fetching();
    TestResult test_column_metadata();
    TestResult test_statement_reuse();
    TestResult test_multiple_result_sets();
};

} // namespace odbc_crusher::tests
