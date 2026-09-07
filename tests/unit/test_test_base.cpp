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

#include <algorithm>
#include <cstring>
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

// ── TestBase::bounded_string — IMPROVEMENT_PLAN.md A3 ──────────────────────
//
// Driver-supplied lengths were being used directly as buffer lengths. The
// reported length is the *total available* bytes, not the bytes written, and
// SQL_SUCCEEDED accepts the 01004 warning that accompanies truncation — so
// `std::string(buf, reported)` read past the end of the caller's stack buffer.
// SQL_NO_TOTAL (-4) was worse: converted to size_t it is SIZE_MAX - 3.
//
// A pure function, so these are exhaustive rather than illustrative.

TEST(BoundedStringTest, ExactLengthIsReturnedVerbatim) {
    char buf[16] = "hello";
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), 5);
    EXPECT_EQ(r.value, "hello");
    EXPECT_FALSE(r.truncated);
    EXPECT_FALSE(r.length_unknown);
}

TEST(BoundedStringTest, EmptyValueIsNotAnError) {
    char buf[16] = "";
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), 0);
    EXPECT_EQ(r.value, "");
    EXPECT_FALSE(r.truncated);
    EXPECT_FALSE(r.length_unknown);
}

// The core case: the driver says "there were 100 bytes" while handing back a
// 16-byte buffer. Taking it at its word is an out-of-bounds read.
TEST(BoundedStringTest, ReportedLengthPastTheBufferIsClampedAndFlagged) {
    char buf[16];
    std::memset(buf, 'A', sizeof(buf));   // deliberately not terminated
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), 100);
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.value.size(), sizeof(buf) - 1)
        << "must stop one short of the buffer, leaving room for a terminator";
    EXPECT_EQ(r.value, std::string(sizeof(buf) - 1, 'A'));
}

// SQL_NO_TOTAL is -4. As a size_t that is SIZE_MAX - 3.
TEST(BoundedStringTest, SqlNoTotalDoesNotBecomeAHugeLength) {
    char buf[16] = "partial";
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), SQL_NO_TOTAL);
    EXPECT_EQ(r.value, "partial");
    EXPECT_TRUE(r.length_unknown);
    EXPECT_TRUE(r.truncated) << "if the driver cannot say how much there is, "
                                "assume there is more";
}

TEST(BoundedStringTest, SqlNullDataYieldsNothingRatherThanAnInventedString) {
    char buf[16] = "leftover";
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), SQL_NULL_DATA);
    EXPECT_EQ(r.value, "");
    EXPECT_TRUE(r.length_unknown);
}

// An unterminated buffer with a reported length that fits must still not read
// past what the driver can have written.
TEST(BoundedStringTest, UnterminatedBufferIsBoundedByTheBuffer) {
    char buf[8];
    std::memset(buf, 'Z', sizeof(buf));
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), 7);
    EXPECT_EQ(r.value, "ZZZZZZZ");
    EXPECT_FALSE(r.truncated);
}

// A driver that over-reports within the buffer must not make us return
// uninitialised bytes past the terminator it did write.
TEST(BoundedStringTest, ReportedLengthPastTheTerminatorIsClampedToIt) {
    char buf[16] = {0};
    std::memcpy(buf, "abc", 3);
    auto r = tests::TestBase::bounded_string(buf, sizeof(buf), 10);
    EXPECT_EQ(r.value, "abc");
}

TEST(BoundedStringTest, NullBufferAndZeroCapacityAreHandled) {
    auto a = tests::TestBase::bounded_string(nullptr, 16, 5);
    EXPECT_EQ(a.value, "");
    EXPECT_TRUE(a.length_unknown);

    char buf[4] = "abc";
    auto b = tests::TestBase::bounded_string(buf, 0, 3);
    EXPECT_EQ(b.value, "");
    EXPECT_TRUE(b.length_unknown);
}

// ── DialectAttempt::format_failures — IMPROVEMENT_PLAN.md C2 ───────────────
//
// The point of the helper is that a probe which finds no working dialect can
// say what the driver objected to. Prior plan item 2.8 was closed as
// "implicitly addressed"; it was not, and this is the part that closes it.

TEST(DialectAttemptTest, NoFailuresFormatsEmpty) {
    tests::DialectAttempt a;
    a.executed = true;
    a.query = "SELECT 1";
    EXPECT_EQ(a.format_failures(), "");
}

TEST(DialectAttemptTest, EachFailedVariantIsNamedWithItsSqlstate) {
    tests::DialectAttempt a;
    a.failures.push_back({"SELECT 1 FROM RDB$DATABASE", "42S02", "Table unknown"});
    a.failures.push_back({"SELECT 1 FROM dual", "42000", "Syntax error"});

    const auto text = a.format_failures();
    EXPECT_NE(text.find("RDB$DATABASE"), std::string::npos) << text;
    EXPECT_NE(text.find("42S02"), std::string::npos) << text;
    EXPECT_NE(text.find("Table unknown"), std::string::npos) << text;
    EXPECT_NE(text.find("dual"), std::string::npos) << text;
    EXPECT_NE(text.find("42000"), std::string::npos) << text;
    // One line per variant.
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 1);
}

