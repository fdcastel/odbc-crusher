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

} // namespace mock_odbc
