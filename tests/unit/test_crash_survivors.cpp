// S3 — what a crashed category leaves behind. IMPROVEMENT_PLAN_V2.md.
//
// Every category is written as `return { probe_a(), probe_b(), … }`, so the
// vector does not exist until the last probe returns. A driver fault in probe C
// therefore destroys A's and B's verdicts as well: the crash guard catches the
// fault, but there is nothing to report. On Firebird 3.0.1.21 that removed all
// five Descriptor Tests probes from the report, which is why the two runs of
// the H17 pair differ by four in their totals and why both triage passes had to
// caveat the baseline pass rate as "optimistic by an unknown margin".
//
// TestBase now keeps a copy of each verdict as it is reached, and remembers the
// probe it started last. These tests pin that mechanism. The *wiring* — main.cpp
// reading them when the crash guard fires — is exercised by the Firebird pair
// rather than here: faulting the process on purpose is not something to ship in
// the mock, and the real driver already provides the fault.
//
// No ODBC driver is needed; run_test never touches the connection.
#include <gtest/gtest.h>

#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_error.hpp"
#include "tests/test_base.hpp"

#include <memory>
#include <stdexcept>
#include <string>

using namespace odbc_crusher;

namespace {

class SurvivorProbe : public tests::TestBase {
public:
    using tests::TestBase::TestBase;

    std::vector<tests::TestResult> run() override { return {}; }
    std::string category_name() const override { return "SurvivorProbe"; }

    template <typename Func>
    tests::TestResult call(const std::string& name, Func&& body) {
        return run_test(name, "SQLFoo", "expected text", tests::Severity::INFO,
                        tests::ConformanceLevel::CORE, "spec ref",
                        std::forward<Func>(body));
    }
};

// TestBase holds a reference, so the connection must outlive the probe. It is
// never dialled.
struct CrashSurvivors : public ::testing::Test {
    core::OdbcEnvironment env;
    std::unique_ptr<core::OdbcConnection> conn{
        std::make_unique<core::OdbcConnection>(env)};
    std::unique_ptr<SurvivorProbe> probe{
        std::make_unique<SurvivorProbe>(*conn)};
};

}  // namespace

TEST_F(CrashSurvivors, NothingRunYetMeansNothingToSalvage) {
    EXPECT_TRUE(probe->completed_results().empty());
    EXPECT_TRUE(probe->last_probe_started().empty());
}

TEST_F(CrashSurvivors, EveryCompletedProbeIsKept) {
    probe->call("probe_a", [](tests::TestResult&) {});
    probe->call("probe_b", [](tests::TestResult& r) {
        r.status = tests::TestStatus::FAIL;
    });

    ASSERT_EQ(probe->completed_results().size(), 2u);
    EXPECT_EQ(probe->completed_results()[0].test_name, "probe_a");
    EXPECT_EQ(probe->completed_results()[1].test_name, "probe_b");
    // The verdict is kept, not just the name — the point is that a crash in a
    // later probe must not turn an earlier FAIL into a missing row.
    EXPECT_EQ(probe->completed_results()[1].status, tests::TestStatus::FAIL);
}

TEST_F(CrashSurvivors, LastStartedNamesTheProbeInFlight) {
    probe->call("probe_a", [](tests::TestResult&) {});
    EXPECT_EQ(probe->last_probe_started(), "probe_a");

    // Set *before* the body runs, so it names the probe that is executing and
    // not the last one that finished — the distinction that matters when the
    // body never returns.
    probe->call("probe_b", [&](tests::TestResult&) {
        EXPECT_EQ(probe->last_probe_started(), "probe_b");
    });
    EXPECT_EQ(probe->last_probe_started(), "probe_b");
}

// A probe whose body throws still completes, via run_test's own catch — so it
// is a survivor, not a casualty, and must be kept with its ERR verdict.
TEST_F(CrashSurvivors, AThrowingProbeIsStillASurvivor) {
    probe->call("probe_a", [](tests::TestResult&) {});
    probe->call("probe_throws", [](tests::TestResult&) {
        throw std::runtime_error("probe defect");
    });

    ASSERT_EQ(probe->completed_results().size(), 2u);
    EXPECT_EQ(probe->completed_results()[1].test_name, "probe_throws");
    EXPECT_EQ(probe->completed_results()[1].status, tests::TestStatus::ERR);
    EXPECT_EQ(probe->last_probe_started(), "probe_throws");
}

// The salvage list is the category's own, not a process-wide accumulator: two
// categories running in sequence must not inherit each other's results.
TEST_F(CrashSurvivors, EachCategoryKeepsItsOwnSurvivors) {
    probe->call("probe_a", [](tests::TestResult&) {});

    SurvivorProbe other(*conn);
    EXPECT_TRUE(other.completed_results().empty());
    other.call("probe_z", [](tests::TestResult&) {});

    EXPECT_EQ(other.completed_results().size(), 1u);
    EXPECT_EQ(probe->completed_results().size(), 1u);
    EXPECT_EQ(other.completed_results()[0].test_name, "probe_z");
    EXPECT_EQ(probe->completed_results()[0].test_name, "probe_a");
}
