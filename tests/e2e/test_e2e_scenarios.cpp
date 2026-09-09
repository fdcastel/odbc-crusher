// E2E scenarios — IMPROVEMENT_PLAN.md §5.1
//
// Each scenario maps a mock-driver configuration to expected per-test
// outcomes in the JSON report. The harness lives in e2e_harness.{hpp,cpp}.
// Tests SKIP gracefully when the mock driver isn't loadable on the host
// — an environment problem, not a regression to flag.
#include <cstdlib>
#include <gtest/gtest.h>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include "e2e_harness.hpp"

using namespace odbc_crusher::e2e;

// D63: what a report actually held, for the message on a lookup that missed.
//
// `ASSERT_TRUE(t.has_value())` prints "Actual: false" and stops there, so a
// probe absent from the report is indistinguishable from a probe present and
// wrong - and from a run the harness killed. The categories, their sizes and
// the run's own stderr are all to hand; this puts them in the failure.
// D75: was this run killed by a signal before it could finish?
//
// A process killed by SIGKILL leaves the last atomic snapshot the reporter
// wrote - so there are categories and no summary - and exits 128+signal. That
// is not a result a scenario can grade: no probe reached a verdict.
//
// On macOS under BufferValidation=Lenient this is what happens, intermittently
// and early: `Killed: 9`, exit 137, one category written. SIGKILL cannot be
// handled, so D73's widened signal set does not help and nothing in the tool
// can. It is D71's fault wearing the platform's other ending.
//
// `timed_out` is checked because the harness's own watchdog (D55) also kills
// with SIGKILL, and that *is* a result worth failing on - a wedged crusher is
// the tool's problem.
std::optional<std::string> killed_before_finishing(const CrusherRun& run) {
    if (run.timed_out) return std::nullopt;           // the harness did it
    if (run.report.is_object() && run.report.contains("summary")) {
        return std::nullopt;                          // it finished
    }
    if (run.exit_code <= 128 || run.exit_code >= 160) return std::nullopt;
    return "killed by signal " + std::to_string(run.exit_code - 128);
}

// D75: a run this scenario can grade, or a skip that says why not.
//
// The macro is what makes the check unmissable: it expands to a GTEST_SKIP,
// which must return from the calling function, so it cannot be written as a
// helper that returns a bool and gets ignored. Every run_crusher call in a
// BufferValidation=Lenient scenario goes through it.
#define SKIP_IF_KILLED(run)                                                   \
    do {                                                                      \
        if (auto killed__ = killed_before_finishing(run)) {                   \
            GTEST_SKIP() << "D75: crusher was " << *killed__                   \
                         << " under BufferValidation=Lenient, so no probe "   \
                            "reached a verdict to grade. D71's fault in the " \
                            "form macOS gives it; these assertions still run "\
                            "on every platform where the driver manager "     \
                            "survives.\n"                                     \
                         << report_outline(run);                              \
        }                                                                     \
    } while (false)

// D79: did this particular category fault?
//
// `crashed_category` answers "did anything fault", which is what D72 needed.
// This answers it per category, because a probe missing from a category that
// crashed is D63's vanishing probe, while a probe missing from one that did
// not is a name that has drifted - and those want different verdicts.
bool category_crashed(const CrusherRun& run, const std::string& category) {
    if (!run.report.is_object() || !run.report.contains("categories")) {
        return false;
    }
    for (const auto& cat : run.report["categories"]) {
        if (cat.value("name", std::string{}) != category) continue;
        if (!cat.contains("tests")) return false;
        for (const auto& t : cat["tests"]) {
            if (t.value("test_name", std::string{}).find("(DRIVER CRASH)")
                != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}

// D71: did a category fault during this run?
//
// main.cpp replaces a faulting category with a single "<name> (DRIVER CRASH)"
// entry (D63), so this is how a scenario asks whether what it is about to
// assert is still about the tool. Under BufferValidation=Lenient on Linux the
// answer is yes: unixODBC takes a wild write inside SQLExecDirect, and after
// that the process's exit status belongs to the sanitizer rather than to any
// flag the scenario passed.
std::optional<std::string> crashed_category(const CrusherRun& run) {
    if (!run.report.contains("categories")) return std::nullopt;
    for (const auto& cat : run.report["categories"]) {
        if (!cat.contains("tests")) continue;
        for (const auto& t : cat["tests"]) {
            const auto name = t.value("test_name", std::string{});
            if (name.find("(DRIVER CRASH)") != std::string::npos) {
                return name + " - " + t.value("actual", std::string{});
            }
        }
    }
    return std::nullopt;
}

std::string report_outline(const CrusherRun& run) {
    std::ostringstream out;
    // D74: `contains` first, and never `operator[]` on a const json that may
    // not be an object. When crusher writes no report at all `run.report` is
    // null, and nlohmann's const operator[] asserts on that - so the helper
    // built to explain a missing report aborted the test binary whenever the
    // report was missing. Caught by three macOS scenarios turning from
    // `Failed` into `Subprocess aborted` in the first CI run after D74.
    if (run.report.is_object() && run.report.contains("summary")) {
        const auto& summary = run.report["summary"];
        out << "summary: total=" << summary.value("total_tests", -1)
            << " passed=" << summary.value("passed", -1)
            << " failed=" << summary.value("failed", -1)
            << " skipped=" << summary.value("skipped", -1)
            << " errors=" << summary.value("errors", -1) << "\n";
    } else {
        out << "no summary in the report\n";
    }
    // D74: `value()` throws on a non-object as surely as `operator[]` does,
    // so guarding only the line above would have moved the abort down two.
    const bool complete = run.report.is_object() &&
                          run.report.value("complete", false);
    out << "complete=" << (complete ? "true" : "false")
        << " timed_out=" << (run.timed_out ? "true" : "false")
        << " exit_code=" << run.exit_code << "\n";
    if (run.report.contains("categories")) {
        out << "categories:";
        for (const auto& cat : run.report["categories"]) {
            out << " " << cat.value("name", std::string{"?"}) << "("
                << (cat.contains("tests") ? cat["tests"].size() : 0) << ")";
        }
        out << "\n";
    } else {
        out << "no categories key at all\n";
    }
    if (!run.raw_stderr.empty()) {
        out << "stderr: " << run.raw_stderr.substr(0, 2000) << "\n";
    }
    return out.str();
}

namespace {

class CrusherE2EFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_runnable_mock()) {
            // D51: skipping is right on a developer's machine, where the
            // driver may simply not be registered. In CI it is not: the whole
            // §5.1 harness exists to catch e2e regressions, and a driver that
            // has stopped loading is the largest regression there is. When
            // D2's first attempt made the .so export nothing, all 30
            // scenarios skipped and the job still reported success - the gate
            // went quiet at exactly the moment it mattered.
            //
            // CI sets ODBC_CRUSHER_REQUIRE_MOCK=1, which turns the skip into
            // the failure it should have been.
            const char* required = std::getenv("ODBC_CRUSHER_REQUIRE_MOCK");
            if (required && *required && std::string(required) != "0") {
                FAIL() << "Mock ODBC Driver is not loadable, and "
                          "ODBC_CRUSHER_REQUIRE_MOCK is set. In CI the driver "
                          "is registered before this runs, so this means the "
                          "driver stopped loading - check the export surface "
                          "and the registration step.";
            }
            GTEST_SKIP() << "Mock ODBC Driver not loadable on this host — "
                            "register mock-driver/build/<config>/mockodbc.dll first.";
        }
    }
};

// Why a fault-injection contrast cannot be judged on this platform, or
// nullopt when it can.
//
// Several scenarios below assert "PASS at Mode=Success, FAIL under that
// configuration". Where the probe does not reach its assertion in the first
// place - Linux still carries the IMPROVEMENT_PLAN section 8 mock/unixODBC
// gaps, tracked as D1 and I1-I5 - the contrast proves nothing, so those
// scenarios skip rather than fail. When Phase 6 closes the gap they begin
// asserting there automatically, with no edit here.
std::optional<std::string> baseline_blocker(const nlohmann::json& report,
                                            const std::string& category,
                                            const std::string& probe) {
    auto b = find_test(report, category, probe);
    if (!b.has_value()) {
        return "probe " + probe + " is missing from category " + category +
               " - was it renamed?";
    }
    const auto status = b->value("status", std::string{});
    if (status != "PASS") {
        return probe + " is " + status + " at Mode=Success on this platform, "
               "so the fault-injection contrast proves nothing "
               "(IMPROVEMENT_PLAN D1 / I1-I5).";
    }
    return std::nullopt;
}

} // namespace

// ── Mode=Success: baseline run produces a coherent report ──────────────────
//
// The mock at Mode=Success is the project's reference fixture — every probe
// in the conformance suite must PASS against it. A FAIL/ERROR/SKIP here is
// either (a) a real mock regression, or (b) a probe that started exercising
// a mock code path the fixture doesn't yet cover. Either way it's a build
// signal, not a "soft" report finding (§7.10: this canary is what should
// have caught the §7.7-§7.9 regressions on day one).

TEST_F(CrusherE2EFixture, ModeSuccessProducesCoherentReport) {
    // P10: `Database=`, not `Catalog=`, though they are the same key. The
    // baseline exists to show that every probe can be *graded* against a
    // healthy driver, and one of them needs a database name it can spoil to
    // get a failed connect. With `Catalog=` it has nothing to spoil and skips,
    // which is honest but is not what this scenario is for.
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched) << "Failed to launch crusher binary";
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    // Walk every probe once and collect the non-PASS ones grouped by status,
    // so the EXPECT failure messages can name the offenders. Without this the
    // canary would report only counts ("failed=6, skipped=23"), which forces
    // a second debugging round just to find out which probes regressed.
    std::string failed_names;
    std::string error_names;
    std::string skipped_names;
    if (run.report.contains("categories")) {
        for (const auto& cat : run.report["categories"]) {
            const auto cat_name = cat.value("name", std::string{});
            if (!cat.contains("tests")) continue;
            for (const auto& t : cat["tests"]) {
                const auto status = t.value("status", std::string{});
                if (status == "PASS") continue;
                std::string& bucket =
                    (status == "FAIL")  ? failed_names :
                    (status == "ERROR") ? error_names  : skipped_names;
                if (!bucket.empty()) bucket += ", ";
                bucket += cat_name + "/" + t.value("test_name", std::string{});
                if (status != "FAIL" && status != "ERROR") {
                    bucket += " (" + status + ")";
                }
                // D56: the name alone has never been enough. This canary
                // fires on a platform one cannot attach a debugger to, and
                // reading the probe's own account of what it found is the
                // whole difference between a diagnosis and another CI round
                // trip - which is what D54 had just written into `actual`.
                const auto actual = t.value("actual", std::string{});
                if (!actual.empty()) {
                    bucket += " [" + actual.substr(0, 200) + "]";
                }
            }
        }
    }

    // Per-platform tolerance baselines.
    //
    // Windows holds to the §7.10 contract: every probe PASSes. Linux and
    // macOS carry the same three failures, for the same reason, and it is not
    // a gap in the mock or in the probes — see below.
    //
    // The bound is locked in so a regression past it still trips this test,
    // and the improvement detector below nudges anyone who moves it.
#if defined(__linux__) || defined(__APPLE__)
    // D1, re-measured after D2: **3 failed, 0 skipped**, down from 13 and 19.
    //
    // D2 gave the .so the Windows DLL's export surface, and that closed the
    // whole of §8 in one change — the diagnostic cluster (I1), the eight
    // array-parameter skips (I5), the SQLWCHAR-width probes (I2), the catalog
    // patterns (I3) and the cursor probes (I4). Every one of them was a
    // consequence of unixODBC reaching the driver's ANSI entry points, which
    // the Windows driver manager never could. The numbers were locked in
    // rather than deleted precisely so that this would show up as a drop.
    //
    // What is left is three probes on both POSIX platforms, reporting
    // byte-for-byte the same thing at three different buffer sizes:
    //
    //   test_buffer_overflow_protection  offset 10 of a 10-byte buffer
    //   test_truncation_indicators       offset  3 of a 3-byte buffer
    //   test_undersized_buffer           offset  1 of a 1-byte buffer
    //
    // Each says `holds 0x00`: the driver manager writes the declared number
    // of characters and then a terminator one place past the end. It counts
    // BufferLength as characters available for text rather than as the total
    // including the terminator. Three sizes and two platforms make that a
    // rule rather than a coincidence.
    //
    // These are true positives, and nothing in this repository can fix them.
    // The mock is not involved — Windows runs the same mock through the same
    // probes and reports 191/191 with every guard intact — and the probes'
    // logic is not at fault either: they detected a one-byte overrun of
    // application memory, which is what they exist to do. So the canary's own
    // advice, "fix the mock or update the probe", is wrong in both directions
    // here, and this baseline is the only honest place for it. See D58, D59
    // and D64; only the guards those added made the write visible at all.
    //
    // Unlike the numbers this replaces, these are not expected to fall. If
    // they change, a driver manager changed.
    constexpr int kMaxFailed = 3;
    constexpr int kMaxSkipped = 0;
#else
    constexpr int kMaxFailed = 0;
    constexpr int kMaxSkipped = 0;
