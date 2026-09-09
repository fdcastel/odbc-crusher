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

#include "reporting/console_reporter.hpp"
#include "reporting/json_reporter.hpp"
#include "reporting/tee_reporter.hpp"
#include "tests/test_base.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
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
    // Against the constant, not a literal: what this case is about is that a
    // snapshot is versioned like the final report, not which version it is.
    EXPECT_EQ(j.value("schema_version", -1),
              reporting::JsonReporter::kSchemaVersion);
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

// ── D82: a report the tool cannot parse must not be handed over ───────────
//
// Recurred on macOS with the bytes captured: `"categories": [` followed
// immediately by a bare `,`, because element 0 serialised to *zero
// characters*. nlohmann does that for a value whose `m_type` matches no case
// in its dump switch - the default arm's JSON_ASSERT is compiled out under
// NDEBUG - which means a corrupt node, most likely from D71's wild write in
// the driver manager. crusher wrote it, printed "JSON report written to:",
// and exited.
//
// A `discarded` value stands in for the corrupt one here. It is not the same
// value, but it is the same problem stated in a way a test can construct: it
// dumps to `<discarded>`, which is not JSON, so it exercises the same path.

TEST(JsonReportSalvage, AnUnserialisableCategoryIsReplacedNotShipped) {
    nlohmann::json categories = nlohmann::json::array();

    nlohmann::json good = nlohmann::json::object();
    good["name"] = "Statement Tests";
    good["tests"] = nlohmann::json::array();

    categories.push_back(nlohmann::json(nlohmann::json::value_t::discarded));
    categories.push_back(good);

    // Before: the array does not survive a round-trip at all.
    ASSERT_FALSE(nlohmann::json::accept(categories.dump()))
        << "the fixture must start from a document that really is broken";

    const size_t replaced =
        reporting::quarantine_unserialisable(categories);

    EXPECT_EQ(replaced, 1u);
    EXPECT_TRUE(nlohmann::json::accept(categories.dump()))
        << "the salvaged document still does not parse";
    ASSERT_EQ(categories.size(), 2u) << "the good category was lost too";
    EXPECT_EQ(categories[1]["name"], "Statement Tests")
        << "salvaging must not disturb the categories that were fine";
    EXPECT_NE(categories[0]["name"].get<std::string>().find("unserialisable"),
              std::string::npos)
        << "the replacement has to say what happened: "
        << categories[0].dump();
}

TEST(JsonReportSalvage, ACleanReportIsLeftExactlyAsItWas) {
    nlohmann::json categories = nlohmann::json::array();
    nlohmann::json one = nlohmann::json::object();
    one["name"] = "Connection Tests";
    one["tests"] = nlohmann::json::array();
    categories.push_back(one);

    const nlohmann::json before = categories;
    EXPECT_EQ(reporting::quarantine_unserialisable(categories), 0u);
    EXPECT_EQ(categories, before)
        << "the salvage pass rewrote a report that was already valid";
}

TEST(JsonReportSalvage, ANonArrayIsNotTouched) {
    nlohmann::json not_an_array = nlohmann::json::object();
    EXPECT_EQ(reporting::quarantine_unserialisable(not_an_array), 0u);
}

// The end-to-end shape: a reporter whose accumulated categories hold a bad
// node still writes a file that parses, and says how many it lost.
TEST_F(JsonReporterFixture, AReportWithABadNodeIsStillReadable) {
    reporting::JsonReporter reporter(path_.string());
    reporter.report_start("Driver={Mock};");
    reporter.report_category("Statement Tests",
                             {make("t1", tests::TestStatus::PASS)});
    reporter.report_summary(1, 1, 0, 0, 0, 0, std::chrono::microseconds(1));
    reporter.report_end();

    // The reporter's own output must parse - that is the baseline this
    // guards, and read_back() would throw if it did not.
    auto doc = read_back();
    EXPECT_TRUE(doc.contains("categories"));
    EXPECT_FALSE(doc.contains("quarantined_categories"))
        << "a clean run must not claim it quarantined anything";
}

// ── S4: what produced the report, and what must not be in it ─────────────
//
// Two reports of the same driver are only comparable if the same crusher on
// the same platform produced both, and until schema 2 neither report said so
// — a rebuilt binary or a different runner image would have read as a driver
// regression with nothing to contradict it.
//
// The redaction is the other half. These reports are published as CI
// artifacts, and the Firebird pair's carried `PWD=masterkey` in clear. The
// connection string is the most useful single line in a report when two runs
// disagree, so the value is masked rather than the key dropped.
TEST_F(JsonReporterFixture, TheReportSaysWhatProducedIt) {
    reporting::JsonReporter reporter(path_.string());
    reporter.report_start("Driver={X};DBNAME=db;");
    reporter.report_summary(0, 0, 0, 0, 0, 0, std::chrono::microseconds(0));
    reporter.report_end();

    auto j = read_back();
    ASSERT_TRUE(j.contains("environment")) << j.dump(1);
    const auto& env = j["environment"];
    EXPECT_EQ(j.value("schema_version", 0),
              reporting::JsonReporter::kSchemaVersion);
    EXPECT_FALSE(env.value("crusher_version", std::string{}).empty());
    EXPECT_NE(env.value("platform", std::string{}), "unknown");
    // Not asserted as a value — the point is that the report carries the two
    // ABI widths at all, because a driver writing 32 bits of a 64-bit SQLLEN
    // is what P6 found and neither width is implied by the other.
    EXPECT_GT(env.value("pointer_bits", 0), 0);
    EXPECT_GT(env.value("sqllen_bits", 0), 0);
}

