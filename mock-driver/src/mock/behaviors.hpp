#pragma once

#include "../driver/config.hpp"
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

    bool should_fail(const std::string& function_name) const;
    void apply_latency() const;

private:
    BehaviorController() = default;

    mutable std::mutex mu_;
    DriverConfig config_;
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
