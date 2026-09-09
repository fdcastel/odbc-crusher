#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// H19: the leak heuristic of test_rapid_cursor_lifecycle, as a free
// function so `tests/unit` can pin its thresholds without a driver.
//
// It compares ten iterations at the end of a 100-cycle loop against ten
// at the start. A bare ratio there is noise: on a macOS CI runner the
// baseline was 25 us for all ten cycles — 2.5 us each, less than one
// scheduling slice — so a hiccup tripped a "possible leak" warning and
// two identical runs disagreed on `severity`, `suggestion` and `actual`.
// That is the one thing the determinism e2e check exists to forbid, and a
// verdict that moves between runs is a defect however it was derived.
//
// Both floors are deliberately generous. Against a driver fast enough
// that a cycle costs microseconds this now says nothing about leaks,
// which is the honest answer: a real leak over 100 cycles moves absolute
// time by far more than this.
bool cursor_cycle_time_looks_degraded(long long first_10_us,
                                      long long last_10_us);

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