#endif

    const auto& summary = run.report["summary"];
    EXPECT_GT(summary.value("total_tests", 0), 100)
        << "Expected the full conformance suite to run";
    EXPECT_LE(summary.value("failed", -1), kMaxFailed)
        << "Mode=Success: failed-probe count exceeded the per-platform "
           "baseline (kMaxFailed=" << kMaxFailed << "). Either fix the mock "
           "or update the probe.\n  Failing probes: " << failed_names;
    EXPECT_EQ(summary.value("errors", -1), 0)
        << "Mode=Success must not produce probe ERRORs.\n"
           "  Erroring probes: " << error_names;
    EXPECT_LE(summary.value("skipped", -1), kMaxSkipped)
        << "Mode=Success: skipped-probe count exceeded the per-platform "
           "baseline (kMaxSkipped=" << kMaxSkipped << ").\n"
           "  Skipped probes: " << skipped_names;

    // B2: the pass rate is taken over what was graded, not over every result.
    // The reporter's own unit tests cover the arithmetic; this covers the
    // wiring - main.cpp's tally has to reach the report for the published
    // numbers to be self-consistent.
    ASSERT_TRUE(summary.contains("informational"))
        << "summary is missing the informational count (B2)";
    ASSERT_TRUE(summary.contains("scored"))
        << "summary is missing the scored count (B2)";
    EXPECT_EQ(summary.value("scored", -1),
              summary.value("total_tests", 0) -
                  summary.value("informational", 0))
        << "scored must be every result that was graded";
    if (summary.value("scored", 0) > 0) {
        EXPECT_NEAR(summary.value("pass_rate", -1.0),
                    summary.value("passed", 0) * 100.0 /
                        summary.value("scored", 1),
                    0.05)
            << "pass_rate must be passed/scored, not passed/total_tests";
    }

#if defined(__linux__) || defined(__APPLE__)
    // Improvement detector — when one of the §8 gaps gets fixed and the count
    // drops below baseline, surface a notice so the bound can be tightened.
    // Stays informational (doesn't fail the test); ratchets the baseline only
    // when someone reads the log and updates the constant above.
    //
    // D59 widened this past Linux. macOS carries a baseline now too, and it
    // is the one number nobody can lower from this repository — so a change
    // in it means the runner's driver manager changed, which is precisely
    // what one would want to be told.
#ifdef __linux__
    constexpr const char* kBaselineTag = "[linux-baseline]";
#else
    constexpr const char* kBaselineTag = "[macos-baseline]";
#endif
    // D1: both POSIX platforms sit at the same three now, so a drop below
    // means a driver manager was fixed rather than that someone closed a gap
    // in here. Worth the same notice either way.
    const int observed_failed = summary.value("failed", -1);
    const int observed_skipped = summary.value("skipped", -1);
    // Phase 6 works by pushing a change and reading this line out of the CI
    // log - there is no Linux machine to measure on locally. The counts used
    // to be printed only when they dropped *below* baseline, so a change that
    // moved nothing was indistinguishable from one that was never measured.
    // Printed unconditionally now, with the probe names, so each experiment
    // in orders 43-47 has a number to compare against.
    std::cerr << kBaselineTag << " failed=" << observed_failed
              << " (max " << kMaxFailed << ")"
              << " skipped=" << observed_skipped
              << " (max " << kMaxSkipped << ")" << std::endl;
    if (!failed_names.empty()) {
        std::cerr << kBaselineTag << " failing: " << failed_names << std::endl;
    }
    if (!skipped_names.empty()) {
        std::cerr << kBaselineTag << " skipped: " << skipped_names << std::endl;
    }
    // B5: a ratchet, not a suggestion. This used to write a `[notice]` to
    // stderr and pass, which ctest swallows for a passing test - so a closed
    // gap left the bound where it was and the canary went slack by exactly
    // the amount that had been fixed. Dropping below baseline now fails, and
    // the only way to make it pass again is to lower the constant.
    //
    // It reads oddly to fail a build for an improvement, and that is the
    // point: the failure is trivially fixed by editing one number, and
    // nothing else forces that number down. Everything above still fails on a
    // regression, so the bound is closed on both sides.
    EXPECT_GE(observed_failed, kMaxFailed)
        << "failed=" << observed_failed << " is BELOW the baseline of "
        << kMaxFailed << ", which means something was fixed. Lower "
        << "kMaxFailed to " << observed_failed << " in this file and record "
        << "what closed it (IMPROVEMENT_PLAN §8). This is the ratchet B5 "
        << "asks for - the test fails until the bound follows the "
        << "improvement.\n  Still failing: " << failed_names;
    EXPECT_GE(observed_skipped, kMaxSkipped)
        << "skipped=" << observed_skipped << " is BELOW the baseline of "
        << kMaxSkipped << ". Lower kMaxSkipped to " << observed_skipped
        << " and record what closed it.\n  Still skipped: " << skipped_names;
#endif

    EXPECT_TRUE(run.report.contains("driver_info"));
    EXPECT_TRUE(run.report.contains("categories"));
}

// ── Mode=Failure: every ODBC call fails, so the binary errors at connect ──

TEST_F(CrusherE2EFixture, ModeFailureFailsAtConnect) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Failure;Catalog=Default;");

    ASSERT_TRUE(run.launched);
    EXPECT_NE(run.exit_code, 0)
        << "Mode=Failure must surface a non-zero exit (connect / setup failure).";
}

// ── FailOn=SQLPrepare: prepare-related tests fail with the injected SQLSTATE
// while connection / metadata tests stay green. ────────────────────────────

TEST_F(CrusherE2EFixture, FailOnSQLPrepareIsLocalisedToPrepareTests) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLPrepare;"
        "ErrorCode=42S22;Catalog=Default;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto conn_cat = find_category(run.report, "Connection Tests");
    ASSERT_TRUE(conn_cat.has_value())
        << "Connection Tests category should be present in the report";
    ASSERT_TRUE(conn_cat->contains("tests"));
    EXPECT_FALSE((*conn_cat)["tests"].empty())
        << "Connection Tests must run even when SQLPrepare is gated";

    // Across the report, at least one test should be FAIL — the prepare path.
    int fail_count = 0;
    for (const auto& cat : run.report["categories"]) {
        for (const auto& t : cat["tests"]) {
            if (t.value("status", std::string{}) == "FAIL") ++fail_count;
        }
    }
    EXPECT_GT(fail_count, 0)
        << "At least one prepare-touching test must FAIL when FailOn=SQLPrepare";
}

// ── Catalog=Empty: catalog discovery returns no tables / no errors ────────

TEST_F(CrusherE2EFixture, CatalogEmptyKeepsCatalogTestsRunning) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Empty;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    // The metadata category should still execute — empty catalog isn't a
    // hard error, just zero rows. Confirms we didn't take the connect-fail
    // path by accident.
    auto meta = find_category(run.report, "Metadata/Catalog Tests");
    ASSERT_TRUE(meta.has_value());
    EXPECT_FALSE((*meta)["tests"].empty());
}

// ── ResultSetSize=0: zero-row result sets still produce a complete run ────

TEST_F(CrusherE2EFixture, ResultSetSizeZeroExercisesEmptyCursor) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=0;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));
    EXPECT_GT(run.report["summary"].value("total_tests", 0), 100);
}

// ── SilentCorruption=DropInserts: round-trip tests must FAIL (the §1.4
// verify_rows_persisted chain catches the silent drop). This is the
// scenario whose existence motivates the whole §5.2 work. ─────────────────

TEST_F(CrusherE2EFixture, SilentCorruptionDropInsertsTripsRoundTripChecks) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SilentCorruption=DropInserts;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Parameter Binding Tests",
                       "test_param_rebind_per_row_row_count");
    ASSERT_TRUE(t.has_value())
        << "test_param_rebind_per_row_row_count must be present (the "
           "DropInserts canary). Older mock DLL may be registered.";
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "Under DropInserts, the row-count verification MUST fail. "
           "If this PASSes, verify_rows_persisted is no longer load-bearing.";
}

// ── SilentCorruption=MangleVarchar: int→varchar round-trip must FAIL ─────
// MangleVarchar appends a sentinel char to every stored string, so even
// numeric-as-string round-trips ("5" → "5X") fail the value comparison
// inside verify_rows_persisted.

TEST_F(CrusherE2EFixture, SilentCorruptionMangleVarcharTripsVarcharRoundTrip) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SilentCorruption=MangleVarchar;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Parameter Binding Tests",
                       "test_bindparam_int_to_varchar_roundtrip");
    ASSERT_TRUE(t.has_value()) << report_outline(run);
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "Under MangleVarchar, the int→varchar round-trip MUST fail "
           "(stored values come back with sentinel appended).";
}

// ── SilentCorruption=TruncateNumeric: fractional double round-trip FAILs ──
// TruncateNumeric std::trunc()s every stored double. The dedicated
// fractional cell test_bindparam_double_to_varchar_fractional_roundtrip
// inserts 1.5, 2.5, … which become 1.0, 2.0, … under truncation —
// verify_rows_persisted's epsilon-bounded comparison trips. The
// non-fractional cells (which insert whole numbers) are NOT a valid
// canary because trunc(1.0) == 1.0; this test is what makes the
// TruncateNumeric chain end-to-end-verifiable.

TEST_F(CrusherE2EFixture, SilentCorruptionTruncateNumericTripsFractionalRoundTrip) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SilentCorruption=TruncateNumeric;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Parameter Binding Tests",
                       "test_bindparam_double_to_varchar_fractional_roundtrip");
    ASSERT_TRUE(t.has_value())
        << "Fractional roundtrip cell missing — was it removed?";
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "Under TruncateNumeric, the fractional double round-trip MUST fail "
           "(stored 1.5 → 1.0).";

    // PORT plan port 1.C — the SQL_NUMERIC_STRUCT byte-equality probe also
    // detects TruncateNumeric: literal 12345.67 stored as 12345.0 produces
    // a different mantissa than round(12345.67 * 10^scale).
    auto bytes = find_test(run.report, "Numeric Struct Tests",
                           "test_numeric_struct_roundtrip_byte_equality");
    ASSERT_TRUE(bytes.has_value())
        << "Byte-equality probe missing — was it removed?";
    EXPECT_EQ(bytes->value("status", std::string{}), "FAIL")
        << "Under TruncateNumeric, the SQL_NUMERIC_STRUCT byte-equality probe "
           "MUST fail (stored 12345.67 → 12345.0 changes the mantissa).";

    // PORT plan port 11 — sum-loop also trips: 100 × 0.01 → 100 × 0.0 = 0
    // instead of expected mantissa total 100 at scale=2.
    auto sum = find_test(run.report, "Numeric Struct Tests",
                         "test_decimal_sum_loop_precision");
    ASSERT_TRUE(sum.has_value())
        << "Decimal sum-loop probe missing — was it removed?";
    EXPECT_EQ(sum->value("status", std::string{}), "FAIL")
        << "Under TruncateNumeric, the decimal sum-loop MUST fail "
           "(every 0.01 → 0.0; total_mantissa goes from 100 to 0).";
}

// ── ArrayBindRowFailsAt=3: row 3 SQL_PARAM_ERROR, surrounding rows OK ─────
// PORT plan port 6. The mock injects a server-side failure for the configured
// row; per-row status array must show the mixed outcome. The probe stays
// PASS in this scenario (mixed outcome is reported correctly); the canary
// merely confirms the probe correctly discriminates the mixed-outcome shape.

TEST_F(CrusherE2EFixture, ArrayBindRowFailsAtProducesMixedStatus) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ArrayBindRowFailsAt=3;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Array Parameter Tests",
                       "test_param_status_per_row_partial_failure");
    ASSERT_TRUE(t.has_value()) << report_outline(run);
    EXPECT_EQ(t->value("status", std::string{}), "PASS")
        << "Probe must PASS — succ=4 err=1 is the correct mixed outcome.";
    // The actual string must contain the ERR marker for row 3 specifically.
    const std::string actual = t->value("actual", std::string{});
    EXPECT_NE(actual.find("OK, OK, ERR"), std::string::npos)
        << "Per-row status must show err in position 3. actual: " << actual;
}

// ── SupportsArrayBind=false: paramset-size probe SKIPs or silently passes ──
// PORT plan port 6. Driver returns HYC00 for SQL_ATTR_PARAMSET_SIZE > 1;
// the probe should report SKIP_UNSUPPORTED. The driver-manager layer
// (unixODBC) sometimes intercepts SQLSetStmtAttr and returns SQL_SUCCESS
// without forwarding to the driver — masking the error. That platform
// behavior is itself spec-legal (HYC00 is "the driver doesn't support
// it", and a manager pretending it's supported is the manager's bug).
// Therefore the canary's actual contract is: must NOT FAIL — both
// SKIP_UNSUPPORTED (driver-reported HYC00) and PASS (manager-suppressed)
// are acceptable here.

TEST_F(CrusherE2EFixture, SupportsArrayBindFalseDoesNotFailProbe) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SupportsArrayBind=false;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Array Parameter Tests",
                       "test_paramset_size_unsupported_returns_error");
    ASSERT_TRUE(t.has_value()) << report_outline(run);
    const std::string status = t->value("status", std::string{});
    EXPECT_NE(status, "FAIL")
        << "Probe must not FAIL when driver claims unsupported and returns "
           "HYC00 (or when DM intercepts and silently passes). got "
        << status << "; actual: " << t->value("actual", std::string{});
}

// ── Procedures=BrokenInout: {?=CALL …} OUT/INOUT probes FAIL ─────────────
// PORT plan port 3. Mock's MOCK_INOUT callback returns empty output_values,
// so the SQLExecute writeback path is a no-op. The IN-only probe still
// PASSes (its assertion is execute success); the OUT and INOUT probes
// FAIL with the sentinel-survived / no-mutation diagnostics.