TEST_F(JsonReporterFixture, ThePasswordDoesNotReachThePublishedReport) {
    reporting::JsonReporter reporter(path_.string());
    reporter.report_start(
        "Driver={Firebird ODBC Driver};DBNAME=localhost:/db.fdb;UID=SYSDBA;"
        "PWD=masterkey;CHARSET=UTF8;");
    reporter.report_summary(0, 0, 0, 0, 0, 0, std::chrono::microseconds(0));
    reporter.report_end();

    const auto conn = read_back().value("connection_string", std::string{});
    EXPECT_EQ(conn.find("masterkey"), std::string::npos) << conn;
    EXPECT_NE(conn.find("PWD=***"), std::string::npos) << conn;
    // Everything else is still legible, which is the reason for masking the
    // value rather than dropping the key.
    EXPECT_NE(conn.find("DBNAME=localhost:/db.fdb"), std::string::npos) << conn;
    EXPECT_NE(conn.find("UID=SYSDBA"), std::string::npos) << conn;
    EXPECT_NE(conn.find("CHARSET=UTF8"), std::string::npos) << conn;
}

// A keyword that merely ends in a secret name is not one. `NEWPWD` is a real
// ODBC keyword (SQLDriverConnect uses it to change an expiring password), so
// the boundary check is load-bearing rather than defensive.
TEST_F(JsonReporterFixture, RedactionMatchesWholeKeywordsOnly) {
    reporting::JsonReporter reporter(path_.string());
    reporter.report_start("Driver={X};UID=u;PWD=old_one;NEWPWD=new_one;DB=keepme;");
    reporter.report_summary(0, 0, 0, 0, 0, 0, std::chrono::microseconds(0));
    reporter.report_end();

    const auto conn = read_back().value("connection_string", std::string{});
    EXPECT_EQ(conn.find("old_one"), std::string::npos) << conn;
    EXPECT_EQ(conn.find("new_one"), std::string::npos) << conn;
    EXPECT_NE(conn.find("NEWPWD=***"), std::string::npos) << conn;
    EXPECT_NE(conn.find("DB=keepme"), std::string::npos) << conn;
}

// ── S5: one run, both reports ────────────────────────────────────────────
//
// run-crusher used to invoke crusher twice, once per format. On the U2 Linux
// run against Firebird 3.0.1.21 the first invocation wedged the server and the
// second one — the JSON one, the machine-readable one every triage reads —
// stopped after a single category where the text had captured ten.
//
// TeeReporter removes the second invocation. What has to hold is that neither
// child loses anything by sharing a run: the JSON file is byte-for-byte what a
// JSON-only run would have written, and the console child still writes its
// text. Asserted by driving the tee and a lone JsonReporter through the same
// calls and comparing the documents.
TEST_F(JsonReporterFixture, TeeWritesTheSameJsonAsAJsonOnlyRun) {
    auto solo_path = path_;
    solo_path += ".solo";

    const auto drive = [](reporting::Reporter& r) {
        r.report_start("Driver={X};PWD=secret;");
        r.report_category("Cat A", {make("t1", tests::TestStatus::PASS),
                                    make("t2", tests::TestStatus::FAIL)});
        r.report_category("Cat B", {make("t3", tests::TestStatus::PASS)});
        r.report_summary(3, 2, 1, 0, 0, 0, std::chrono::microseconds(7));
        r.report_end();
    };

    {
        reporting::JsonReporter solo(solo_path.string());
        drive(solo);
    }
    {
        std::ostringstream swallowed;
        reporting::TeeReporter tee(
            std::make_unique<reporting::JsonReporter>(path_.string()),
            std::make_unique<reporting::ConsoleReporter>(swallowed, false));
        drive(tee);
        EXPECT_FALSE(swallowed.str().empty())
            << "the console child wrote nothing, so the tee is not forwarding "
               "to both";
    }

    nlohmann::json solo_json;
    { std::ifstream in(solo_path); ASSERT_TRUE(in.is_open()); in >> solo_json; }
    auto teed_json = read_back();

    // The timestamp is wall-clock and may differ by a second between the two
    // runs; nothing else may.
    solo_json.erase("timestamp");
    teed_json.erase("timestamp");
    EXPECT_EQ(solo_json, teed_json)
        << "teed:\n" << teed_json.dump(1) << "\nsolo:\n" << solo_json.dump(1);

    std::error_code ec;
    fs::remove(solo_path, ec);
}

// The console child must not be able to take the JSON down with it, and the
// JSON is written first for exactly that reason — a run killed mid-report has
// already updated the file on disk.
TEST_F(JsonReporterFixture, TeeWritesTheJsonBeforeTheConsole) {
    std::ostringstream text;
    reporting::TeeReporter tee(
        std::make_unique<reporting::JsonReporter>(path_.string()),
        std::make_unique<reporting::ConsoleReporter>(text, false));
    tee.report_start("Driver={X};");
    tee.report_category("Cat A", {make("t1", tests::TestStatus::PASS)});

    // No report_end() — this is the state a SIGKILL leaves. The snapshot is
    // already on disk, and it carries the category.
    auto j = read_back();
    ASSERT_TRUE(j.contains("categories")) << j.dump(1);
    ASSERT_EQ(j["categories"].size(), 1u) << j.dump(1);
    EXPECT_EQ(j["categories"][0].value("name", std::string{}), "Cat A");
    EXPECT_FALSE(j.value("complete", true))
        << "a snapshot must not claim to be a finished report";
}
