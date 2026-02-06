#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief Cancellation Tests
 * 
 * Tests SQLCancel behavior:
 * - SQLCancel on an idle statement (should succeed)
 * - SQLCancel as state reset (clears cursor)
 */
class CancellationTests : public TestBase {
public:
    explicit CancellationTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Cancellation Tests"; }
    
private:
    TestResult test_cancel_idle();
    TestResult test_cancel_as_reset();
};

} // namespace odbc_crusher::tests
