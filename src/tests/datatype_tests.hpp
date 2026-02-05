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
};

} // namespace odbc_crusher::tests
