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
    TestResult test_column_wise_array_binding();
    TestResult test_row_wise_array_binding();
    TestResult test_param_status_array();
    TestResult test_params_processed_count();
    TestResult test_array_with_null_values();
    TestResult test_param_operation_array();
    TestResult test_paramset_size_one();
    TestResult test_array_partial_error();
};

} // namespace odbc_crusher::tests