TEST_F(CrusherE2EFixture, ProceduresBrokenInoutTripsOutAndInoutProbes) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "Procedures=BrokenInout;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    // IN probe stays PASS — execute-success only, no writeback expectation.
    auto in_t = find_test(run.report, "Escape Sequence Tests",
                          "test_call_escape_in_parameter");
    ASSERT_TRUE(in_t.has_value());
    EXPECT_EQ(in_t->value("status", std::string{}), "PASS")
        << "IN probe must stay PASS — BrokenInout doesn't break the IN path.";

    // OUT and INOUT probes MUST FAIL.
    auto out_t = find_test(run.report, "Escape Sequence Tests",
                           "test_call_escape_out_parameter");
    ASSERT_TRUE(out_t.has_value());
    EXPECT_EQ(out_t->value("status", std::string{}), "FAIL")
        << "OUT probe MUST fail under BrokenInout (sentinel survives).";

    auto inout_t = find_test(run.report, "Escape Sequence Tests",
                             "test_call_escape_inout_parameter");
    ASSERT_TRUE(inout_t.has_value());
    EXPECT_EQ(inout_t->value("status", std::string{}), "FAIL")
        << "INOUT probe MUST fail under BrokenInout (no UPPER mutation).";
}

// ── NativeSqlPassThrough=true: SQLNativeSql translation probes FAIL ───────
// PORT plan port 4. Mock returns SQLNativeSql input verbatim — escape
// sequences survive untouched. All four SQLNativeSql-only cells in
// EscapeSequenceTests must trip; execution-side tests
// (test_outer_join_escape, test_string_scalar_functions, etc.) stay PASS
// because the mock SQLExecDirect path still translates internally.

TEST_F(CrusherE2EFixture, NativeSqlPassThroughTripsTranslationProbes) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "NativeSqlPassThrough=true;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    for (const char* name : {
            "test_native_sql_scalar_functions",
            "test_native_sql_datetime_literals",
            // C11: `test_native_sql_call_escape` and
            // `test_call_escape_translation` were the same check as
            // `test_call_escape_format_variants` with different
            // identifier names, and that one already covered both of
            // their two-parameter forms. All three are one probe now,
            // over seven CALL formats, so this names the survivor.
            "test_call_escape_format_variants",
            "test_native_sql_outer_join_escape"}) {
        auto t = find_test(run.report, "Escape Sequence Tests", name);
        ASSERT_TRUE(t.has_value()) << "Probe missing: " << name;
        EXPECT_EQ(t->value("status", std::string{}), "FAIL")
            << name << " MUST fail under NativeSqlPassThrough.";
    }

    // Sanity: SQLExecDirect-side tests still pass — the mock translates at
    // execution time independently of SQLNativeSql.
    //
    // B1: the control used to be test_outer_join_escape, which was a poor
    // one — it never executed anything, so it could not have shown that
    // execution-time translation survived. (It is INFORMATIONAL now for
    // exactly that reason.) These three send `{fn …}` through SQLExecDirect
    // and compare the value that comes back, so they fail if execution-time
    // translation breaks.
    for (const char* name : {"test_string_scalar_functions",
                             "test_numeric_scalar_functions",
                             "test_scalar_function_claim_vs_execute"}) {
        auto t = find_test(run.report, "Escape Sequence Tests", name);
        ASSERT_TRUE(t.has_value()) << "Probe missing: " << name;
        EXPECT_EQ(t->value("status", std::string{}), "PASS")
            << name << ": execution-time escape translation must remain "
                       "intact under the SQLNativeSql-only pass-through. "
            << t->value("actual", std::string{});
    }
}

// ── SilentCorruption=MangleUnicode: WCHAR round-trip probes FAIL ───────────
// PORT plan port 7. Every non-ASCII byte in a fetched char/wchar cell becomes
// '?'; the codepoint-comparison round-trip catches it. Both the BMP probe
// and the supplementary (surrogate-pair) probe must trip.

TEST_F(CrusherE2EFixture, SilentCorruptionMangleUnicodeTripsWcharRoundTrip) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SilentCorruption=MangleUnicode;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Unicode Tests",
                       "test_wchar_roundtrip_non_ascii");
    ASSERT_TRUE(t.has_value())
        << "WCHAR round-trip probe missing — was it removed?";
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "Under MangleUnicode the BMP round-trip probe MUST fail "
           "(non-ASCII codepoints replaced with '?').";

    auto sp = find_test(run.report, "Unicode Tests",
                        "test_wchar_surrogate_pair_preserved");
    ASSERT_TRUE(sp.has_value())
        << "Surrogate-pair probe missing — was it removed?";
    EXPECT_EQ(sp->value("status", std::string{}), "FAIL")
        << "Under MangleUnicode the supplementary-codepoint probe MUST fail.";
}

// ── SilentCorruption=NullAsEmpty: the NULL-vs-empty contrast probe FAILs ──
// PORT plan port 2.E. Under NullAsEmpty the mock returns NULL char cells as
// empty string with indicator=0 — Oracle-style empty-vs-null conflation.
// The new contrast probe in datatype_edge_tests catches this; the integer
// and numeric variants are unaffected (NullAsEmpty only touches char/wchar).

TEST_F(CrusherE2EFixture, SilentCorruptionNullAsEmptyTripsNullVsEmptyContrast) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "SilentCorruption=NullAsEmpty;ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary"));

    auto t = find_test(run.report, "Data Type Edge Cases",
                       "test_null_vs_empty_distinction_varchar");
    ASSERT_TRUE(t.has_value())
        << "NULL-vs-empty contrast probe missing — was it removed?";
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "Under NullAsEmpty, the contrast probe MUST fail "
           "(both rows return indicator=0).";

    // Sibling cells stay PASS — NullAsEmpty only affects char/wchar fetch.
    auto t_int = find_test(run.report, "Data Type Edge Cases",
                           "test_null_vs_zero_distinction_integer");
    ASSERT_TRUE(t_int.has_value());
    EXPECT_EQ(t_int->value("status", std::string{}), "PASS")
        << "NullAsEmpty must not affect SQL_C_SLONG fetches.";
    auto t_num = find_test(run.report, "Data Type Edge Cases",
                           "test_null_in_numeric_struct");
    ASSERT_TRUE(t_num.has_value());
    EXPECT_EQ(t_num->value("status", std::string{}), "PASS")
        << "NullAsEmpty must not affect SQL_C_NUMERIC fetches.";
}

// ── Output contract: stdout is machine-readable, and the report is versioned ─
//
// G1: `odbc-crusher "..." -o json | jq '.summary'` is documented in README.md
// and did not work — main.cpp wrote "Phase 2: Running ODBC tests..." to stdout
// unconditionally, so the pipe fed a JSON parser a bare word. Progress chatter
// now goes to stderr and stdout carries nothing but the report.
//
// G4: the report carries a schema_version so downstream consumers (the triage
// skill, these tests, anything a driver project writes) can reject a shape they
// do not understand, and an ISO-8601 timestamp instead of a raw epoch integer.

TEST_F(CrusherE2EFixture, JsonToStdoutIsParseableWithNoProgressChatter) {
    auto run = run_crusher_stdout(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched) << "Failed to launch crusher binary";
    ASSERT_FALSE(run.raw_stdout.empty()) << "stderr: " << run.raw_stderr;

    // The first non-whitespace byte on stdout must open the JSON document.
    // Asserting on the parse alone would not catch chatter, because nlohmann
    // would simply fail; asserting on the first byte says *why* it failed.
    const auto first = run.raw_stdout.find_first_not_of(" \t\r\n");
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(run.raw_stdout[first], '{')
        << "stdout must contain only the JSON report. It begins with:\n"
        << run.raw_stdout.substr(0, 200);

    ASSERT_TRUE(run.report.is_object())
        << "stdout did not parse as JSON.\nstderr: " << run.raw_stderr;
    EXPECT_TRUE(run.report.contains("summary"));

    // The progress line has to still exist — on the other stream.
    EXPECT_NE(run.raw_stderr.find("Phase 2: Running ODBC tests"), std::string::npos)
        << "Progress output should have moved to stderr, not disappeared.\n"
           "stderr was:\n" << run.raw_stderr;
}

TEST_F(CrusherE2EFixture, ReportCarriesSchemaVersionAndIso8601Timestamp) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    ASSERT_TRUE(run.report.contains("schema_version"))
        << "The report must be self-describing (G4).";
    ASSERT_TRUE(run.report["schema_version"].is_number_integer())
        << "schema_version must be an integer, matching .github/drivers.json.";
    // S4: 2. The bump is for `connection_string`, whose secrets are now
    // masked - a consumer that reconnected with it can no longer, which is the
    // meaning change the number exists to announce. `environment` and
    // `driver_info.driver_manager_version` came with it and are additive.
    EXPECT_EQ(run.report["schema_version"].get<int>(), 2);

    ASSERT_TRUE(run.report.contains("timestamp"));
    ASSERT_TRUE(run.report["timestamp"].is_string())
        << "timestamp must be an ISO-8601 string, not a raw epoch integer (G4).";
    const auto ts = run.report["timestamp"].get<std::string>();

    // Shape check: YYYY-MM-DDThh:mm:ssZ. Deliberately hand-rolled rather than
    // <regex> — this asserts the exact 20-character layout, and a regex that
    // accepted a 19- or 21-character variant would let the contract drift.
    ASSERT_EQ(ts.size(), 20u) << "timestamp was: " << ts;
    for (size_t i = 0; i < ts.size(); ++i) {
        const char c = ts[i];
        if (i == 4 || i == 7) {
            EXPECT_EQ(c, '-') << "at index " << i << " of " << ts;
        } else if (i == 10) {
            EXPECT_EQ(c, 'T') << "at index " << i << " of " << ts;
        } else if (i == 13 || i == 16) {
            EXPECT_EQ(c, ':') << "at index " << i << " of " << ts;
        } else if (i == 19) {
            EXPECT_EQ(c, 'Z') << "at index " << i << " of " << ts;
        } else {
            EXPECT_TRUE(c >= '0' && c <= '9') << "at index " << i << " of " << ts;
        }
    }
}

// ── Phase 2: probe fixes, each with a configuration that makes it fail ──────
//
// The rule for this phase (IMPROVEMENT_PLAN.md Phase 2 exit criteria) is that
// every repaired probe must have a mock configuration proving it can now fail.
// Three of them had none, because nothing in the mock could make a numeric
// value come back wrong or a string come back unterminated — which is exactly
// why these probes could ship broken and nothing noticed. D33 and D34 added
// those levers; these scenarios are what they were added for.

// A5 — test_null_termination called std::strlen() on a buffer memset to 'X'
// with no NUL: undefined behaviour in precisely the case it existed to detect,
// and its FAIL branch (`buffer[strlen(buffer)] != '\0'`) was a tautology.
TEST_F(CrusherE2EFixture, NullTerminationProbeCatchesAnUnterminatedString) {
    auto ok = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(ok.report.contains("summary")) << report_outline(ok);
    if (auto why = baseline_blocker(ok.report, "Buffer Validation",
                                    "test_null_termination")) GTEST_SKIP() << *why;
    auto clean = find_test(ok.report, "Buffer Validation",
                           "test_null_termination");
    ASSERT_TRUE(clean.has_value());
    const auto clean_actual = clean->value("actual", std::string{});

    auto bad = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;"
        "BufferValidation=Lenient;");
    SKIP_IF_KILLED(bad);
    ASSERT_TRUE(bad.report.contains("summary")) << report_outline(bad);
    auto t = find_test(bad.report, "Buffer Validation", "test_null_termination");
    ASSERT_TRUE(t.has_value()) << report_outline(bad);
    const auto status = t->value("status", std::string{});
    const auto actual = t->value("actual", std::string{});

    // D53: this used to assert FAIL unconditionally, and that is a claim about
    // the driver manager rather than about the probe. The mock writes
    // "mockodbc.dll" and, under Lenient, overwrites the terminator at index 12
    // with an 'X'. Whether the application ever sees an unterminated string
    // depends on who is in the middle: since D2 the driver exports only its W
    // entry points, so every driver manager converts, and each does it
    // differently. Windows and unixODBC hand the un-terminated run through;
    // the macOS manager rescans its own zeroed buffer, finds the NUL one byte
    // later and re-terminates - so the application gets a well-formed
    // 13-character string instead of a broken 12-character one.
    //
    // What D33 and A5 actually promise is that the injected fault reaches the
    // application and that the probe grades it, so that is what is asserted.
    // Laundering it away entirely - the same result as the clean run - would
    // mean the lever does nothing, and that still fails here.
    EXPECT_NE(actual, clean_actual)
        << "BufferValidation=Lenient changed nothing the application can see; "
           "the fault-injection lever is not reaching SQLGetInfo.";

    if (status == "FAIL") {
        // A5's fix: the probe used to call strlen() on an unterminated buffer
        // and its FAIL branch was a tautology. This is the branch that proves
        // it can now report the thing it is named for.
        EXPECT_NE(actual.find("No NUL"), std::string::npos)
            << "actual was: " << actual;
    } else {
        EXPECT_EQ(status, "PASS") << "actual was: " << actual;
        // The manager re-terminated. The filler byte must still be visible as
        // one extra character, or the corruption never left the driver.
        EXPECT_NE(actual.find("13 bytes"), std::string::npos)
            << "the driver manager re-terminated the string, so the 'X' that "
               "replaced the NUL should show up as a 13th character; "
               "actual was: " << actual;
    }
}

