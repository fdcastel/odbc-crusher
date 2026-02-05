#pragma once

#include "../driver/config.hpp"

namespace mock_odbc {

// Additional behavior control
class BehaviorController {
public:
    static BehaviorController& instance();
    
    // Set current configuration
    void set_config(const DriverConfig& config);
    const DriverConfig& config() const { return config_; }
    
    // Check if we should fail
    bool should_fail(const std::string& function_name) const;
    
    // Apply configured latency
    void apply_latency() const;
    
private:
    BehaviorController() = default;
    DriverConfig config_;
};

} // namespace mock_odbc
