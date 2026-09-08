#include "behaviors.hpp"

namespace mock_odbc {

BehaviorController& BehaviorController::instance() {
    static BehaviorController instance;
    return instance;
}

void BehaviorController::set_config(const DriverConfig& config) {
    std::lock_guard<std::mutex> g(mu_);
    config_ = config;
    // D62: mirrored so copy_chars can ask without taking the mutex.
    lenient_buffers_.store(
        config.buffer_validation == DriverConfig::BufferValidationMode::Lenient,
        std::memory_order_relaxed);
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

namespace {

// True when the active mode should perturb a value delivered on `path`.
bool skew_applies(FetchPath path) {
    switch (BehaviorController::instance().config().silent_corruption) {
        case DriverConfig::SilentCorruptionMode::SkewNumeric:
            return true;
        case DriverConfig::SilentCorruptionMode::SkewNumericBound:
            return path == FetchPath::BoundColumn;
        default:
            return false;
    }
}

}  // namespace

long long apply_numeric_skew(long long value, FetchPath path) {
    return skew_applies(path) ? value + 1 : value;
}

double apply_numeric_skew(double value, FetchPath path) {
    return skew_applies(path) ? value + 1.0 : value;
}

} // namespace mock_odbc
