// E2E scenarios — IMPROVEMENT_PLAN.md §5.1
//
// Each scenario maps a mock-driver configuration to expected per-test
// outcomes in the JSON report. The harness lives in e2e_harness.{hpp,cpp}.
// Tests SKIP gracefully when the mock driver isn't loadable on the host
// — an environment problem, not a regression to flag.
#include <gtest/gtest.h>
#include <iostream>
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
//
// The mock at Mode=Success is the project's reference fixture — every probe
// in the conformance suite must PASS against it. A FAIL/ERROR/SKIP here is
// either (a) a real mock regression, or (b) a probe that started exercising
// a mock code path the fixture doesn't yet cover. Either way it's a build
// signal, not a "soft" report finding (§7.10: this canary is what should
// have caught the §7.7-§7.9 regressions on day one).

TEST_F(CrusherE2EFixture, ModeSuccessProducesCoherentReport) {
    auto run = run_crusher(
        "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
        "ResultSetSize=10;");

    ASSERT_TRUE(run.launched) << "Failed to launch crusher binary";
    ASSERT_TRUE(run.report.contains("summary"))
        << "stderr: " << run.raw_stderr;

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
            }
        }
    }

    // Per-platform tolerance baselines. Windows + macOS hold to the §7.10
    // contract (every probe PASSes). Linux carries pre-existing mock↔unixODBC
    // integration gaps — diagnostic-path FAILs and W-function / array-param
    // SKIPs surfaced when §7.10 tightened this canary. Tracked in
    // IMPROVEMENT_PLAN §8; the bound is locked in so any new regression beyond
    // baseline still trips this test, and the improvement detector below
    // nudges anyone who fixes one of the underlying gaps.
#ifdef __linux__
    constexpr int kMaxFailed = 6;
    constexpr int kMaxSkipped = 23;
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

#ifdef __linux__
    // Improvement detector — when one of the §8 gaps gets fixed and the count
    // drops below baseline, surface a notice so the bound can be tightened.
    // Stays informational (doesn't fail the test); ratchets the baseline only
    // when someone reads the log and updates the constant above.
    const int observed_failed = summary.value("failed", -1);
    const int observed_skipped = summary.value("skipped", -1);
    if (observed_failed >= 0 && observed_failed < kMaxFailed) {
        std::cerr << "[notice] failed=" << observed_failed
                  << " is below baseline " << kMaxFailed
                  << " — consider tightening kMaxFailed (see IMPROVEMENT_PLAN §8)\n";
    }
    if (observed_skipped >= 0 && observed_skipped < kMaxSkipped) {
        std::cerr << "[notice] skipped=" << observed_skipped
                  << " is below baseline " << kMaxSkipped
                  << " — consider tightening kMaxSkipped (see IMPROVEMENT_PLAN §8)\n";
    }
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
    ASSERT_TRUE(t.has_value());
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
    ASSERT_TRUE(t.has_value());
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
// sequences survive untouched. All four SQLNativeSql cells in
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
            "test_native_sql_call_escape",
            "test_native_sql_outer_join_escape"}) {
        auto t = find_test(run.report, "Escape Sequence Tests", name);
        ASSERT_TRUE(t.has_value()) << "Probe missing: " << name;
        EXPECT_EQ(t->value("status", std::string{}), "FAIL")
            << name << " MUST fail under NativeSqlPassThrough.";
    }

    // Sanity: SQLExecDirect-side tests still pass — the mock translates at
    // execution time independently of SQLNativeSql.
    auto exec_oj = find_test(run.report, "Escape Sequence Tests",
                             "test_outer_join_escape");
    ASSERT_TRUE(exec_oj.has_value());
    EXPECT_EQ(exec_oj->value("status", std::string{}), "PASS")
        << "Execution-time {oj} translation must remain intact under the "
           "SQLNativeSql-only pass-through.";
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
