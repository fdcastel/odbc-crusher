// E2E scenarios — IMPROVEMENT_PLAN.md §5.1
//
// Each scenario maps a mock-driver configuration to expected per-test
// outcomes in the JSON report. The harness lives in e2e_harness.{hpp,cpp}.
// Tests SKIP gracefully when the mock driver isn't loadable on the host
// — an environment problem, not a regression to flag.
#include <gtest/gtest.h>
#include "e2e_harness.hpp"

using namespace odbc_crusher::e2e;

namespace {

class CrusherE2EFixture : public ::testing::Test {
protected:
    void SetUp() override {
        if (!has_runnable_mock()) {
            GTEST_SKIP() << "Mock ODBC Driver not loadable on this host — "
                            "register mock-driver/build/<config>/mockodbc.dll first.";
        }
    }
};

} // namespace

// ── Mode=Success: baseline run produces a coherent report ──────────────────

TEST_F(CrusherE2EFixture, ModeSuccessProducesCoherentReport) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched) << "Failed to launch crusher binary";
    ASSERT_TRUE(run.report.contains("summary"))
        << "stderr: " << run.raw_stderr;

    const auto& summary = run.report["summary"];
    EXPECT_GT(summary.value("total_tests", 0), 100)
        << "Expected the full conformance suite to run";
    EXPECT_GT(summary.value("passed", 0), 0)
        << "At least some tests must pass against a healthy mock";

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
    ASSERT_TRUE(t.has_value());
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
}
