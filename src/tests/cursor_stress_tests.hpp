#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief Cursor Stress Tests (Phase 26)
 *
 * Inspired by SQLComponents' TestClosingCursor.cpp which runs 1500
 * sequential SELECT+close cycles. Tests for handle leaks, cursor
 * exhaustion, and performance degradation.
 */
class CursorStressTests : public TestBase {
public:
    explicit CursorStressTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Cursor Stress Tests"; }

private:
    TestResult test_rapid_cursor_lifecycle();
    TestResult test_concurrent_statements();
};

} // namespace odbc_crusher::tests
