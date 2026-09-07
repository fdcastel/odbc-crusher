// TestBase::run_test unit tests — IMPROVEMENT_PLAN.md A9.
//
// run_test is the single place every probe's body is invoked from. Before A9
// it caught only core::OdbcError, so any other exception unwound out of the
// category, past crash_guard (which deliberately lets C++ exceptions through),
// to main's `return 3`: all 195 results discarded, no summary printed, and
// with `-o json -f` no report file written at all, because JsonReporter only
// serialises in report_end(). The crash guard then attributed it to the
// driver. These tests pin the contract that a throwing probe costs exactly one
// result and nothing else.
//
// No ODBC driver is needed: TestBase only holds an OdbcConnection reference
// and run_test never touches it.
#include <gtest/gtest.h>

#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_error.hpp"
#include "tests/test_base.hpp"

#include <memory>
#include <new>
#include <stdexcept>
#include <string>

using namespace odbc_crusher;

namespace {

// Minimal concrete TestBase so the protected run_test is reachable. The
// `probe_*` methods below stand in for real probes.
class RunTestProbe : public tests::TestBase {
public:
    using tests::TestBase::TestBase;

    std::vector<tests::TestResult> run() override { return {}; }
    std::string category_name() const override { return "RunTestProbe"; }

    // Exposes run_test with the metadata the assertions care about.
    template <typename Func>
    tests::TestResult call(Func&& body,
                           tests::Severity severity = tests::Severity::INFO,
                           tests::TestStatus on_odbc_error = tests::TestStatus::ERR) {
        return run_test("probe", "SQLFoo", "expected text", severity,
                        tests::ConformanceLevel::CORE, "spec ref",
                        std::forward<Func>(body), on_odbc_error);
    }
};

class RunTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        env_ = std::make_unique<core::OdbcEnvironment>();
        conn_ = std::make_unique<core::OdbcConnection>(*env_);
        probe_ = std::make_unique<RunTestProbe>(*conn_);
    }

    std::unique_ptr<core::OdbcEnvironment> env_;
    std::unique_ptr<core::OdbcConnection> conn_;
    std::unique_ptr<RunTestProbe> probe_;
};

struct NotAnException {
    int value = 7;
};

}  // namespace

TEST_F(RunTestFixture, PassingBodyKeepsTheDefaultPassStatus) {
    auto r = probe_->call([](tests::TestResult& res) { res.actual = "fine"; });

    EXPECT_EQ(r.status, tests::TestStatus::PASS);
    EXPECT_EQ(r.actual, "fine");
    EXPECT_EQ(r.test_name, "probe");
    EXPECT_EQ(r.expected, "expected text");
}

// The core A9 contract: a std::exception becomes one ERR result, and does not
// escape. If this ever regresses, one throwing probe destroys the whole run.
TEST_F(RunTestFixture, StdExceptionBecomesErrAndDoesNotEscape) {
    tests::TestResult r;
    ASSERT_NO_THROW({
        r = probe_->call([](tests::TestResult&) {
            throw std::runtime_error("probe blew up");
        });
    });

    EXPECT_EQ(r.status, tests::TestStatus::ERR);
    EXPECT_NE(r.actual.find("probe blew up"), std::string::npos)
        << "actual was: " << r.actual;
    ASSERT_TRUE(r.diagnostic.has_value());
    EXPECT_NE(r.diagnostic->find("not a finding about the driver"),
              std::string::npos)
        << "The diagnostic must not blame the driver for a crusher defect.";
}

// std::bad_alloc is the case the plan calls out by name: it is thrown from
// deep inside a container operation, nowhere near a try block a probe author
// would think to write.
TEST_F(RunTestFixture, BadAllocBecomesErrAndDoesNotEscape) {
    tests::TestResult r;
    ASSERT_NO_THROW({
        r = probe_->call([](tests::TestResult&) { throw std::bad_alloc(); });
    });

    EXPECT_EQ(r.status, tests::TestStatus::ERR);
    EXPECT_FALSE(r.actual.empty());
}

TEST_F(RunTestFixture, NonStdExceptionBecomesErrAndDoesNotEscape) {
    tests::TestResult r;
    ASSERT_NO_THROW({
        r = probe_->call([](tests::TestResult&) { throw NotAnException{}; });
    });

    EXPECT_EQ(r.status, tests::TestStatus::ERR);
    EXPECT_NE(r.actual.find("non-std::exception"), std::string::npos)
        << "actual was: " << r.actual;
}

// An ERR result must be ranked as such in the severity-ordered summary, or it
// sorts below WARNING noise and gets missed.
TEST_F(RunTestFixture, ThrowingProbeUpgradesInfoSeverityToErr) {
    auto r = probe_->call(
        [](tests::TestResult&) { throw std::runtime_error("x"); },
        tests::Severity::INFO);

    EXPECT_EQ(r.severity, tests::Severity::ERR);
}

// ...but a caller that deliberately said CRITICAL keeps it.
TEST_F(RunTestFixture, ThrowingProbeDoesNotDowngradeCriticalSeverity) {
    auto r = probe_->call(
        [](tests::TestResult&) { throw std::runtime_error("x"); },
        tests::Severity::CRITICAL);

    EXPECT_EQ(r.severity, tests::Severity::CRITICAL);
}

// A non-OdbcError is always ERR — it is a crusher defect, never a driver
// verdict — even for a probe that asked for OdbcError to mean FAIL.
TEST_F(RunTestFixture, StdExceptionIsErrEvenWhenOdbcErrorWouldMeanFail) {
    auto r = probe_->call(
        [](tests::TestResult&) { throw std::runtime_error("x"); },
        tests::Severity::INFO, tests::TestStatus::FAIL);

    EXPECT_EQ(r.status, tests::TestStatus::ERR);
}

// The pre-existing OdbcError behaviour must survive the new handlers: the
// std::exception catch is listed after it, and OdbcError derives from
// std::runtime_error, so getting the order wrong would silently reclassify
// every driver error as a crusher defect.
TEST_F(RunTestFixture, OdbcErrorStillHonoursOnOdbcErrorStatus) {
    auto r = probe_->call(
        [](tests::TestResult&) {
            throw core::OdbcError("driver said no");
        },
        tests::Severity::INFO, tests::TestStatus::FAIL);

    EXPECT_EQ(r.status, tests::TestStatus::FAIL);
    EXPECT_EQ(r.severity, tests::Severity::ERR);
    EXPECT_NE(r.actual.find("driver said no"), std::string::npos);
}

// Timing must still be recorded on the throwing path — the duration_cast used
// to sit after the catch blocks and could be skipped by a bad refactor.
TEST_F(RunTestFixture, DurationIsRecordedEvenWhenTheBodyThrows) {
    auto r = probe_->call([](tests::TestResult&) {
        throw std::runtime_error("x");
    });

    EXPECT_GE(r.duration.count(), 0);
}
