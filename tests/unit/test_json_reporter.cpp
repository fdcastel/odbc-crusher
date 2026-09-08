// JsonReporter tests — IMPROVEMENT_PLAN.md F2 and G4.
//
// F2: CI wraps crusher in `timeout --kill-after=30 570`. The report used to be
// serialised only in report_end(), so a run that hung produced no JSON at all
// — precisely when the report matters most. (The text run survives because it
// is piped through tee.) The reporter now snapshots to the output file as
// categories complete, so a SIGKILL leaves a valid document behind.
//
// These tests drive the reporter directly rather than through the binary,
// because the interesting states are the intermediate ones — a subprocess test
// would have to race the kill against the write to observe them.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "reporting/json_reporter.hpp"
#include "tests/test_base.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace odbc_crusher;
namespace fs = std::filesystem;

namespace {

tests::TestResult make(const std::string& name, tests::TestStatus status) {
    tests::TestResult r;
    r.test_name = name;
    r.function = "SQLFoo";
    r.status = status;
    r.severity = tests::Severity::INFO;
    r.conformance = tests::ConformanceLevel::CORE;
    r.expected = "expected";
    r.actual = "actual";
    r.duration = std::chrono::microseconds(1);
    return r;
}

class JsonReporterFixture : public ::testing::Test {
protected:
    void SetUp() override {
        path_ = fs::temp_directory_path() /
                ("crusher_json_test_" +
                 std::to_string(
                     std::chrono::steady_clock::now().time_since_epoch().count()) +
                 ".json");
        std::error_code ec;
        fs::remove(path_, ec);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove(path_, ec);
        auto partial = path_;
        partial += ".partial";
        fs::remove(partial, ec);
    }

    nlohmann::json read_back() const {
        std::ifstream in(path_);
        EXPECT_TRUE(in.is_open()) << path_.string();
        nlohmann::json j;
        in >> j;
        return j;
    }

    bool partial_exists() const {
        auto partial = path_;
        partial += ".partial";
        std::error_code ec;
        return fs::exists(partial, ec);
    }

    fs::path path_;
};

}  // namespace

// D52: the report is the product. A driver that returns a byte which is not
// UTF-8 — which is exactly the driver this tool exists to find — used to take
// the whole document down with it: nlohmann::json::dump() throws
// type_error.316 and crusher wrote nothing at all. Observed on Linux and
// macOS as soon as D2 made the .so export only its W entry points.
TEST_F(JsonReporterFixture, ADriverByteThatIsNotUtf8DoesNotCostTheWholeReport) {
    auto bad = make("t1", tests::TestStatus::FAIL);
    bad.actual = "No NUL within 256 bytes; buffer was mockodbc.dll\x83XX";
    bad.diagnostic = "[HY000] driver said \xFF\xFE";

    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("Buffer Validation", {bad});
    rep.report_summary(1, 0, 1, 0, 0, 0, std::chrono::microseconds(1));
    rep.report_end();

    ASSERT_TRUE(fs::exists(path_)) << "no report written at all";
    const auto j = read_back();                 // throws if it is not JSON
    ASSERT_TRUE(j.contains("summary"));
    EXPECT_EQ(j["summary"].value("failed", -1), 1);

    // The byte is not merely dropped: a driver developer needs to see which
    // one it was and where.
    const auto actual =
        j["categories"][0]["tests"][0].value("actual", std::string{});
    EXPECT_NE(actual.find("mockodbc.dll<0x83>XX"), std::string::npos)
        << "actual was: " << actual;
    const auto diag =
        j["categories"][0]["tests"][0].value("diagnostic", std::string{});
    EXPECT_NE(diag.find("<0xFF><0xFE>"), std::string::npos)
        << "diagnostic was: " << diag;
}

