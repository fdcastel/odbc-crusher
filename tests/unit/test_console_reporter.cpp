// ConsoleReporter tests — IMPROVEMENT_PLAN.md G5.
//
// G5 had two halves. The warning half (C4100 on the unused connection_string,
// two C4267 narrowings in the padding arithmetic) was fixed under E1, and /WX
// is its regression test. This file covers the other half: report_summary()
// needs only the FAIL/ERR results, but report_category() used to keep a full
// second copy of every TestResult — 195 of them against the mock — to build a
// list that is empty on a clean run. These tests pin the observable behaviour
// so that filtering on the way in cannot quietly change what gets printed.
#include <gtest/gtest.h>

#include "reporting/console_reporter.hpp"
#include "tests/test_base.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <vector>

using namespace odbc_crusher;

namespace {

tests::TestResult make(const std::string& name, tests::TestStatus status,
                       tests::Severity severity) {
    tests::TestResult r;
    r.test_name = name;
    r.function = "SQLFoo";
    r.status = status;
    r.severity = severity;
    r.conformance = tests::ConformanceLevel::CORE;
    r.expected = "expected";
    r.actual = "actual for " + name;
    r.duration = std::chrono::microseconds(1);
    return r;
}

std::string summary_of(const std::vector<tests::TestResult>& results) {
    std::ostringstream out;
    reporting::ConsoleReporter rep(out, /*verbose=*/false);
    rep.report_category("Cat", results);
    size_t failed = 0, errors = 0, skipped = 0, passed = 0, informational = 0;
    for (const auto& r : results) {
        switch (r.status) {
            case tests::TestStatus::PASS: ++passed; break;
            case tests::TestStatus::FAIL: ++failed; break;
            case tests::TestStatus::ERR: ++errors; break;
            case tests::TestStatus::INFORMATIONAL: ++informational; break;  // B2
            default: ++skipped; break;
        }
    }
    rep.report_summary(results.size(), passed, failed, skipped, errors,
                       informational, std::chrono::microseconds(100));
    rep.report_end();
    return out.str();
}

}  // namespace

TEST(ConsoleReporterTest, CleanRunPrintsNoFailureSection) {
    auto text = summary_of({
        make("a", tests::TestStatus::PASS, tests::Severity::INFO),
        make("b", tests::TestStatus::SKIP_UNSUPPORTED, tests::Severity::WARNING),
    });

    EXPECT_EQ(text.find("FAILURES BY SEVERITY"), std::string::npos) << text;
    EXPECT_NE(text.find("ALL TESTS PASSED"), std::string::npos) << text;
}

TEST(ConsoleReporterTest, FailuresAndErrorsBothReachTheSummary) {
    auto text = summary_of({
        make("passing", tests::TestStatus::PASS, tests::Severity::INFO),
        make("failing", tests::TestStatus::FAIL, tests::Severity::ERR),
        make("erroring", tests::TestStatus::ERR, tests::Severity::CRITICAL),
        make("skipped", tests::TestStatus::SKIP_INCONCLUSIVE, tests::Severity::INFO),
    });

    ASSERT_NE(text.find("FAILURES BY SEVERITY"), std::string::npos) << text;
    EXPECT_NE(text.find("actual for failing"), std::string::npos) << text;
    EXPECT_NE(text.find("actual for erroring"), std::string::npos) << text;

    // Non-failures must not be listed there. "passing" and "skipped" still
    // appear in the per-category listing above, so search the summary section.
    const auto section = text.substr(text.find("FAILURES BY SEVERITY"));
    EXPECT_EQ(section.find("actual for passing"), std::string::npos) << section;
    EXPECT_EQ(section.find("actual for skipped"), std::string::npos) << section;
}

