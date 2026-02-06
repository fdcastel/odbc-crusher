#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief Boundary Value Tests (Phase 13.2)
 * 
 * Tests edge cases with buffer sizes, null pointers, and extreme values
 * to verify the driver handles boundary conditions correctly without
 * crashing or returning incorrect data.
 */
class BoundaryTests : public TestBase {
public:
    explicit BoundaryTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Boundary Value Tests"; }
    
private:
    TestResult test_getinfo_zero_buffer();
    TestResult test_getdata_zero_buffer();
    TestResult test_bindparam_null_value_with_null_indicator();
    TestResult test_execdirect_empty_sql();
    TestResult test_describecol_col0();
};

} // namespace odbc_crusher::tests
