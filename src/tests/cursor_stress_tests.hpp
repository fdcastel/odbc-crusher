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

    // PORT plan §4.5 — phase-separated open/close timing. The existing
    // rapid-lifecycle probe times whole iterations; this one breaks the
    // open-phase (alloc + execute) from the close-phase
    // (SQLCloseCursor + SQLFreeHandle) so drivers that re-fetch
    // remaining rows on close show up as outliers. FAIL when close mean
    // exceeds 10× open mean — that's a real driver footgun.
    TestResult test_open_close_hammer_loop();

    // PORT plan §4.5 — same statement handle reused N times across
    // execute/close cycles. Verifies no diagnostic-queue accumulation
    // and no need to re-allocate the handle. Detects drivers that leak
    // state into the next reuse.
    TestResult test_handle_reuse_no_leak();
};

} // namespace odbc_crusher::tests
