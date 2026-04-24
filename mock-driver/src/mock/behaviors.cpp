#include "behaviors.hpp"

namespace mock_odbc {

BehaviorController& BehaviorController::instance() {
    static BehaviorController instance;
    return instance;
}

void BehaviorController::set_config(const DriverConfig& config) {
    std::lock_guard<std::mutex> g(mu_);
    config_ = config;
}

DriverConfig BehaviorController::config() const {
    std::lock_guard<std::mutex> g(mu_);
    return config_;
}

bool BehaviorController::should_fail(const std::string& function_name) const {
    std::lock_guard<std::mutex> g(mu_);
    return config_.should_fail(function_name);
}

void BehaviorController::apply_latency() const {
    DriverConfig snapshot;
    {
        std::lock_guard<std::mutex> g(mu_);
        snapshot = config_;
    }
    // apply_latency() can sleep; release the mutex before blocking so we
    // don't serialise every connection on the slowest one.
    snapshot.apply_latency();
}

} // namespace mock_odbc
