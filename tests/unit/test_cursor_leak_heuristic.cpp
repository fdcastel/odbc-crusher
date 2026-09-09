// H19 — the leak heuristic of `test_rapid_cursor_lifecycle`.
//
// A pure function, so this needs no driver (AGENTS.md: `tests/unit/` is where
// a pure function goes; a probe needs an e2e scenario instead).
//
// What it is defending against is not a wrong answer but an *unstable* one.
// The heuristic used to be a bare `last_10 > first_10 * 10`, and on a macOS CI
// runner it fired at 273 us against a 25 us baseline — ten cycles at 2.5 us
// each, where a single scheduling slice dwarfs the whole measurement. The two
// runs of `CrusherE2EFixture.TwoRunsDifferOnlyInTimings` then disagreed on
// `severity`, `suggestion` and `actual`, none of which that check treats as a
// timing field, and it failed the whole master CI run.
//
// The cases below are the contract: a ratio is only meaningful once both
// samples are large enough to be measurements.

#include "tests/cursor_stress_tests.hpp"

#include <gtest/gtest.h>

using odbc_crusher::tests::cursor_cycle_time_looks_degraded;

// The exact CI failure. Twelvefold on paper, noise in fact.
TEST(CursorLeakHeuristic, MicrosecondNoiseIsNotALeak) {
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(25, 273));
}

// The same ratio, at a scale where it means something.
TEST(CursorLeakHeuristic, RealDegradationAtAMeasurableScaleIsALeak) {
    EXPECT_TRUE(cursor_cycle_time_looks_degraded(1000, 20000));
}

// A baseline below the floor can never trip it, however wild the ratio —
// that is the whole point, and the case a bare ratio got wrong.
TEST(CursorLeakHeuristic, ABaselineTooSmallToDivideBySaysNothing) {
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(1, 1000000));
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(999, 5000000));
}

// Above the baseline floor, growth still has to be worth naming.
TEST(CursorLeakHeuristic, GrowthBelowTheAbsoluteFloorSaysNothing) {
    // 4900 us is more than 10x 1000 us but under the 5000 us floor.
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(1000, 4900));
}

// Both floors met, but the loop did not actually slow down.
TEST(CursorLeakHeuristic, SteadyTimingIsNotALeak) {
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(6000, 6000));
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(6000, 59999));
    EXPECT_TRUE(cursor_cycle_time_looks_degraded(6000, 60001));
}

// A loop that got *faster* is never a leak, and must not underflow the
// comparison.
TEST(CursorLeakHeuristic, FasterAtTheEndIsNotALeak) {
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(50000, 6000));
}

// Zero-length samples happen when every cycle threw: the probe reports that
// through `successful`, and the heuristic must not also claim a leak.
TEST(CursorLeakHeuristic, ZeroSamplesSayNothing) {
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(0, 0));
    EXPECT_FALSE(cursor_cycle_time_looks_degraded(0, 5000000));
}
