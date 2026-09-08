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
    // C8: the three integer-literal probes and the two NULL
    // probes are one shape each, driven from a table in the .cpp.
    // `const void*` keeps the row types out of this header - they
    // are an implementation detail of the file that owns them.
    TestResult run_integer_edge_case(const void* row);
    TestResult run_null_edge_case(const void* row);

    TestResult test_varchar_empty();
    TestResult test_varchar_special_chars();
    TestResult test_integer_as_string();
    TestResult test_string_as_integer();
    TestResult test_decimal_values();
    TestResult test_varchar_raw_byte_integrity();

    // PORT plan §4.2 — NULL-vs-non-NULL contrast probes. The existing
    // test_null_{integer,varchar} only checks NULL alone; drivers that
    // conflate empty/zero with NULL pass those individually but FAIL these.
    TestResult test_null_vs_empty_distinction_varchar();
    TestResult test_null_vs_zero_distinction_integer();
    TestResult test_null_in_numeric_struct();
};

} // namespace odbc_crusher::tests
