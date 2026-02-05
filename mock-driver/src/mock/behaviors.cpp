#include "behaviors.hpp"

namespace mock_odbc {

BehaviorController& BehaviorController::instance() {
    static BehaviorController instance;
    return instance;
}

void BehaviorController::set_config(const DriverConfig& config) {
    config_ = config;
}

bool BehaviorController::should_fail(const std::string& function_name) const {
    return config_.should_fail(function_name);
}

void BehaviorController::apply_latency() const {
    config_.apply_latency();
}

} // namespace mock_odbc