// A6 — the value check was `value == 42 || indicator != SQL_NULL_DATA`. The
// right operand is true for every non-NULL fetch, so the probe passed on any
// value at all and its FAIL branch was unreachable.
TEST_F(CrusherE2EFixture, BindColIntegerProbeCatchesAWrongValue) {
    auto ok = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(ok.report.contains("summary")) << report_outline(ok);
    if (auto why = baseline_blocker(ok.report, "Statement Tests",
                                    "test_bind_col_integer")) GTEST_SKIP() << *why;

    auto bad = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;"
        "SilentCorruption=SkewNumeric;");
    ASSERT_TRUE(bad.report.contains("summary")) << report_outline(bad);
    auto t = find_test(bad.report, "Statement Tests", "test_bind_col_integer");
    ASSERT_TRUE(t.has_value()) << report_outline(bad);
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "SELECT 42 came back as 43 through the bound column; with the old "
           "`||` this still reported PASS.";
    EXPECT_NE(t->value("actual", std::string{}).find("43"), std::string::npos)
        << "actual was: " << t->value("actual", std::string{});
}

// A7 — the probe is named "values match" and never compared them: both
// branches set PASS, and SQLBindCol's return code was discarded.
TEST_F(CrusherE2EFixture, FetchBoundVsGetDataProbeCatchesADisagreement) {
    auto ok = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(ok.report.contains("summary")) << report_outline(ok);
    if (auto why = baseline_blocker(ok.report, "Statement Tests",
                                    "test_fetch_bound_vs_getdata")) GTEST_SKIP() << *why;

    // SkewNumericBound perturbs only the bound-column path, so the same column
    // read two ways disagrees — the exact defect this probe is named for.
    auto bad = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;"
        "SilentCorruption=SkewNumericBound;");
    ASSERT_TRUE(bad.report.contains("summary")) << report_outline(bad);
    auto t = find_test(bad.report, "Statement Tests", "test_fetch_bound_vs_getdata");
    ASSERT_TRUE(t.has_value()) << report_outline(bad);
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "bound and SQLGetData returned different values for one column";
    EXPECT_EQ(t->value("severity", std::string{}), "CRITICAL")
        << "two delivery paths disagreeing is data corruption, not a warning";
}

// A8 — test_paramset_size_one ignored both SQLSetStmtAttr return codes and
// then FAILed on the sentinel values a driver leaves behind when it declines
// the attribute: processed stays 0, status stays 0xFFFF. Both attributes are
// optional Level 1, so that was a guaranteed FAIL at Core for a correct
// driver. Verified before the fix: with the pointers declined the old code
// reported `FAIL: Execute returned 0; processed=0; status=65535`.
TEST_F(CrusherE2EFixture, ParamsetSizeOneSkipsRatherThanFailsWhenAttrsDeclined) {
    auto ok = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(ok.report.contains("summary")) << report_outline(ok);
    if (auto why = baseline_blocker(ok.report, "Array Parameter Tests",
                                    "test_paramset_size_one")) GTEST_SKIP() << *why;

    // SupportsArrayBind=false declines SQL_ATTR_PARAM_STATUS_PTR and
    // SQL_ATTR_PARAMS_PROCESSED_PTR — a driver with no array-parameter
    // execution has no use for either.
    auto declined = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;"
        "SupportsArrayBind=false;");
    ASSERT_TRUE(declined.report.contains("summary")) << report_outline(declined);
    auto t = find_test(declined.report, "Array Parameter Tests",
                       "test_paramset_size_one");
    ASSERT_TRUE(t.has_value()) << report_outline(declined);
    EXPECT_EQ(t->value("status", std::string{}), "SKIP_UNSUPPORTED")
        << "declining an optional Level 1 attribute is not a Core failure";
}

// A11 — 14 fetch loops were written `SQLFetch(h) == SQL_SUCCESS`, so they
// exited mid-result-set on any row carrying SQL_SUCCESS_WITH_INFO: a driver
// warning of 01004, 01S07, or its own 01000. Real drivers do warn per row, and
// an application must keep fetching until SQL_NO_DATA.
//
// The assertion is deliberately whole-suite rather than per-probe: the bug was
// one line repeated across five files, and comparing the two runs of the same
// binary catches any of them without hard-coding a platform baseline.
// Measured before the fix: 188 pass / 1 fail / 6 skip, including
// "Fetched 0 rows, then SQLFetch returned 1" from test_forward_only_past_end.
TEST_F(CrusherE2EFixture, PerRowWarningsDoNotTruncateFetchLoops) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";

    auto quiet = run_crusher(base);
    ASSERT_TRUE(quiet.report.contains("summary")) << report_outline(quiet);
    auto warning = run_crusher(base + "FetchReturnsWarning=true;");
    ASSERT_TRUE(warning.report.contains("summary")) << report_outline(warning);

    const auto& q = quiet.report["summary"];
    const auto& w = warning.report["summary"];

    // A driver that warns on every row is still a working driver: the verdicts
    // must be identical to the quiet run.
    EXPECT_EQ(w.value("failed", -1), q.value("failed", -2))
        << "per-row warnings changed the failure count";
    EXPECT_EQ(w.value("skipped", -1), q.value("skipped", -2))
        << "per-row warnings changed the skip count — a fetch loop stopped early";
    EXPECT_EQ(w.value("passed", -1), q.value("passed", -2))
        << "per-row warnings changed the pass count";

    // Name the probe the plan calls out, so a failure points somewhere useful.
    auto t = find_test(warning.report, "Cursor Behavior Tests", "test_forward_only_past_end");
    ASSERT_TRUE(t.has_value()) << report_outline(quiet);
    EXPECT_NE(t->value("status", std::string{}), "FAIL")
        << "actual: " << t->value("actual", std::string{});
}

// G6 — two runs of the same binary against the same driver must differ only in
// their timings, so that diffing a before/after report shows changed verdicts
// rather than changed noise.
//
// This started as a Phase 0 exit criterion checked by hand; it found that of
// 2,023 leaf values exactly two differed for a non-timing reason, both raw
// microsecond figures baked into `actual` by the cursor-stress probes. Making
// it executable stops that creeping back in.
TEST_F(CrusherE2EFixture, TwoRunsDifferOnlyInTimings) {
    const std::string conn =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";
    auto a = run_crusher(conn);
    auto b = run_crusher(conn);
    ASSERT_TRUE(a.report.contains("summary")) << report_outline(a);
    ASSERT_TRUE(b.report.contains("summary")) << report_outline(b);

    // Flatten to leaf paths so a difference can be named precisely.
    std::function<void(const nlohmann::json&, const std::string&,
                       std::map<std::string, std::string>&)> flatten =
        [&](const nlohmann::json& j, const std::string& path,
            std::map<std::string, std::string>& out) {
            if (j.is_object()) {
                for (auto it = j.begin(); it != j.end(); ++it) {
                    flatten(it.value(), path + "/" + it.key(), out);
                }
            } else if (j.is_array()) {
                for (size_t i = 0; i < j.size(); ++i) {
                    flatten(j[i], path + "/" + std::to_string(i), out);
                }
            } else {
                out[path] = j.dump();
            }
        };

    std::map<std::string, std::string> fa, fb;
    flatten(a.report, "", fa);
    flatten(b.report, "", fb);

    // Fields that are *expected* to move between runs.
    const auto is_volatile = [](const std::string& key) {
        return key == "/timestamp" ||
               (key.size() >= 12 &&
                key.compare(key.size() - 12, 12, "/duration_us") == 0) ||
               key == "/summary/total_duration_us";
    };

    std::vector<std::string> unstable;
    for (const auto& [key, value] : fa) {
        if (is_volatile(key)) continue;
        auto it = fb.find(key);
        if (it == fb.end() || it->second != value) {
            unstable.push_back(key + ": " + value + " vs " +
                               (it == fb.end() ? "<missing>" : it->second));
        }
    }
    for (const auto& [key, value] : fb) {
        if (is_volatile(key)) continue;
        if (fa.find(key) == fa.end()) unstable.push_back(key + ": <missing> vs " + value);
    }

    std::string detail;
    for (const auto& u : unstable) detail += "\n  " + u;
    EXPECT_TRUE(unstable.empty())
        << unstable.size() << " non-timing value(s) differ between two "
        << "identical runs, out of " << fa.size() << " leaves:" << detail;
}

// A1 — the largest false-negative class in the suite. A dialect-fallback loop
// treated "executed fine, returned 41 instead of 42" exactly like "did not
// execute": both fell through to the next variant, and when the list ran out
// the probe reported SKIP_INCONCLUSIVE — which does not affect the exit code.
//
// With C2's helper the two are distinguishable: once a variant executes the
// probe is conclusive, so a wrong value is a FAIL.
//
// Measured across the whole suite under SkewNumeric: 33 failures / 4 skips
// before, 36 / 1 after — the three DataType probes moving from SKIP to FAIL.
TEST_F(CrusherE2EFixture, WrongValueIsAFailureNotAnInconclusiveSkip) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";

    auto quiet = run_crusher(base);
    ASSERT_TRUE(quiet.report.contains("summary")) << report_outline(quiet);
    if (auto why = baseline_blocker(quiet.report, "Data Type Tests",
                                    "test_integer_types")) GTEST_SKIP() << *why;

    auto skewed = run_crusher(base + "SilentCorruption=SkewNumeric;");
    ASSERT_TRUE(skewed.report.contains("summary")) << report_outline(skewed);

    for (const char* probe : {"test_integer_types", "test_decimal_types",
                              "test_float_types"}) {
        auto t = find_test(skewed.report, "Data Type Tests", probe);
        ASSERT_TRUE(t.has_value()) << probe;
        EXPECT_EQ(t->value("status", std::string{}), "FAIL")
            << probe << " returned a wrong value; SKIP_INCONCLUSIVE would hide "
            << "it from the exit code. actual: " << t->value("actual", std::string{});
        // The report must say what the driver returned, not just that it was
        // wrong — that is what makes the finding actionable.
        EXPECT_NE(t->value("actual", std::string{}).find("Expected"),
                  std::string::npos)
            << probe << " actual: " << t->value("actual", std::string{});
    }
}

// C2 — when no dialect variant executes, the report must say what each one
// failed with. Prior plan item 2.8 was closed as "implicitly addressed" by the
// run_test extraction; it was not, because every fallback loop carried its own
// inner `catch (const OdbcError&) { continue; }` that discarded the query, the
// SQLSTATE and the message.
TEST_F(CrusherE2EFixture, FailedDialectVariantsAreReportedNotSwallowed) {
    // Catalog=Empty removes the tables, so catalog-dependent variants fail;
    // Mode=Partial with FailOn=SQLExecDirect makes every variant fail outright.
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLExecDirect;"
        "Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.launched);
    if (!run.report.contains("categories")) {
        GTEST_SKIP() << "driver refused the connection under this configuration";
    }

    // Find any probe that reported no compatible query pattern, and require it
    // to carry a diagnostic naming at least one failed variant.
    int checked = 0;
    for (const auto& cat : run.report["categories"]) {
        if (!cat.contains("tests")) continue;
        for (const auto& t : cat["tests"]) {
            const auto actual = t.value("actual", std::string{});
            if (actual.find("No compatible") == std::string::npos &&
                actual.find("Could not test") == std::string::npos) {
                continue;
            }
            ++checked;
            const auto diag = t.value("diagnostic", std::string{});
            EXPECT_FALSE(diag.empty())
                << cat.value("name", std::string{}) << "/"
                << t.value("test_name", std::string{})
                << " gave up on every dialect variant without saying why";
            EXPECT_NE(diag.find("->"), std::string::npos)
                << "diagnostic should list each variant and its SQLSTATE: " << diag;
        }
    }
    if (checked == 0) {
        GTEST_SKIP() << "no probe exhausted its dialect list under this "
                        "configuration; nothing to assert";
    }
}

// B3 — not one probe read a SQLSTATE before choosing between SKIP_UNSUPPORTED
// and FAIL, so a driver that failed a *Core* function and one that declined an
// optional feature were reported identically. SKIP does not affect the exit
// code, so the Core failure disappeared.
//
// classify_failure() maps IM001/HYC00/HY092/HY106 to SKIP_UNSUPPORTED and
// everything else to FAIL, and writes the state into the report either way.
TEST_F(CrusherE2EFixture, DeclinedOptionalFeatureSkipsAndNamesItsSqlstate) {
    // SupportsArrayBind=false makes the mock return HYC00 for the array
    // parameter attributes — a genuine "not implemented".
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;"
        "SupportsArrayBind=false;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    auto t = find_test(run.report, "Array Parameter Tests", "test_param_status_array");
    ASSERT_TRUE(t.has_value()) << report_outline(run);
    const auto status = t->value("status", std::string{});
    if (status == "SKIP_INCONCLUSIVE") {
        // The probe did not get as far as the attribute. On Linux the
        // array-parameter cluster (I5) stops it earlier, and there is no
        // classification to judge.
        GTEST_SKIP() << "test_param_status_array is SKIP_INCONCLUSIVE on this "
                        "platform, so it never reached the attribute: "
                     << t->value("actual", std::string{});
    }
    EXPECT_EQ(status, "SKIP_UNSUPPORTED")
        << "HYC00 means the driver does not implement it; that is a skip";
    // The state must reach the report, whichever way it was classified —
    // without it a reader cannot tell a refusal from a failure.
    const auto where = t->value("actual", std::string{}) + " " +
                       t->value("diagnostic", std::string{});
    EXPECT_NE(where.find("HYC00"), std::string::npos)
        << "actual/diagnostic did not name the SQLSTATE: " << where;
}

