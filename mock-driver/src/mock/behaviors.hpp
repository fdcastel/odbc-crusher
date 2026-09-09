#pragma once

#include "../driver/config.hpp"
#include <atomic>
#include <mutex>

namespace mock_odbc {

// Additional behavior control
//
// Thread-safety: all public methods serialize on `mu_`. `config()` returns
// a copy rather than a reference so the caller's snapshot can't be torn by
// a concurrent `set_config` — existing `const auto& c = …config()` bindings
// stay correct because the temporary's lifetime is extended to the
// reference binding's scope.
class BehaviorController {
public:
    static BehaviorController& instance();

    void set_config(const DriverConfig& config);
    DriverConfig config() const;

    // D89: `bool should_fail(const std::string&) const` was here and had
    // zero callers. Every site asks the DriverConfig it already holds -
    // `BehaviorController::instance().config().should_fail(...)` - because
    // config() returns a copy under the mutex and the call site needs the
    // rest of that copy anyway (`error_code`, `apply_latency`). A second way
    // in that nobody used is a second way to get the locking wrong.
    void apply_latency() const;

    // D62: BufferValidation=Lenient, asked cheaply.
    //
    // copy_chars/copy_wchars consult this once per string handed back to the
    // caller - per column, per row, per SQLGetData chunk - so it cannot go
    // through config(), which locks the mutex and copies six std::strings.
    // Mirrored into an atomic by set_config instead; the only reader wants
    // one bit and a stale read cannot happen, because the connect that sets
    // the config happens-before any call that copies a string on it.
    bool lenient_buffers() const {
        return lenient_buffers_.load(std::memory_order_relaxed);
    }

private:
    BehaviorController() = default;

    mutable std::mutex mu_;
    DriverConfig config_;
    std::atomic<bool> lenient_buffers_{false};
};

// D34: numeric fault injection, for probes that assert a specific value.
//
// Which delivery path a value is travelling on, so SkewNumericBound can make
// the two disagree — a probe that reads the same column via a bound buffer and
// via SQLGetData should see identical values, and nothing could previously
// make a driver break that.
enum class FetchPath {
    BoundColumn,   // SQLFetch / SQLFetchScroll writing into a bound buffer
    GetData        // SQLGetData
};

// Apply the active SilentCorruption numeric skew, if any. Returns `value`
// unchanged for every mode that is not a Skew* mode, which is the default.
long long apply_numeric_skew(long long value, FetchPath path);
double apply_numeric_skew(double value, FetchPath path);

} // namespace mock_odbc
