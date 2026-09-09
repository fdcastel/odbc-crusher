#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Data type tests (Phase 6)
class DataTypeTests : public TestBase {
public:
    explicit DataTypeTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Data Type Tests"; }
    
private:
    TestResult test_integer_types();
    TestResult test_decimal_types();
    TestResult test_float_types();
    TestResult test_string_types();
    TestResult test_date_time_types();
    TestResult test_null_values();
    TestResult test_unicode_types();
    TestResult test_binary_types();
    TestResult test_guid_type();

    // P9 (IMPROVEMENT_PLAN_V2): the *input* path. test_guid_type covers
    // SQLGetData; PR #296 fixed a corruption that only happens when a
    // SQL_C_GUID is bound as a parameter.
    TestResult test_guid_parameter_binding();
    // Note: Interval types are rarely supported, skipping for now
};

} // namespace odbc_crusher::tests