// The other half: a driver that fails a Core function for some *other* reason
// must be a FAIL, not excused as unsupported. Mode=Partial + FailOn makes the
// mock fail SQLTables with its configured ErrorCode rather than HYC00.
TEST_F(CrusherE2EFixture, CoreFunctionFailureIsNotExcusedAsUnsupported) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLTables;ErrorCode=42000;"
        "Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.launched);
    if (!run.report.contains("categories")) {
        GTEST_SKIP() << "driver refused the connection under this configuration";
    }

    auto t = find_test(run.report, "Catalog Function Depth",
                       "test_tables_search_patterns");
    if (!t.has_value()) {
        GTEST_SKIP() << "probe not present in this build";
    }
    const auto status = t->value("status", std::string{});
    if (status == "PASS") {
        GTEST_SKIP() << "the driver did not fail SQLTables under this "
                        "configuration, so there is nothing to classify";
    }
    EXPECT_EQ(status, "FAIL")
        << "SQLTables is Core and 42000 is not an optional-feature state, so "
           "this must not be excused as SKIP_UNSUPPORTED. actual: "
        << t->value("actual", std::string{});
}

// ── A22: a failed SQLEndTran must be reported as itself ───────────────────
//
// The transaction probes used to say "SQLEndTran(COMMIT) failed" with no
// SQLSTATE and no return code, which tells a driver author nothing about
// which failure they are looking at. FailOn=SQLEndTran with a chosen
// ErrorCode gives an exact string to assert on: if the state stops reaching
// the report, this fails.
TEST_F(CrusherE2EFixture, FailedEndTranNamesItsSqlstate) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLEndTran;"
        "ErrorCode=40001;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("categories"));

    struct Case {
        const char* probe;
        const char* verb;
    };
    for (const Case& c : {Case{"test_manual_commit", "SQL_COMMIT"},
                          Case{"test_manual_rollback", "SQL_ROLLBACK"}}) {
        auto t = find_test(run.report, "Transaction Tests", c.probe);
        ASSERT_TRUE(t.has_value()) << c.probe << " is missing from the report";

        const auto status = t->value("status", std::string{});
        const auto actual = t->value("actual", std::string{});
        EXPECT_EQ(status, "FAIL")
            << c.probe << " under FailOn=SQLEndTran: " << actual;
        if (status != "FAIL") continue;

        // Portable: the return code and which transaction verb failed.
        // Before A22 the entire message was "SQLEndTran(COMMIT) failed".
        EXPECT_NE(actual.find("rc=-1"), std::string::npos)
            << c.probe << " did not report the return code: " << actual;
        EXPECT_NE(actual.find(c.verb), std::string::npos)
            << c.probe << " did not say which transaction verb failed: "
            << actual;

        // Driver-manager dependent. On unixODBC the probe correctly reports
        // `rc=-1 [no diagnostic]`, because SQLGetDiagRec on the connection
        // handle returns nothing after the driver's SQLEndTran returned
        // SQL_ERROR. That is I1, "diagnostic forwarding through unixODBC" --
        // this scenario is a concrete instance of it, not a separate defect:
        // the tool asked for the state and was not given one. The assertion
        // starts running on Linux the moment I1 closes, with no edit here.
        if (actual.find("no diagnostic") != std::string::npos) {
            std::cout << "[ I1 ] " << c.probe
                      << ": no SQLSTATE reached the application on this "
                         "platform, so the injected 40001 cannot be "
                         "asserted: " << actual << std::endl;
            continue;
        }
        EXPECT_NE(actual.find("40001"), std::string::npos)
            << c.probe << " reported a diagnostic, but not the injected "
                          "SQLSTATE: " << actual;
    }
}

// A22's other half, on the probes that INSERT and then verify persistence.
//
// Whether their commit can fail at all is a driver-manager question, and the
// two disagree. The Windows DM answers SQLEndTran itself while the connection
// is in autocommit and never calls the driver, so the commit cannot fail and
// these probes stay green. unixODBC forwards it, the injected failure lands,
// and D40 makes the mock discard the rows a failed COMMIT did not commit - so
// on Linux and macOS the probes correctly find nothing and fail.
//
// Both are acceptable. What is NOT acceptable, and what A22 fixed, is failing
// with the bind path blamed: before A22 the report said "this is the Firebird
// #161 silent-corruption shape - check the driver's numeric-C to
// character-SQL conversion on the bind path" when the real fault was a
// COMMIT that returned SQL_ERROR. So the assertion is on the *reason*, which
// is the same on every platform that can reach it.
TEST_F(CrusherE2EFixture, FailedCommitIsBlamedOnTheCommitNotTheBindPath) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLEndTran;"
        "ErrorCode=40001;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("categories"));

    for (const char* probe : {"test_bindparam_int_to_varchar_roundtrip",
                              "test_param_rebind_per_row_row_count",
                              "test_param_batch_then_single_row_tail"}) {
        auto t = find_test(run.report, "Parameter Binding Tests", probe);
        ASSERT_TRUE(t.has_value()) << probe << " is missing from the report";

        const auto status = t->value("status", std::string{});
        const auto actual = t->value("actual", std::string{});
        const auto suggestion = t->value("suggestion", std::string{});

        // D92: this used to `continue` on a PASS, on the reading that the
        // driver manager had not forwarded the commit and there was nothing
        // to attribute. The real reason was that all three probes ran in
        // autocommit ON, where SQLEndTran is a no-op - so all three always
        // passed, the `reached` counter this loop kept was **0 of 3**, and
        // every assertion below was dead code on every platform.
        //
        // They open a transaction now, so the manager has to forward the
        // commit and a PASS is a genuine disagreement.
        EXPECT_EQ(status, "FAIL")
            << probe << " passed under FailOn=SQLEndTran, which means its "
                        "COMMIT was not forwarded or not graded: " << actual;
        if (status != "FAIL") continue;

        // The commit's outcome must be in the report at all - A22.
        EXPECT_NE(actual.find("SQLEndTran(SQL_COMMIT) rc=-1"), std::string::npos)
            << probe << " did not report the failed commit: " << actual;

        // ...and the suggestion must not send a driver author to the bind
        // path for a fault that happened at COMMIT.
        EXPECT_NE(suggestion.find("COMMIT failed"), std::string::npos)
            << probe << " blamed something other than the commit: "
            << suggestion;
        EXPECT_EQ(suggestion.find("#161"), std::string::npos)
            << probe << " still blames the Firebird #161 bind-path shape for a "
                        "commit failure: " << suggestion;
    }

}

// ── A27: a discarded SQLGetData return code produced a false PASS ─────────
//
// Both transaction probes read SELECT COUNT(*) into a variable initialised to
// 0 and threw the return code away. For test_manual_rollback, 0 *is* the pass
// condition — so a driver whose SQLGetData returned SQL_ERROR was reported as
// "Transaction rolled back successfully". Measured, not assumed: with the
// return code discarded this probe PASSes under this exact configuration.
//
// FailOn=SQLGetData is a fault-injection hook added with this fix; before it,
// no configuration of the reference driver could make either probe wrong.
TEST_F(CrusherE2EFixture, DiscardedGetDataRcDoesNotBecomeARollbackPass) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLGetData;"
        "ErrorCode=HY000;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.launched);
    ASSERT_TRUE(run.report.contains("categories"));

    for (const char* probe : {"test_manual_rollback", "test_manual_commit"}) {
        auto t = find_test(run.report, "Transaction Tests", probe);
        ASSERT_TRUE(t.has_value()) << probe << " is missing from the report";

        const auto status = t->value("status", std::string{});
        const auto actual = t->value("actual", std::string{});
        // The assertion that matters is portable: the probe must not report
        // success while the call it depends on is failing. Naming the call
        // comes from report_failure, which reads the statement diagnostics,
        // so it is checked only where they arrive - see I1 on the scenario
        // above.
        EXPECT_NE(status, "PASS")
            << probe << " passed while SQLGetData was failing: " << actual;
        if (actual.find("SQLGetData") == std::string::npos) {
            std::cout << "[ I1 ] " << probe
                      << ": the failing call was not named on this platform: "
                      << actual << std::endl;
        }
    }
}
// The harness's temp files currently in the temp directory.
//
// run_crusher() removes both of its files before returning, so one still here
// afterwards is a handle someone is holding - see D55 below.
//
// D80: this used to return a *count*, and the assertion compared it to zero.
// The temp directory is shared: a file left by another scenario, by an earlier
// ctest run, or by a crusher still exiting elsewhere all counted against it.
// That made the check fail intermittently on macOS across unrelated commits,
// and locally whenever the test ran twice inside thirty seconds - its own
// wedged child sleeps for thirty, so the previous run's orphan was still
// holding its file. The set lets the caller compare before against after and
// count only what its own run left.
std::set<std::string> harness_temp_files() {
    std::set<std::string> names;
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(
             std::filesystem::temp_directory_path(), ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("crusher_e2e_", 0) == 0) names.insert(name);
    }
    return names;
}

// D55 - the harness could wait on a hung child forever. The sanitizer job
// proved it: crusher wedged on the first scenario and the CI job sat on that
// one test for over an hour, heading for GitHub's six-hour cap, while the
// other thirteen jobs had long since finished.
//
// `Latency=` makes every ODBC call sleep, and a sleep long enough is
// indistinguishable from a wedge - which is exactly the point. The driver
// sleeps for thirty seconds on connect; the harness is told to give up after
// two.
TEST_F(CrusherE2EFixture, AWedgedCrusherIsKilledRatherThanWaitedOn) {
    struct EnvGuard {
        explicit EnvGuard(const char* value) {
#ifdef _WIN32
            _putenv_s("ODBC_CRUSHER_E2E_TIMEOUT_SECONDS", value);
#else
            setenv("ODBC_CRUSHER_E2E_TIMEOUT_SECONDS", value, 1);
#endif
        }
        ~EnvGuard() {
#ifdef _WIN32
            _putenv_s("ODBC_CRUSHER_E2E_TIMEOUT_SECONDS", "");
#else
            unsetenv("ODBC_CRUSHER_E2E_TIMEOUT_SECONDS");
#endif
        }
    } guard("2");

    // D80: what was already lying about before this run starts. Anything in
    // this set at the end is somebody else's and is not what this test asks
    // about.
    const auto before = harness_temp_files();

    const auto started = std::chrono::steady_clock::now();
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;Latency=30s;");
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started);

    EXPECT_TRUE(run.timed_out)
        << "the child slept for thirty seconds and the harness waited for it; "
           "exit_code=" << run.exit_code << " stderr=" << run.raw_stderr;
    EXPECT_LT(elapsed.count(), 25)
        << "the harness took " << elapsed.count()
        << "s to give up on a two-second deadline";
    EXPECT_NE(run.raw_stderr.find("did not exit within"), std::string::npos)
        << "a killed child must say so, or the failure it causes downstream "
           "looks like a driver bug; stderr was: " << run.raw_stderr;

    // Giving up on the child is not the same as killing it. The first cut of
    // this fix terminated the shell and left odbc-crusher running, still
    // holding the stderr file the redirection had opened - so the harness
    // could not delete it, and the leftover is the orphan's signature. On
    // POSIX the group kill makes this structurally true rather than
    // observable; the assertion is the same either way.
    std::vector<std::string> orphaned;
    for (const auto& name : harness_temp_files()) {
        if (before.count(name) == 0) orphaned.push_back(name);
    }
    EXPECT_TRUE(orphaned.empty())
        << "this run left " << orphaned.size() << " temp file(s) behind, "
           "which means a child of the killed shell is still holding them "
           "open: " << [&] {
               std::string s;
               for (const auto& n : orphaned) { s += n; s += ' '; }
               return s;
           }();
}
// ── G2: running a subset ───────────────────────────────────────────────────
//
// There was no way to run fewer than all 206 probes. The harness said so in a
// comment and lived with a full run every time; bisecting a driver that hangs
// meant bisecting the source.

// The test that matters most here, because it caught a real bug in the first
// cut of G2: --list-categories advertised hand-written names, and 16 of the
// 23 disagreed with what the classes report — so every one of those names was
// a --category argument that would be refused. The two lists have to be the
// same list.
TEST_F(CrusherE2EFixture, ListedCategoriesAreExactlyTheOnesTheReportUses) {
    auto listed = run_crusher_stdout("", {"--list-categories"});
    ASSERT_TRUE(listed.launched);
    ASSERT_EQ(listed.exit_code, 0) << listed.raw_stderr;

    std::vector<std::string> names;
    std::istringstream lines(listed.raw_stdout);
    for (std::string line; std::getline(lines, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) names.push_back(line);
    }
    ASSERT_FALSE(names.empty()) << "stdout was: " << listed.raw_stdout;

    auto full = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;");
    ASSERT_TRUE(full.report.contains("categories")) << full.raw_stderr;

    std::vector<std::string> reported;
    for (const auto& cat : full.report["categories"]) {
        reported.push_back(cat.value("name", std::string{}));
    }

    EXPECT_EQ(names, reported)
        << "--list-categories must print the names the report uses, in the "
           "same order — anything else is a name --category will refuse";
}

