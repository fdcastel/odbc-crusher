#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Statement-related tests (Phase 4 + Phase 12 extensions)
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
    
    // Phase 12: Column Binding Tests
    TestResult test_bind_col_integer();
    TestResult test_bind_col_string();
    TestResult test_fetch_bound_vs_getdata();
    TestResult test_free_stmt_unbind();
    
    // Phase 12: Row Count & Parameter Tests
    TestResult test_row_count();
    TestResult test_num_params();
    TestResult test_describe_param();
    TestResult test_native_sql();
};

} // namespace odbc_crusher::tests