// A driver that fails without posting a diagnostic must still be reported —
// "(no SQLSTATE)" is information, an empty string is not.
TEST(DialectAttemptTest, MissingSqlstateIsSaidExplicitly) {
    tests::DialectAttempt a;
    a.failures.push_back({"SELECT 1", "", ""});
    const auto text = a.format_failures();
    EXPECT_NE(text.find("SELECT 1"), std::string::npos) << text;
    EXPECT_NE(text.find("no SQLSTATE"), std::string::npos) << text;
}

TEST(DialectAttemptTest, BoolConversionReflectsExecution) {
    tests::DialectAttempt a;
    EXPECT_FALSE(static_cast<bool>(a));
    a.executed = true;
    EXPECT_TRUE(static_cast<bool>(a));
}

// first_sqlstate on a handle with no diagnostics returns the caller's
// fallback, so a probe can distinguish "driver said nothing" from a state.
TEST_F(RunTestFixture, FirstSqlstateFallsBackWhenNoDiagnosticPosted) {
    EXPECT_EQ(tests::TestBase::first_sqlstate(SQL_HANDLE_DBC, conn_->get_handle(),
                                              "none"),
              "none");
    EXPECT_EQ(tests::TestBase::first_sqlstate(SQL_HANDLE_DBC, conn_->get_handle()),
              "");
}

// ── literal_select_variants — IMPROVEMENT_PLAN.md A2 ───────────────────────
//
// 58 bare `SELECT <expr>` statements across four probe files had no Firebird
// variant, which made the whole 20-probe Escape Sequence category, all of
// Numeric Struct and all of Cursor Stress unusable against the driver family
// this repository sits inside. Rather than duplicate 58 string literals, the
// variants are built from the bare form — so this is the one place the rule
// lives, and the one place a regression could happen.

TEST(LiteralSelectVariantsTest, BareFormComesFirst) {
    const auto v = tests::TestBase::literal_select_variants("SELECT 42");
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(v.front(), "SELECT 42")
        << "the bare form is what most engines want; trying it first keeps the "
           "common case to a single round trip";
}

TEST(LiteralSelectVariantsTest, CoversFirebirdAndOracle) {
    const auto v = tests::TestBase::literal_select_variants("SELECT 42");
    EXPECT_NE(std::find(v.begin(), v.end(), "SELECT 42 FROM RDB$DATABASE"),
              v.end())
        << "Firebird requires a FROM clause — this is the whole point of A2";
    EXPECT_NE(std::find(v.begin(), v.end(), "SELECT 42 FROM DUAL"), v.end())
        << "Oracle requires one too, and MySQL accepts DUAL";
}

// The escape-sequence probes pass whole `{fn ...}` expressions through here,
// so the suffix must be appended rather than the statement rebuilt.
TEST(LiteralSelectVariantsTest, PreservesTheExpressionVerbatim) {
    const auto v = tests::TestBase::literal_select_variants("SELECT {fn UCASE('a')}");
    for (const auto& q : v) {
        EXPECT_EQ(q.compare(0, 22, "SELECT {fn UCASE('a')}"), 0)
            << "variant mangled the expression: " << q;
    }
}

TEST(LiteralSelectVariantsTest, EveryVariantIsDistinct) {
    auto v = tests::TestBase::literal_select_variants("SELECT 1");
    const auto before = v.size();
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    EXPECT_EQ(v.size(), before) << "a duplicated variant is a wasted round trip";
}

// ── classify_failure — IMPROVEMENT_PLAN.md B3 ──────────────────────────────
//
// A handle with no diagnostics classifies as FAIL with no state: "the driver
// did not say why" is a failure, not a licence to skip. The states that do
// mean "not implemented" are covered end to end by the e2e scenarios, which
// need a real driver to post them.
TEST_F(RunTestFixture, ClassifyFailureDefaultsToFailWhenNothingWasPosted) {
    const auto c = tests::TestBase::classify_failure(SQL_HANDLE_DBC,
                                                     conn_->get_handle());
    EXPECT_EQ(c.status, tests::TestStatus::FAIL)
        << "silence is not evidence that a feature is unimplemented";
    EXPECT_TRUE(c.sqlstate.empty());
}

TEST_F(RunTestFixture, ReportFailureAlwaysRecordsAState) {
    tests::TestResult r;
    r.severity = tests::Severity::INFO;
    tests::TestBase::report_failure(r, SQL_HANDLE_DBC, conn_->get_handle(),
                                    "SQLSomething");

    EXPECT_EQ(r.status, tests::TestStatus::FAIL);
    EXPECT_NE(r.actual.find("SQLSomething"), std::string::npos) << r.actual;
    EXPECT_NE(r.actual.find("no SQLSTATE"), std::string::npos) << r.actual;
    ASSERT_TRUE(r.diagnostic.has_value());
    EXPECT_FALSE(r.diagnostic->empty());
    // A Core failure must not stay at INFO severity, or it sorts below
    // warning noise in the severity-ranked summary.
    EXPECT_EQ(r.severity, tests::Severity::ERR);
}