// The summary is the thing a driver developer reads first, so CRITICAL has to
// come before ERR regardless of the order the categories ran in.
TEST(ConsoleReporterTest, FailureSummaryIsOrderedBySeverity) {
    auto text = summary_of({
        make("warn_one", tests::TestStatus::FAIL, tests::Severity::WARNING),
        make("critical_one", tests::TestStatus::FAIL, tests::Severity::CRITICAL),
        make("err_one", tests::TestStatus::FAIL, tests::Severity::ERR),
    });

    const auto section = text.substr(text.find("FAILURES BY SEVERITY"));
    const auto crit = section.find("critical_one");
    const auto err = section.find("err_one");
    const auto warn = section.find("warn_one");
    ASSERT_NE(crit, std::string::npos) << section;
    ASSERT_NE(err, std::string::npos) << section;
    ASSERT_NE(warn, std::string::npos) << section;
    EXPECT_LT(crit, err) << section;
    EXPECT_LT(err, warn) << section;
}

// Results arriving across several report_category() calls must accumulate.
TEST(ConsoleReporterTest, FailuresAccumulateAcrossCategories) {
    std::ostringstream out;
    reporting::ConsoleReporter rep(out, false);
    rep.report_category("First", {make("f1", tests::TestStatus::FAIL,
                                       tests::Severity::ERR)});
    rep.report_category("Second", {make("f2", tests::TestStatus::ERR,
                                        tests::Severity::CRITICAL)});
    rep.report_summary(2, 0, 1, 0, 1, 0, std::chrono::microseconds(1));
    rep.report_end();

    const auto text = out.str();
    const auto section = text.substr(text.find("FAILURES BY SEVERITY"));
    EXPECT_NE(section.find("f1"), std::string::npos) << section;
    EXPECT_NE(section.find("f2"), std::string::npos) << section;
}

// A long category name plus a long summary must not make the padding
// arithmetic wrap — this is the C4267 site E1 rewrote as guarded size_t math.
TEST(ConsoleReporterTest, OverlongCategoryNameDoesNotWrapThePadding) {
    std::ostringstream out;
    reporting::ConsoleReporter rep(out, false);
    const std::string long_name(200, 'X');
    rep.report_category(long_name, {
        make("a", tests::TestStatus::PASS, tests::Severity::INFO),
        make("b", tests::TestStatus::FAIL, tests::Severity::ERR),
        make("c", tests::TestStatus::SKIP_UNSUPPORTED, tests::Severity::INFO),
        make("d", tests::TestStatus::ERR, tests::Severity::CRITICAL),
    });

    const auto text = out.str();
    ASSERT_NE(text.find(long_name), std::string::npos);
    // A wrapped size_t would ask std::string for ~2^64 spaces and throw.
    // Reaching here at all is most of the assertion; check the line is sane.
    const auto start = text.find(long_name);
    const auto eol = text.find('\n', start);
    ASSERT_NE(eol, std::string::npos);
    EXPECT_LT(eol - start, 1024u) << "padding line looks wrong";
}

// ── B2: reported, but not scored ──────────────────────────────────────────

TEST(ConsoleReporterTest, InformationalResultsAreNamedAndKeptOutOfThePassRate) {
    auto text = summary_of({
        make("scored_pass", tests::TestStatus::PASS, tests::Severity::INFO),
        make("not_scored", tests::TestStatus::INFORMATIONAL, tests::Severity::INFO),
    });

    // The percentage is over what was graded — one of one, not one of two.
    EXPECT_NE(text.find("100.0% of 1 scored"), std::string::npos) << text;
    EXPECT_NE(text.find("Informational: 1 (reported, not scored)"),
              std::string::npos) << text;

    // And it is not a failure: a run of passes and informationals is clean.
    EXPECT_NE(text.find("ALL TESTS PASSED"), std::string::npos) << text;
}

// The per-result tag has to be distinguishable at a glance from PASS, or the
// status changes nothing for someone reading the console output.
TEST(ConsoleReporterTest, InformationalHasItsOwnTag) {
    std::ostringstream out;
    reporting::ConsoleReporter rep(out, /*verbose=*/true);
    rep.report_category("Cat",
                        {make("i", tests::TestStatus::INFORMATIONAL,
                              tests::Severity::INFO)});
    const auto text = out.str();
    EXPECT_NE(text.find("[INFO]"), std::string::npos) << text;
    EXPECT_EQ(text.find("[PASS]"), std::string::npos) << text;
}
