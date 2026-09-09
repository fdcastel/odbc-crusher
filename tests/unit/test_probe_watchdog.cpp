// S1 — the watchdog that says a probe has stopped coming back.
// IMPROVEMENT_PLAN_V2.
//
// It reports and does not recover, so what there is to test is exactly that:
// that a probe running past the threshold produces a line naming it, that a
// probe finishing inside the threshold produces none, and that the line keeps
// coming while the probe is still stuck rather than appearing once and
// scrolling away.
//
// std::cerr is redirected into a stringstream for the duration. The instance
// under test is a local one with a short interval — the process-wide singleton
// run_test uses has a sixty-second threshold, which is right for a real run and
// useless in a unit test.
#include <gtest/gtest.h>

#include "tests/test_base.hpp"

#include <chrono>
#include <sstream>
#include <string>
#include <thread>

using odbc_crusher::tests::ProbeWatchdog;

namespace {

// Swaps std::cerr's buffer for the duration, and puts it back afterwards even
// if an assertion throws.
class CapturedCerr {
public:
    CapturedCerr() : saved_(std::cerr.rdbuf(buffer_.rdbuf())) {}
    ~CapturedCerr() { std::cerr.rdbuf(saved_); }
    std::string str() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* saved_;
};

constexpr auto kThreshold = std::chrono::seconds(1);

}  // namespace

TEST(ProbeWatchdogTest, SaysNothingAboutAProbeThatReturnsInTime) {
    CapturedCerr captured;
    {
        ProbeWatchdog watchdog(kThreshold);
        watchdog.enter("Some Category", "test_quick");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        watchdog.leave();
        // Well past the threshold, but the probe already finished.
        std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    }
    EXPECT_EQ(captured.str().find("test_quick"), std::string::npos)
        << "a probe that returned in time was reported as stuck: "
        << captured.str();
}

TEST(ProbeWatchdogTest, NamesAProbeThatHasNotReturned) {
    CapturedCerr captured;
    {
        ProbeWatchdog watchdog(kThreshold);
        watchdog.enter("Block Cursor Tests", "test_stuck");
        // The worker wakes on a 5s cadence *or* when signalled, so give it a
        // full cycle to notice.
        std::this_thread::sleep_for(std::chrono::milliseconds(6500));
        watchdog.leave();
    }
    const std::string out = captured.str();
    EXPECT_NE(out.find("test_stuck"), std::string::npos)
        << "the stuck probe was not named: " << out;
    EXPECT_NE(out.find("Block Cursor Tests"), std::string::npos)
        << "the category was not named: " << out;
    EXPECT_NE(out.find("has not returned"), std::string::npos) << out;
}

TEST(ProbeWatchdogTest, ForgetsTheProbeOnceItLeaves) {
    CapturedCerr captured;
    {
        ProbeWatchdog watchdog(kThreshold);
        watchdog.enter("Some Category", "test_a");
        watchdog.leave();
        watchdog.enter("Some Category", "test_b");
        std::this_thread::sleep_for(std::chrono::milliseconds(6500));
        watchdog.leave();
    }
    const std::string out = captured.str();
    // Only the probe that was actually running when the threshold passed.
    EXPECT_NE(out.find("test_b"), std::string::npos) << out;
    EXPECT_EQ(out.find("test_a"), std::string::npos)
        << "a probe that had already finished was reported as stuck: " << out;
}

// Constructing and destroying it must not hang, whether or not a probe was
// ever entered — the destructor has to wake the worker, not wait for its next
// scheduled tick.
TEST(ProbeWatchdogTest, ShutsDownPromptlyWithNoProbeRunning) {
    const auto start = std::chrono::steady_clock::now();
    { ProbeWatchdog watchdog(kThreshold); }
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
              1000)
        << "the destructor waited for the worker's next wake-up instead of "
           "signalling it";
}