TEST_F(CrusherE2EFixture, CategoryFilterRunsOnlyWhatWasAskedFor) {
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;",
        {"--category", "Connection Tests"});
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    ASSERT_TRUE(run.report.contains("categories"));
    ASSERT_EQ(run.report["categories"].size(), 1u) << report_outline(run);
    EXPECT_EQ(run.report["categories"][0].value("name", std::string{}),
              "Connection Tests");

    // Matching is case-insensitive, because nobody types "SQLSTATE Validation"
    // with the capitalisation exactly right.
    auto lowered = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;",
        {"--category", "connection tests"});
    ASSERT_TRUE(lowered.report.contains("categories")) << lowered.raw_stderr;
    EXPECT_EQ(lowered.report["categories"].size(), 1u);
}

TEST_F(CrusherE2EFixture, AFilteredReportSaysThatItWasFiltered) {
    // A filtered run is a smaller, entirely valid-looking report. Without
    // this, a consumer comparing its pass rate against a full run reads the
    // filter as a regression.
    auto filtered = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;",
        {"--category", "Connection Tests"});
    ASSERT_TRUE(filtered.report.contains("summary")) << report_outline(filtered);

    ASSERT_TRUE(filtered.report.contains("categories_selected"))
        << "a filtered report must say so: " << report_outline(filtered);
    EXPECT_EQ(filtered.report["categories_selected"].size(), 1u);
    ASSERT_TRUE(filtered.report.contains("categories_available"));
    EXPECT_GT(filtered.report["categories_available"].size(), 1u);

    auto full = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;");
    ASSERT_TRUE(full.report.contains("summary")) << report_outline(full);
    EXPECT_FALSE(full.report.contains("categories_selected"))
        << "an unfiltered run must not claim to be filtered";
}

TEST_F(CrusherE2EFixture, ACategoryThatMatchesNothingIsAnError) {
    // The alternative is a clean, empty and entirely meaningless report,
    // which in someone's CI reads as "everything passed".
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;ResultSetSize=10;",
        {"--category", "No Such Category"});
    EXPECT_NE(run.exit_code, 0)
        << "a typo'd category must fail, not run nothing and pass";
    EXPECT_NE(run.raw_stderr.find("no test category matched"), std::string::npos)
        << "stderr was: " << run.raw_stderr;
    // And it must name what it does know, or the user is left guessing.
    EXPECT_NE(run.raw_stderr.find("Connection Tests"), std::string::npos)
        << "stderr was: " << run.raw_stderr;
}

// ── G3: severity threshold for the exit code ───────────────────────────────
//
// main exited 1 for any FAIL or ERR whatever its severity, so against a real
// driver the exit code was always 1 — which is why the stress-test composite
// sets continue-on-error and nobody reads it.

TEST_F(CrusherE2EFixture, FailOnSeverityDecidesTheExitCode) {
    // The threshold to test comes from the report, not from an assumption
    // about what fails here.
    //
    // The first version of this asserted that BufferValidation=Lenient
    // produces exactly one FAIL at severity ERROR, and that --fail-on=critical
    // therefore exits 0. Both are true on Windows and neither is true on
    // POSIX, where the driver managers' buffer overrun (D1, D59) adds two
    // more failures, one of them CRITICAL — so --fail-on=critical correctly
    // exits non-zero there and this test called that a bug. Same shape as
    // D53: a scenario asserting a platform's behaviour as though it were the
    // tool's.
    const std::string conn =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;BufferValidation=Lenient;";

    auto baseline = run_crusher(conn);
    SKIP_IF_KILLED(baseline);
    ASSERT_TRUE(baseline.report.contains("summary")) << report_outline(baseline);
    ASSERT_GT(baseline.report["summary"].value("failed", -1), 0)
        << "this configuration must produce something to grade\n"
        << report_outline(baseline);
    EXPECT_NE(baseline.exit_code, 0)
        << "the default must keep failing on any FAIL — G3 is opt-in";

    // Most severe first, matching the enum's order.
    const std::vector<std::string> levels = {"critical", "error", "warning",
                                             "info"};
    size_t worst = levels.size();
    for (const auto& cat : baseline.report["categories"]) {
        for (const auto& t : cat["tests"]) {
            const auto status = t.value("status", std::string{});
            if (status != "FAIL" && status != "ERROR") continue;
            const auto sev = t.value("severity", std::string{});
            for (size_t i = 0; i < levels.size(); ++i) {
                std::string upper = levels[i];
                for (auto& c : upper) c = static_cast<char>(std::toupper(c));
                if (sev == upper && i < worst) worst = i;
            }
        }
    }
    ASSERT_LT(worst, levels.size())
        << "no failure carried a severity this test recognises\n"
        << report_outline(baseline);

    // A threshold at the worst severity present must trip, and so must every
    // less severe one — those are the levels that include it.
    for (size_t i = worst; i < levels.size(); ++i) {
        auto run = run_crusher_with_args(conn, {"--fail-on", levels[i]});
        // D75: 137 is not 0, so a killed run would have satisfied the
        // assertion below for entirely the wrong reason. This one was never
        // failing; it was quietly agreeing.
        SKIP_IF_KILLED(run);
        EXPECT_NE(run.exit_code, 0)
            << "--fail-on=" << levels[i] << " must trip on a "
            << levels[worst] << "-severity failure; " << report_outline(run);
    }

    // A threshold stricter than anything present must not trip. There is no
    // level above CRITICAL, so this is skipped when the worst is already that
    // — which is the case on Linux and macOS.
    if (worst > 0) {
        auto stricter = run_crusher_with_args(conn, {"--fail-on", levels[worst - 1]});
        SKIP_IF_KILLED(stricter);
        EXPECT_EQ(stricter.exit_code, 0)
            << "--fail-on=" << levels[worst - 1] << " must not trip on a "
            << levels[worst] << "-severity failure; " << stricter.raw_stderr;
        // The probe still ran and still failed — the threshold changes the
        // exit code, not the report. Reporting less would be a different tool.
        ASSERT_TRUE(stricter.report.contains("summary")) << report_outline(stricter);
        EXPECT_EQ(stricter.report["summary"].value("failed", -1),
                  baseline.report["summary"].value("failed", -1));
    } else {
        std::cout << "[ G3 ] worst severity present is CRITICAL, so the "
                     "\"stricter threshold does not trip\" half is not "
                     "reachable on this platform\n";
    }

    // And the escape hatch. The report half is unconditional; the exit-code
    // half is not, and D71 is why.
    auto none = run_crusher_with_args(conn, {"--fail-on", "none"});
    SKIP_IF_KILLED(none);
    ASSERT_TRUE(none.report.contains("summary")) << report_outline(none);
    EXPECT_EQ(none.report["summary"].value("failed", -1),
              baseline.report["summary"].value("failed", -1))
        << "--fail-on=none must silence the exit code, not the report";

    if (auto crash = crashed_category(none)) {
        // D71: the driver manager faulted, so the exit status is no longer
        // about --fail-on. The crash guard's siglongjmp abandons the
        // category's results, LeakSanitizer reports that at exit, and
        // abort_on_error turns it into an abort - none of which the flag
        // controls. Asserting through it would be asserting the platform's
        // behaviour as though it were the tool's, which is D53's mistake.
        //
        // The report half above still ran, which is what the flag actually
        // promises: silence the exit code, not the report.
        std::cout << "[ D71 ] exit-code half skipped: " << *crash << "\n";
    } else {
        EXPECT_EQ(none.exit_code, 0) << none.raw_stderr;
    }
}

TEST_F(CrusherE2EFixture, FailOnDoesNotMaskAConnectionFailure) {
    // --fail-on grades probe results. A driver that will not connect produced
    // no results to grade, and must not come back as success just because
    // someone asked for a high threshold.
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Failure;", {"--fail-on", "none"});
    EXPECT_NE(run.exit_code, 0)
        << "a failed connection is not a passing run; stderr was: "
        << run.raw_stderr;
}
// ── I8 / PORT 9.B and 10.B — the two probes C13 unblocked ─────────────────

// The failing configuration 9.B needs. FailOn=SQLDisconnect makes the
// disconnect fail with something that is not 25000, which is exactly the
// branch the probe grades: a driver may refuse to disconnect with an open
// transaction, but it has to say 25000 so the application can tell that apart
// from the connection having dropped.
TEST_F(CrusherE2EFixture, DisconnectProbeCatchesTheWrongSqlstate) {
    auto ok = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(ok.report.contains("summary")) << report_outline(ok);
    auto clean = find_test(ok.report, "Transaction Tests",
                           "test_disconnect_with_open_transaction");
    ASSERT_TRUE(clean.has_value()) << report_outline(ok);
    ASSERT_EQ(clean->value("status", std::string{}), "PASS")
        << "precondition: the probe passes against a conforming driver; "
        << clean->value("actual", std::string{});

    auto bad = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Partial;FailOn=SQLDisconnect;"
        "ErrorCode=HY000;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(bad.report.contains("summary")) << report_outline(bad);
    auto t = find_test(bad.report, "Transaction Tests",
                       "test_disconnect_with_open_transaction");
    ASSERT_TRUE(t.has_value()) << report_outline(bad);

    const auto status = t->value("status", std::string{});
    const auto actual = t->value("actual", std::string{});
    EXPECT_EQ(status, "FAIL")
        << "a disconnect that fails with HY000 rather than 25000 must be "
           "reported; actual was: " << actual;
    EXPECT_NE(actual.find("25000"), std::string::npos)
        << "the message has to name the SQLSTATE it wanted; actual was: "
        << actual;
}

// 10.B has no fault-injection lever that reaches only its post-reconnect
// execute — FailOn is global, so anything that breaks the second statement
// breaks the first as well and the probe never gets that far. What is
// asserted is therefore what can be: that it runs, grades, and reports the
// autocommit state it deliberately does not grade. Said plainly rather than
// left to look like coverage it is not.
TEST_F(CrusherE2EFixture, ReconnectProbeGradesUsabilityAndReportsAutocommit) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Connection Tests",
                       "test_reconnected_handle_is_usable");
    ASSERT_TRUE(t.has_value()) << report_outline(run);

    EXPECT_EQ(t->value("status", std::string{}), "PASS")
        << t->value("actual", std::string{});
    const auto actual = t->value("actual", std::string{});
    // The graded half.
    EXPECT_NE(actual.find("executes and commits normally"), std::string::npos)
        << "actual was: " << actual;
    // The reported-but-not-graded half. Connection attributes belong to the
    // handle and persist across a reconnect, so a driver that keeps the
    // caller's setting is right — the first version of this probe FAILed it,
    // which is why the distinction is asserted rather than assumed.
    EXPECT_NE(actual.find("SQL_ATTR_AUTOCOMMIT after reconnect"),
              std::string::npos)
        << "actual was: " << actual;
}

// ── P10: the diagnostics of a connect that fails ─────────────────────────
//
// The lever is `Database=`, an accepted spelling of `Catalog=`: the probe
// spoils its value, the mock has no database by that name, and the connect is
// refused for the most ordinary reason there is. `ConnectDiagnostics=Garbled`
// then decides whether the refusal can be read.
//
// Three scenarios, because the probe has three outcomes and each one has been
// wrong at some point in a probe of this shape: it grades a good record, it
// catches a bad one, and it says so plainly when the data source cannot be
// made to refuse a connect at all.
TEST_F(CrusherE2EFixture, FailedConnectDiagnosticsPassWhenTheRecordIsReadable) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Connection Tests",
                       "test_failed_connect_diagnostics_are_wellformed");
    ASSERT_TRUE(t.has_value()) << report_outline(run);

    const auto actual = t->value("actual", std::string{});
    EXPECT_EQ(t->value("status", std::string{}), "PASS") << actual;
    EXPECT_NE(actual.find("08001"), std::string::npos)
        << "the record the driver returned has to be quoted; actual was: "
        << actual;
}

TEST_F(CrusherE2EFixture, FailedConnectDiagnosticsCatchAnUnreadableRecord) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;ConnectDiagnostics=Garbled;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Connection Tests",
                       "test_failed_connect_diagnostics_are_wellformed");
    ASSERT_TRUE(t.has_value()) << report_outline(run);

    const auto actual = t->value("actual", std::string{});
    EXPECT_EQ(t->value("status", std::string{}), "FAIL") << actual;

    // The drifting native code is the only one of the three faults that
    // survives every driver manager, so it is the only one asserted outright.
    EXPECT_NE(actual.find("different diagnostics"), std::string::npos)
        << "the drifting native code was not reported; actual was: " << actual;

    // The other two are platform-split, which this scenario found out the hard
    // way: it asserted the length fault, passed on Windows, and failed on
    // Linux and macOS. The driver managers repair *different* halves of the
    // same broken record.
    //
    //   Windows DM   substitutes 'S1000' for the empty SQLSTATE and passes the
    //                bogus length through -> the length fault is visible.
    //   unixODBC     passes the empty SQLSTATE through and recomputes the
    //                length from what the driver wrote -> the SQLSTATE fault
    //                is visible and the length always agrees.
    //
    // The probe is right on both. Asserting that one of the two is named keeps
    // this case honest without pinning it to whichever driver manager the
    // author happened to be running.
    const bool sqlstate_fault =
        actual.find("SQLSTATE is 0 characters") != std::string::npos;
    const bool length_fault =
        actual.find("does not describe the message written") != std::string::npos;
    EXPECT_TRUE(sqlstate_fault || length_fault)
        << "neither the empty SQLSTATE nor the length disagreement was "
           "reported, so nothing but the native code survived the driver "
           "manager; actual was: "
        << actual;
}

