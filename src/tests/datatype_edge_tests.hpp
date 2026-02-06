#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief Data Type Edge Case Tests (Phase 13.3)
 * 
 * Tests boundary values and edge cases for supported data types:
 * - Integer extremes (INT_MIN, INT_MAX, 0)
 * - String edge cases (empty, special characters)
 * - NULL indicator handling for each C type
 * - Type conversion between ODBC types
 */
class DataTypeEdgeCaseTests : public TestBase {
public:
    explicit DataTypeEdgeCaseTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Data Type Edge Cases"; }
    
private:
    TestResult test_integer_zero();
    TestResult test_integer_max();
    TestResult test_integer_min();
    TestResult test_varchar_empty();
    TestResult test_varchar_special_chars();
    TestResult test_null_integer();
    TestResult test_null_varchar();
    TestResult test_integer_as_string();
    TestResult test_string_as_integer();
    TestResult test_decimal_values();
};

} // namespace odbc_crusher::tests