// The same must hold for the snapshots, which is where a killed run's only
// report comes from — and they are written compact, by a different call.
TEST_F(JsonReporterFixture, SnapshotsSurviveANonUtf8ByteToo) {
    auto bad = make("t1", tests::TestStatus::FAIL);
    bad.actual = "trailing \x80";

    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("First", {bad});
    // No report_end() — this is the state a SIGKILL leaves.

    ASSERT_TRUE(fs::exists(path_));
    const auto j = read_back();
    EXPECT_EQ(j["categories"][0]["tests"][0].value("actual", std::string{}),
              "trailing <0x80>");
}

// The core F2 contract: a report exists on disk before the run ends.
TEST_F(JsonReporterFixture, WritesAUsableReportBeforeTheRunFinishes) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("First", {make("t1", tests::TestStatus::PASS)});

    // No report_summary, no report_end — this is the state a SIGKILL leaves.
    ASSERT_TRUE(fs::exists(path_)) << "no report written before report_end()";

    const auto j = read_back();
    EXPECT_EQ(j.value("complete", true), false)
        << "a mid-run snapshot must mark itself incomplete";
    ASSERT_TRUE(j.contains("categories"));
    ASSERT_EQ(j["categories"].size(), 1u);
    EXPECT_EQ(j["categories"][0]["name"], "First");
    EXPECT_EQ(j["categories"][0]["tests"][0]["test_name"], "t1");
    // Header fields must be present in a partial too, or a consumer cannot
    // tell what was being tested.
    EXPECT_EQ(j.value("schema_version", -1), 1);
    EXPECT_TRUE(j.contains("timestamp"));
}

TEST_F(JsonReporterFixture, FinalReportIsMarkedCompleteAndCarriesEverything) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("First", {make("t1", tests::TestStatus::PASS)});
    rep.report_category("Second", {make("t2", tests::TestStatus::FAIL)});
    rep.report_summary(2, 1, 1, 0, 0, 0, std::chrono::microseconds(42));
    rep.report_end();

    const auto j = read_back();
    EXPECT_EQ(j.value("complete", false), true);
    ASSERT_EQ(j["categories"].size(), 2u);
    ASSERT_TRUE(j.contains("summary"));
    EXPECT_EQ(j["summary"]["total_tests"], 2);
    EXPECT_EQ(j["summary"]["failed"], 1);
}

// The snapshot is written to a sibling temp file and renamed over the target,
// so a signal landing mid-write cannot truncate the report. Nothing should be
// left over once the run ends.
TEST_F(JsonReporterFixture, LeavesNoPartialFileBehind) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("First", {make("t1", tests::TestStatus::PASS)});
    EXPECT_FALSE(partial_exists()) << "the .partial should have been renamed away";
    rep.report_summary(1, 1, 0, 0, 0, 0, std::chrono::microseconds(1));
    rep.report_end();
    EXPECT_FALSE(partial_exists());
}

// Snapshots are rate-limited to one per second so that a fast run pays nothing
// (rewriting the whole document 23 times cost ~150 ms and doubled the e2e
// suite). This checks the limiter actually lets a later snapshot through —
// without it, everything after the first category would be lost to a kill.
TEST_F(JsonReporterFixture, SnapshotsResumeAfterTheRateLimitWindow) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("First", {make("t1", tests::TestStatus::PASS)});
    ASSERT_EQ(read_back()["categories"].size(), 1u);

    // Inside the window: no new write, so the file still shows one category.
    rep.report_category("Second", {make("t2", tests::TestStatus::PASS)});
    EXPECT_EQ(read_back()["categories"].size(), 1u)
        << "rate limiter should have suppressed this snapshot";

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    rep.report_category("Third", {make("t3", tests::TestStatus::PASS)});
    EXPECT_EQ(read_back()["categories"].size(), 3u)
        << "after the window, the snapshot must include everything so far";
}

