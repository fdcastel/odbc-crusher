#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief SQL_NUMERIC_STRUCT Tests (Phase 26)
 *
 * Tests SQL_C_NUMERIC / SQL_NUMERIC_STRUCT binding and retrieval.
 * Inspired by SQLComponents' TestNumeric.cpp — SQL_NUMERIC_STRUCT
 * handling is one of the most error-prone areas in ODBC drivers.
 */
class NumericStructTests : public TestBase {
public:
    explicit NumericStructTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Numeric Struct Tests"; }

private:
    TestResult test_numeric_struct_binding();
    TestResult test_numeric_struct_precision_scale();
    TestResult test_numeric_positive_negative();
    TestResult test_numeric_zero_and_extremes();
};

} // namespace odbc_crusher::tests
