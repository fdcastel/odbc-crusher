// Tearing categories down without letting the driver take the process —
// IMPROVEMENT_PLAN.md D76.
//
// The existing crash-guard tests fault inside a lambda the guard is handed
// directly. This is the case nobody had: the fault happens in a *destructor*,
// reached through `unique_ptr::reset()` inside the guard, which is where the
// three categories that hold a RoundTripTableGuard past run() do their
// DROP TABLE.
//
// Named CrashGuardTeardownTest on purpose: the sanitizer job excludes tests
// matching "CrashGuard", because siglongjmp out of a destructor leaves the
// object's memory unreleased by construction and LeakSanitizer is right to say
// so. The exclusion is the same one CrashGuardTest relies on.
#include <gtest/gtest.h>

#include "tests/category_teardown.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"

#include <csignal>
#include <memory>
#include <vector>

using namespace odbc_crusher;

namespace {

// A category whose destructor faults, and one that does not.
class StubCategory : public tests::TestBase {
public:
    StubCategory(core::OdbcConnection& c, std::string name, bool fault)
        : TestBase(c), name_(std::move(name)), fault_(fault) {}

    ~StubCategory() override {
        // The fault has to match what this platform's guard catches. POSIX
        // installs signal handlers, so a raised SIGTRAP is exact and
        // deterministic - and it is the signal D73 had to add. Windows uses
        // SEH, where a raised signal is not a structured exception and would
        // sail past, so there it is an access violation, the same fault
        // CrashGuardTest.CatchesAccessViolation uses.
        if (fault_) {
#ifdef _WIN32
            volatile int* ptr = nullptr;
            *ptr = 42;  // BOOM
#else
            std::raise(SIGTRAP);
#endif
        }
        if (destroyed_) *destroyed_ = true;
    }

    std::vector<tests::TestResult> run() override { return {}; }
    std::string category_name() const override { return name_; }

    bool* destroyed_ = nullptr;

private:
    std::string name_;
    bool fault_;
};

class CrashGuardTeardownTest : public ::testing::Test {
protected:
    void SetUp() override {
        try {
            env = std::make_unique<core::OdbcEnvironment>();
            conn = std::make_unique<core::OdbcConnection>(*env);
            conn->connect("Driver={Mock ODBC Driver};Mode=Success;");
        } catch (...) {
            conn.reset();
        }
    }

    std::unique_ptr<core::OdbcEnvironment> env;
    std::unique_ptr<core::OdbcConnection> conn;
};

} // namespace

TEST_F(CrashGuardTeardownTest, AFaultingDestructorIsReportedAndSurvived) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";

    std::vector<std::unique_ptr<tests::TestBase>> categories;
    categories.push_back(
        std::make_unique<StubCategory>(*conn, "Exploding Tests", true));

    auto crashes = tests::teardown_categories(categories);

    ASSERT_EQ(crashes.size(), 1u)
        << "a destructor that faults must produce a result, not a dead process";
    EXPECT_EQ(crashes[0].test_name, "Exploding Tests (DRIVER CRASH IN TEARDOWN)")
        << "the name has to be taken before the object goes; \"a category "
           "crashed\" without saying which is what D63 spent a round fixing";
    EXPECT_EQ(crashes[0].status, tests::TestStatus::ERR);
    EXPECT_EQ(crashes[0].severity, tests::Severity::CRITICAL);
    EXPECT_FALSE(crashes[0].actual.empty())
        << "the signal belongs in the report";
}

// The point of the row: one category faulting must not stop the others being
// torn down. Before D76 the whole vector was destroyed in one go at end of
// scope, so the first fault took the process and everything after it.
TEST_F(CrashGuardTeardownTest, OneFaultDoesNotStopTheRest) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";

    bool third_destroyed = false;
    std::vector<std::unique_ptr<tests::TestBase>> categories;
    categories.push_back(
        std::make_unique<StubCategory>(*conn, "First Tests", false));
    categories.push_back(
        std::make_unique<StubCategory>(*conn, "Exploding Tests", true));
    auto third = std::make_unique<StubCategory>(*conn, "Third Tests", false);
    third->destroyed_ = &third_destroyed;
    categories.push_back(std::move(third));

    auto crashes = tests::teardown_categories(categories);

    EXPECT_EQ(crashes.size(), 1u) << "only the faulting one should report";
    EXPECT_TRUE(third_destroyed)
        << "the category after the faulting one was never destroyed, so its "
           "tables would have been left behind";
}

TEST_F(CrashGuardTeardownTest, ACleanTeardownReportsNothing) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";

    std::vector<std::unique_ptr<tests::TestBase>> categories;
    for (int i = 0; i < 3; ++i) {
        categories.push_back(std::make_unique<StubCategory>(
            *conn, "Quiet Tests " + std::to_string(i), false));
    }

    EXPECT_TRUE(tests::teardown_categories(categories).empty());
    for (const auto& c : categories) {
        EXPECT_EQ(c, nullptr) << "every category must be released, fault or not";
    }
}