// Writing to stdout has no file to snapshot to; the reporter must not try.
TEST_F(JsonReporterFixture, StdoutModeWritesNoFile) {
    testing::internal::CaptureStdout();
    {
        reporting::JsonReporter rep("");   // empty path => stdout
        rep.report_start("Driver={X}");
        rep.report_category("First", {make("t1", tests::TestStatus::PASS)});
        rep.report_summary(1, 1, 0, 0, 0, 0, std::chrono::microseconds(1));
        rep.report_end();
    }
    const auto out = testing::internal::GetCapturedStdout();

    const auto j = nlohmann::json::parse(out);
    EXPECT_EQ(j.value("complete", false), true);
    ASSERT_EQ(j["categories"].size(), 1u);
    EXPECT_TRUE(j.contains("summary"));
}

// An unwritable path must not crash the run, and must not emit one error line
// per category.
TEST_F(JsonReporterFixture, UnwritablePathIsReportedOnceAndDoesNotThrow) {
    const auto bad = (fs::temp_directory_path() /
                      "crusher_no_such_dir_f2" / "report.json").string();

    testing::internal::CaptureStderr();
    ASSERT_NO_THROW({
        reporting::JsonReporter rep(bad);
        rep.report_start("Driver={X}");
        for (int i = 0; i < 5; ++i) {
            rep.report_category("Cat" + std::to_string(i),
                                {make("t", tests::TestStatus::PASS)});
        }
        rep.report_summary(5, 5, 0, 0, 0, 0, std::chrono::microseconds(1));
        rep.report_end();
    });
    const auto err = testing::internal::GetCapturedStderr();

    size_t errors = 0;
    for (size_t pos = err.find("Error:"); pos != std::string::npos;
         pos = err.find("Error:", pos + 1)) {
        ++errors;
    }
    EXPECT_EQ(errors, 1u) << "stderr was:" << err;
    // And it must not claim success.
    EXPECT_EQ(err.find("JSON report written to:"), std::string::npos) << err;
}

// ── B2: reported, but not scored ──────────────────────────────────────────
//
// Some probes cannot fail and should not: they record what the driver said
// without there being a right answer to grade. Expressing that as PASS put a
// fixed floor of about 13% under every driver's score. INFORMATIONAL results
// are reported like any other and excluded from the pass rate's denominator.

TEST_F(JsonReporterFixture, InformationalResultsAreReportedButNotScored) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("Cat", {
        make("scored_pass", tests::TestStatus::PASS),
        make("scored_fail", tests::TestStatus::FAIL),
        make("not_scored", tests::TestStatus::INFORMATIONAL),
        make("also_not_scored", tests::TestStatus::INFORMATIONAL),
    });
    rep.report_summary(4, 1, 1, 0, 0, 2, std::chrono::microseconds(1));
    rep.report_end();

    const auto j = read_back();
    const auto& s = j["summary"];

    // Every result is still in the report and still counted in the total.
    EXPECT_EQ(s["total_tests"], 4);
    ASSERT_EQ(j["categories"][0]["tests"].size(), 4u);
    EXPECT_EQ(j["categories"][0]["tests"][2]["status"], "INFORMATIONAL");

    // ...but only two of the four were graded.
    EXPECT_EQ(s["informational"], 2);
    EXPECT_EQ(s["scored"], 2);

    // 1 of 2 scored is 50%. Over total_tests it would have been 25%, and
    // counting the informational two as passes would have been 75% — the
    // floor this status exists to remove.
    EXPECT_DOUBLE_EQ(s["pass_rate"].get<double>(), 50.0);
}

// A run that is nothing but informational results has nothing to grade. It
// must not report 100%, and it must not divide by zero either.
TEST_F(JsonReporterFixture, AnEntirelyInformationalRunHasNoPassRate) {
    reporting::JsonReporter rep(path_.string());
    rep.report_start("Driver={X}");
    rep.report_category("Cat", {make("i", tests::TestStatus::INFORMATIONAL)});
    rep.report_summary(1, 0, 0, 0, 0, 1, std::chrono::microseconds(1));
    rep.report_end();

    const auto j = read_back();
    const auto& s = j["summary"];
    EXPECT_EQ(s["scored"], 0);
    EXPECT_DOUBLE_EQ(s["pass_rate"].get<double>(), 0.0);
}
