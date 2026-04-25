#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Array Parameter Tests (Phase 16)
// Tests ODBC "Arrays of Parameter Values" feature — binding arrays of values
// to parameter markers and executing a statement for multiple parameter sets.
class ArrayParamTests : public TestBase {
public:
    explicit ArrayParamTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Array Parameter Tests"; }
    
private:
    // Table lifecycle — creates ODBC_TEST_ARRAY with autocommit ON, drops on cleanup
    bool create_test_table();
    void drop_test_table();
    
    // Stores the last DDL error message for reporting in skip suggestions
    std::string last_ddl_error_;
    
    TestResult test_column_wise_array_binding();
    TestResult test_row_wise_array_binding();
    TestResult test_param_status_array();
    TestResult test_params_processed_count();
    TestResult test_array_with_null_values();
    TestResult test_param_operation_array();
    TestResult test_paramset_size_one();
    TestResult test_array_partial_error();

    // PORT plan §4.6 — driver-detected per-row failure (mid-batch constraint
    // violation), and the SQL_ATTR_PARAMSET_SIZE-unsupported fallback path.
    // The existing partial-error test uses SQL_PARAM_OPERATION_PTR with
    // SQL_PARAM_IGNORE (application-driven skip); these probes exercise the
    // driver-driven failure shape that's harder to detect.
    TestResult test_param_status_per_row_partial_failure();
    TestResult test_paramset_size_unsupported_returns_error();
};

} // namespace odbc_crusher::tests