TEST_F(CrusherE2EFixture, FailedConnectDiagnosticsSkipWhenNoConnectCanFail) {
    // No DBNAME, DATABASE, PWD, PASSWORD or UID to spoil, so the probe has no
    // way to construct a connect that fails and must say that rather than
    // grade a driver on a test it never ran.
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Connection Tests",
                       "test_failed_connect_diagnostics_are_wellformed");
    ASSERT_TRUE(t.has_value()) << report_outline(run);

    const auto actual = t->value("actual", std::string{});
    EXPECT_EQ(t->value("status", std::string{}), "SKIP_INCONCLUSIVE") << actual;
    EXPECT_NE(actual.find("none of the keywords"), std::string::npos)
        << "the skip has to say why; actual was: " << actual;
}


// ── D68: a driver that omits its terminators has exactly one defect ───────
//
// BufferValidation=Lenient makes the mock return every value correctly, with
// a correct StrLen_or_IndPtr, and overwrite the NUL. There is one probe whose
// job is to grade that - test_null_termination - and D62 broadened the lever
// far enough to prove that twelve *other* probes were also reading the fault,
// because they walked to a terminator while holding the length beside them.
//
// Five reported a failure that was not the driver's; two built SQL out of the
// mangled string and executed `SELECT COUNT(*) FROM CUSTOMERSX`; one printed
// `indicator=5` and the six-character value it had read in the same sentence.
//
// The claim asserted here is per-probe rather than a total, because the
// totals are platform properties: on POSIX the driver managers add failures
// of their own under this configuration (D1, D59, and the note in
// FailOnSeverityDecidesTheExitCode), and asserting "exactly one failure"
// would be the D53 mistake again. None of these probes uses a buffer small
// enough to meet the managers' own overrun, so their statuses are the
// driver's answer on every platform.
TEST_F(CrusherE2EFixture, OmittedTerminatorsAreGradedByOneProbeNotTwelve) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=10;";

    auto clean = run_crusher(base);
    ASSERT_TRUE(clean.report.contains("summary")) << report_outline(clean);
    auto bad = run_crusher(base + "BufferValidation=Lenient;");
    SKIP_IF_KILLED(bad);
    ASSERT_TRUE(bad.report.contains("summary")) << report_outline(bad);

    // The probes D68 fixed, by the category they live in.
    const std::vector<std::pair<std::string, std::string>> graded = {
        {"Data Type Edge Cases",   "test_varchar_empty"},
        {"Data Type Edge Cases",   "test_integer_as_string"},
        {"Data Type Tests",        "test_string_types"},
        {"Error Queue Management", "test_field_extraction"},
        {"Escape Sequence Tests",  "test_call_escape_in_parameter"},
        {"Escape Sequence Tests",  "test_call_escape_inout_parameter"},
        {"Metadata/Catalog Tests", "test_count_star_result_metadata"},
        {"Metadata/Catalog Tests", "test_sqlprocedures_smoke"},
        {"Metadata/Catalog Tests", "test_sqlprocedurecolumns_smoke"},
        {"Statement Tests",        "test_native_sql"},
        {"Statement Tests",        "test_bind_col_string"},
        {"Unicode Tests",          "test_columns_unicode_patterns"},
        {"Unicode Tests",          "test_wchar_roundtrip_non_ascii"},
        {"Unicode Tests",          "test_wchar_surrogate_pair_preserved"},
    };

    size_t compared = 0;
    std::vector<std::string> vanished;
    for (const auto& [category, name] : graded) {
        auto a = find_test(clean.report, category, name);
        auto b = find_test(bad.report, category, name);

        // D79: missing from a category that faulted is D63's vanishing probe,
        // not a drifted name. Counted separately so the drift check below
        // still means what it says.
        if (a.has_value() && !b.has_value() &&
            category_crashed(bad, category)) {
            vanished.push_back(category + " / " + name);
            continue;
        }
        if (!a.has_value() || !b.has_value()) continue;   // named below
        ++compared;
        EXPECT_EQ(a->value("status", std::string{}),
                  b->value("status", std::string{}))
            << category << " / " << name
            << " changed verdict when the driver stopped terminating its "
               "strings, and the value it returned did not change. Read the "
               "length the driver reported (D68).\n  terminating: "
            << a->value("actual", std::string{})
            << "\n  not terminating: " << b->value("actual", std::string{});
    }

    if (!vanished.empty()) {
        // Loud, because a probe that disappears is the one failure mode this
        // whole suite cannot see by looking at statuses (D63).
        std::cout << "[ D79 ] " << vanished.size()
                  << " probe(s) absent because their category faulted under "
                     "BufferValidation=Lenient - D71's fault, not this "
                     "scenario's:\n";
        for (const auto& v : vanished) std::cout << "         " << v << "\n";
    }

    EXPECT_GE(compared + vanished.size(), graded.size() - 2)
        << "most of the probes this asserts about were neither compared nor "
           "accounted for by a crashed category, so the names have drifted\n"
        << report_outline(bad);

    // And the one probe that *must* notice, or the lever is doing nothing.
    auto nul = find_test(bad.report, "Buffer Validation", "test_null_termination");
    ASSERT_TRUE(nul.has_value()) << report_outline(bad);
    auto nul_clean = find_test(clean.report, "Buffer Validation",
                               "test_null_termination");
    ASSERT_TRUE(nul_clean.has_value()) << report_outline(clean);
    EXPECT_NE(nul->value("actual", std::string{}),
              nul_clean->value("actual", std::string{}))
        << "BufferValidation=Lenient reached no probe at all";
}


// ── D74: the diagnostic must survive the case it exists for ───────────────
//
// report_outline is what a scenario prints when something has already gone
// wrong, so the input it has to handle is a run that produced nothing. It did
// not: `run.report["summary"]` on a const json asserts when the report is
// null, and three macOS scenarios turned from `Failed` into
// `Subprocess aborted` - the gtest binary itself dying inside the helper meant
// to explain the failure.
TEST(ReportOutlineTest, SurvivesARunThatProducedNoReportAtAll) {
    CrusherRun nothing;                    // null report, exit code 0
    const std::string out = report_outline(nothing);

    EXPECT_NE(out.find("no summary in the report"), std::string::npos) << out;
    EXPECT_NE(out.find("exit_code="), std::string::npos)
        << "the exit code is the whole reason this is printed; " << out;
    EXPECT_NE(out.find("no categories key at all"), std::string::npos) << out;
}

TEST(ReportOutlineTest, SurvivesAReportThatIsNotAnObject) {
    // A truncated or half-written file can parse as something that is not an
    // object at all. `value()` throws on those exactly as `operator[]` does.
    CrusherRun odd;
    odd.report = nlohmann::json::array({1, 2, 3});
    odd.exit_code = 134;

    const std::string out = report_outline(odd);
    EXPECT_NE(out.find("no summary in the report"), std::string::npos) << out;
    EXPECT_NE(out.find("exit_code=134"), std::string::npos) << out;
}


// ── C9: a failed prepare says which statement failed ──────────────────────
//
// Ten probes opened with the same prepare-INSERT prologue and eight of them
// reported "Could not prepare parameterized INSERT" or "Could not prepare
// statement" on failure - which of the two INSERTs it was, and so whether the
// DDL or the parameter markers were at fault, never reached the report.
//
// FailOn=SQLPrepare is the configuration that makes every one of them fail.
TEST_F(CrusherE2EFixture, AFailedPrepareNamesTheStatementItCouldNotPrepare) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;FailOn=SQLPrepare;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    const nlohmann::json* arrays = nullptr;
    for (const auto& cat : run.report["categories"]) {
        if (cat.value("name", std::string{}).find("Array") != std::string::npos) {
            arrays = &cat;
            break;
        }
    }
    ASSERT_NE(arrays, nullptr) << report_outline(run);

    size_t skipped = 0;
    for (const auto& t : (*arrays)["tests"]) {
        const auto status = t.value("status", std::string{});
        const auto actual = t.value("actual", std::string{});
        const auto name = t.value("test_name", std::string{});

        // C9: a probe that blames array binding for a prepare it never checked
        // is a wrong diagnosis, not a missing one - the shape D68 found twelve
        // of. Nothing here may report one while the prepare is what failed.
        EXPECT_EQ(actual.find("Row-wise binding with PARAMSET_SIZE"),
                  std::string::npos)
            << name << " blamed row-wise binding for a failed prepare: "
            << actual;

        if (status != "SKIP_INCONCLUSIVE") continue;
        if (actual.find("Could not prepare") == std::string::npos) continue;
        ++skipped;
        EXPECT_NE(actual.find("INSERT INTO ODBC_TEST_ARRAY"), std::string::npos)
            << name << " skipped without saying which statement it could not "
               "prepare: " << actual;
    }

    EXPECT_GE(skipped, 6u)
        << "FailOn=SQLPrepare should stop most of this category; if it stopped "
           "almost none, the lever is not reaching the prepare and the rest of "
           "this scenario proves nothing\n"
        << report_outline(run);
}


// ── D78: a W-scoped FailOn stops the ANSI fallback too ────────────────────
//
// D78 gave the mock a lever that fails a Unicode entry point without failing
// its ANSI implementation, so `FailOn=SQLPrepareW` and `FailOn=SQLPrepare` are
// now genuinely different configurations *of the driver*. Through a driver
// manager they are not distinguishable by the application, and that is the
// point worth pinning.
//
// The mock exports W only (D2), so a manager classifies it as a Unicode driver
// and converts every ANSI call into its W form. The tool's six W-then-ANSI
// fallbacks therefore call SQLPrepareW, get an error, fall back to SQLPrepare -
// and the manager turns that into SQLPrepareW as well. Both attempts land on
// the entry point that was named.
//
// So the two runs must be identical, probe for probe. This is a tripwire, not
// coverage: if some driver manager ever dispatches per width, the reports
// diverge and this test forces D78's row to be corrected instead of quietly
// becoming untrue.
TEST_F(CrusherE2EFixture, AWScopedFailOnAlsoStopsTheAnsiFallback) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;FailOn=";

    auto ansi = run_crusher(base + "SQLPrepare;");
    ASSERT_TRUE(ansi.report.contains("summary")) << report_outline(ansi);
    auto wide = run_crusher(base + "SQLPrepareW;");
    ASSERT_TRUE(wide.report.contains("summary")) << report_outline(wide);

    // The lever has to be doing something, or "identical" proves nothing.
    ASSERT_GT(ansi.report["summary"].value("skipped", 0), 10)
        << "FailOn=SQLPrepare stopped almost nothing, so this comparison has "
           "no content\n"
        << report_outline(ansi);

    EXPECT_EQ(ansi.report["summary"].value("skipped", -1),
              wide.report["summary"].value("skipped", -2))
        << "naming the W entry point produced a different number of skips from "
           "naming the ANSI one. That would mean this driver manager dispatches "
           "per width - correct D78's row, which says it does not.";

    size_t compared = 0;
    for (const auto& cat : ansi.report["categories"]) {
        const auto name = cat.value("name", std::string{});
        for (const auto& t : cat["tests"]) {
            auto other = find_test(wide.report, name,
                                   t.value("test_name", std::string{}));
            if (!other.has_value()) continue;
            ++compared;
            EXPECT_EQ(t.value("status", std::string{}),
                      other->value("status", std::string{}))
                << name << " / " << t.value("test_name", std::string{})
                << " graded differently under FailOn=SQLPrepareW than under "
                   "FailOn=SQLPrepare. The tool cannot tell the two apart "
                   "through a driver manager that converts ANSI to W.";
        }
    }
    EXPECT_GT(compared, 150u) << "too few probes compared to mean anything";
}


// ── I6: the isolation probe, and the configuration that fails it ──────────
//
// I6's row said the point of a per-connection write set was that "no
// isolation-level probe can be given a configuration that makes it fail". This
// is that sentence as an assertion.
//
// Three configurations, three different right answers:
//
//   default                          - no dirty read, PASS
//   DirtyReads=true                  - a dirty read while reporting READ
//                                      COMMITTED, which is the defect: FAIL
//   IsolationLevel=ReadUncommitted   - a dirty read while reporting READ
//                                      UNCOMMITTED, which is correct, so the
//                                      probe records it and does not grade it
//
// The third matters as much as the second. A probe that failed a driver for
// showing dirty reads at READ UNCOMMITTED would be grading wrongly, and this
// scenario is what stops that being introduced later.
// D83: a probe that reads the UPDATEd row back, and the configuration that
// fails it.
//
// The finding was that no probe read one back at all, so a driver reporting
// the right SQLRowCount while writing nothing passed the suite clean. The
// second case is the important one and the third is what makes it mean
// something: under `SilentCorruption=DropUpdates` the new probe FAILs and
// `test_sqlrowcount_after_update` still PASSes. That is not a gap in the
// row-count probe - it grades SQLRowCount, and SQLRowCount is right. It is
// the demonstration that grading the count was never going to catch this.
// D86: a driver whose SQLGetData offset outlives its result set, and the
// probe that now notices.
//
// D85 was that defect in this repo's own mock. Fixing it moved the reference
// report by nothing in either configuration, because no probe looked - so a
// third-party driver with the same bug would have passed the suite clean.
// This is that gap closed, and the assertion is the one AGENTS.md step 6
// asks for: a configuration that makes the probe fail.
//
// The second half matters as much as the first. `StaleGetDataOffset` must
// fail this probe and *only* this probe: a mode that broke fetching generally
// would fail plenty of probes and prove nothing about whether this one can
// see its own subject.
TEST_F(CrusherE2EFixture, AStaleGetDataOffsetIsCaughtByReadingACellTwice) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;";
    const char* kProbe = "test_getdata_restarts_in_a_new_result_set";

    auto honest = run_crusher(base);
    ASSERT_TRUE(honest.report.contains("summary")) << report_outline(honest);
    SKIP_IF_KILLED(honest);
    {
        auto t = find_test(honest.report, "Cursor Behavior Tests", kProbe);
        ASSERT_TRUE(t.has_value())
            << "the re-read probe is missing from the report\n"
            << report_outline(honest);
        EXPECT_EQ(t->value("status", std::string{}), "PASS")
            << "a driver that restarts the retrieval must pass\n  actual: "
            << t->value("actual", std::string{});
    }

    auto stale = run_crusher(base + "SilentCorruption=StaleGetDataOffset;");
    ASSERT_TRUE(stale.report.contains("summary")) << report_outline(stale);
    SKIP_IF_KILLED(stale);

    auto t = find_test(stale.report, "Cursor Behavior Tests", kProbe);
    ASSERT_TRUE(t.has_value()) << report_outline(stale);
    EXPECT_EQ(t->value("status", std::string{}), "FAIL")
        << "a retrieval offset carried into the next result set must fail "
           "this probe - if it does not, D85's shape is still undetectable "
           "in someone else's driver\n  actual: "
        << t->value("actual", std::string{});

    // And the mode stays narrow, which is what makes the result
    // attributable. A bound rather than an exact count, because the exact
    // count is not a property of the mode: any probe that reuses one
    // statement handle across two executes and reads the same cell will
    // observe a stale offset too, and how many probes do that depends on how
    // the driver manager routes SQLGetData. Measured at **1 on Windows** and
    // **4 on unixODBC and iODBC** - both correct, and the first draft of this
    // asserted the Windows number as though it were universal.
    //
    // The point survives as a ceiling: a mode that broke fetching generally
    // would fail dozens, and would say nothing about whether this probe can
    // see its own subject. Same shape as the Mode=Success canary's
    // kMaxFailed, which §8 loosened for exactly this reason.
    int failed = 0;
    for (const auto& category : stale.report["categories"]) {
        if (!category.is_object() || !category.contains("tests")) continue;
        for (const auto& probe : category["tests"]) {
            if (!probe.is_object()) continue;
            const auto status = probe.value("status", std::string{});
            if (status == "FAIL" || status == "ERROR") ++failed;
        }
    }
    constexpr int kMaxFailedUnderTheMode = 6;
    EXPECT_GE(failed, 1) << "the mode failed nothing at all";
    EXPECT_LE(failed, kMaxFailedUnderTheMode)
        << "StaleGetDataOffset failed " << failed << " probes. It is meant to "
           "stop one thing being reset; failing this many means it breaks "
           "fetching generally, and a probe that fails under a driver broken "
           "everywhere has not been shown to detect anything in particular.";
}

TEST_F(CrusherE2EFixture, AnUpdateThatChangesNothingIsCaughtByReadingItBack) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;";

    auto honest = run_crusher(base);
    ASSERT_TRUE(honest.report.contains("summary")) << report_outline(honest);
    SKIP_IF_KILLED(honest);
    {
        auto t = find_test(honest.report, "Parameter Binding Tests",
                           "test_update_applies_its_set_clause");
        ASSERT_TRUE(t.has_value())
            << "the read-back probe is missing from the report\n"
            << report_outline(honest);
        EXPECT_EQ(t->value("status", std::string{}), "PASS")
            << "a driver that applies its SET clause must pass\n  actual: "
            << t->value("actual", std::string{});
    }

    auto dropped = run_crusher(base + "SilentCorruption=DropUpdates;");
    ASSERT_TRUE(dropped.report.contains("summary")) << report_outline(dropped);
    SKIP_IF_KILLED(dropped);

    auto readback = find_test(dropped.report, "Parameter Binding Tests",
                              "test_update_applies_its_set_clause");
    ASSERT_TRUE(readback.has_value()) << report_outline(dropped);
    EXPECT_EQ(readback->value("status", std::string{}), "FAIL")
        << "an UPDATE that reports 1 row and writes nothing must fail this "
           "probe - if it does not, the probe has never been shown to detect "
           "anything\n  actual: "
        << readback->value("actual", std::string{});

    // And the reason the read-back had to be a probe of its own.
    auto rowcount = find_test(dropped.report, "Parameter Binding Tests",
                              "test_sqlrowcount_after_update");
    ASSERT_TRUE(rowcount.has_value()) << report_outline(dropped);
    EXPECT_EQ(rowcount->value("status", std::string{}), "PASS")
        << "the row-count probe should still pass here: the count IS right. "
           "If it started failing, DropUpdates would be corrupting the count "
           "as well and would no longer be modelling a silent write loss.";
}

TEST_F(CrusherE2EFixture, IsolationProbeGradesTheClaimNotTheBehaviour) {
    const std::string base =
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;";

    struct Case {
        const char* extra;
        const char* expected_status;
        const char* why;
    };
    const Case cases[] = {
        {"", "PASS",
         "a driver that keeps uncommitted rows to itself must pass"},
        {"DirtyReads=true;", "FAIL",
         "a driver reporting READ COMMITTED while showing another "
         "connection's uncommitted row is the defect this probe exists for - "
         "if this does not fail, the probe has never been shown to detect "
         "anything"},
        {"IsolationLevel=ReadUncommitted;", "INFORMATIONAL",
         "a dirty read at READ UNCOMMITTED is correct, so it must be recorded "
         "and not graded"},
    };

    for (const auto& c : cases) {
        auto run = run_crusher(base + c.extra);
        ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
        SKIP_IF_KILLED(run);

        auto t = find_test(run.report, "Transaction Tests",
                           "test_uncommitted_row_isolation");
        ASSERT_TRUE(t.has_value())
            << "the isolation probe is missing from the report\n"
            << report_outline(run);
        EXPECT_EQ(t->value("status", std::string{}), c.expected_status)
            << "with `" << c.extra << "`: " << c.why
            << "\n  actual: " << t->value("actual", std::string{});
    }
}

// ── U1(a)/Q1: the stride defect the Firebird pair cannot show ─────────────
//
// #299 is a driver that uses the application's `BufferLength` as the
// column-wise element stride for every C type, including the fixed-length ones
// where the specification says it is ignored. An application binding
// `SQL_C_SLONG` with `BufferLength = 0` — which the spec invites, and which
// applications therefore write — gets a stride of zero, and every parameter
// set reads element 0.
//
// The pair cannot demonstrate it. On 3.0.1.21 #308's executor defect runs only
// one set at all, so there is no stride to get wrong, and the probe fails
// earlier for a different reason. Q1's key assertion was therefore correct and
// untested — the shape AGENTS.md step 6 exists to forbid — until the mock grew
// a lever for it.
//
// What makes this the interesting failure rather than an ordinary one: the
// execute returns SQL_SUCCESS, `SQL_ATTR_PARAMS_PROCESSED_PTR` says 3, and
// three rows are in the table. Only their contents are wrong. Before Q1 taught
// `verify_rows_persisted` to return the key column, the probe checked the
// `NAME` axis — which strides correctly, because for `SQL_C_CHAR` the
// BufferLength *is* the element size — and passed.
TEST_F(CrusherE2EFixture, ColumnWiseBindingCatchesAZeroStride) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;ColumnWiseStride=BufferLength;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Array Parameter Tests",
                       "test_column_wise_array_binding");
    ASSERT_TRUE(t.has_value()) << report_outline(run);

    const auto actual = t->value("actual", std::string{});
    EXPECT_EQ(t->value("status", std::string{}), "FAIL") << actual;
    // The keys, named individually. A probe that only counted rows, or only
    // checked the NAME axis, reports nothing here.
    EXPECT_NE(actual.find("100, 100, 100"), std::string::npos)
        << "the collapsed key column was not reported; actual was: " << actual;
    EXPECT_NE(actual.find("succeeded"), std::string::npos)
        << "the report has to say the execute succeeded — silent corruption is "
           "the whole point of this defect; actual was: "
        << actual;
}

// The same probe against the same mock with the stride correct. Without this,
// the case above proves only that *something* makes it fail.
TEST_F(CrusherE2EFixture, ColumnWiseBindingPassesWhenTheStrideIsRight) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;");
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    auto t = find_test(run.report, "Array Parameter Tests",
                       "test_column_wise_array_binding");
    ASSERT_TRUE(t.has_value()) << report_outline(run);
    EXPECT_EQ(t->value("status", std::string{}), "PASS")
        << t->value("actual", std::string{});
}

// ── S5: one run, both reports ────────────────────────────────────────────
//
// `run-crusher` used to invoke crusher twice, once per format. On the U2 Linux
// run against Firebird 3.0.1.21 the first invocation left the server wedged
// and the second — the JSON one, which every triage and every
// `compare_reports.py` diff reads — stopped after a single category where the
// text had captured ten. The half that survived worst was the half that
// mattered.
//
// What has to hold now: `-f` alongside the default `-o console` produces both,
// and they agree. A tee that quietly dropped one child, or wrote a JSON
// covering fewer categories than the text, would reintroduce the whole defect
// while looking fine.
TEST_F(CrusherE2EFixture, OneRunProducesBothTheTextAndTheJson) {
    auto run = run_crusher_tee(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;");
    ASSERT_TRUE(run.launched);

    // The text half.
    EXPECT_NE(run.raw_stdout.find("SUMMARY"), std::string::npos)
        << "stdout carried no console report; the tee dropped its text child";
    EXPECT_NE(run.raw_stdout.find("Connection Tests"), std::string::npos)
        << run.raw_stdout.substr(0, 400);

    // The JSON half, from the same run.
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    EXPECT_TRUE(run.report.value("complete", false))
        << "the JSON was left marked incomplete by a run that finished";

    // And they describe the same run. Every category named in the JSON has to
    // appear in the text: the failure this guards against is a JSON that
    // stopped early while the text went on, which is exactly what the two-run
    // design produced.
    ASSERT_TRUE(run.report.contains("categories"));
    ASSERT_GT(run.report["categories"].size(), 20u)
        << "the JSON covers far fewer categories than a healthy run has";
    for (const auto& category : run.report["categories"]) {
        const auto name = category.value("name", std::string{});
        EXPECT_NE(run.raw_stdout.find(name), std::string::npos)
            << "category '" << name << "' is in the JSON but not the text";
    }

    // G1 still holds: progress chatter goes to stderr, and the text report to
    // stdout. The tee must not have crossed them.
    EXPECT_NE(run.raw_stderr.find("->"), std::string::npos)
        << "no per-probe progress on stderr (S2)";
    EXPECT_EQ(run.raw_stdout.find("  -> "), std::string::npos)
        << "progress chatter leaked into the text report";
}

// ── P16: getting past a category that hangs a driver ─────────────────────
//
// Firebird ODBC 3.0.1.21 on Linux never returns from `SQLCancel` on an idle
// statement. Cancellation Tests is category 11 of 24, so categories 12 to 24
// have never run against that build — including Unicode Tests, where #288's
// widechar heap overflow lives. `--category` cannot express "all but this one"
// without naming the other twenty-three, a list that goes stale the moment a
// category is added.
TEST_F(CrusherE2EFixture, ExcludeCategorySkipsItAndRunsTheRest) {
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;",
        {"--exclude-category", "Cancellation Tests"});
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);

    EXPECT_FALSE(find_category(run.report, "Cancellation Tests").has_value())
        << "the excluded category ran anyway";
    // The point of excluding one is that everything after it still runs.
    EXPECT_TRUE(find_category(run.report, "Unicode Tests").has_value())
        << "a category after the excluded one is missing; the exclusion took "
           "more than it was asked for";
    EXPECT_TRUE(find_category(run.report, "Connection Tests").has_value());
    EXPECT_GT(run.report["categories"].size(), 20u) << report_outline(run);
}

// The silent-no-op shape this project keeps finding. A misspelt name would
// exclude nothing, the run would look exactly as intended, and the category
// meant to be skipped would hang it anyway — so the mistake has to be loud.
TEST_F(CrusherE2EFixture, ExcludeCategoryRefusesANameItDoesNotKnow) {
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;",
        {"--exclude-category", "Cancelation Tests"});   // one 'l'
    EXPECT_EQ(run.exit_code, 3)
        << "a misspelt --exclude-category must fail the run, not silently "
           "exclude nothing; stderr was: "
        << run.raw_stderr;
    EXPECT_NE(run.raw_stderr.find("names no category"), std::string::npos)
        << run.raw_stderr;
    // And it must say what the valid names are, or the user is left guessing.
    EXPECT_NE(run.raw_stderr.find("Cancellation Tests"), std::string::npos)
        << run.raw_stderr;
}

// --category and --exclude-category compose, exclusion applied second.
TEST_F(CrusherE2EFixture, ExcludeCategoryAppliesAfterCategory) {
    auto run = run_crusher_with_args(
        "Driver={Mock ODBC Driver};Mode=Success;Database=Default;"
        "ResultSetSize=10;",
        {"--category", "Connection Tests", "--category", "Unicode Tests",
         "--exclude-category", "Unicode Tests"});
    ASSERT_TRUE(run.report.contains("summary")) << report_outline(run);
    ASSERT_TRUE(run.report.contains("categories"));
    EXPECT_EQ(run.report["categories"].size(), 1u) << report_outline(run);
    EXPECT_TRUE(find_category(run.report, "Connection Tests").has_value());
    EXPECT_FALSE(find_category(run.report, "Unicode Tests").has_value());
}
