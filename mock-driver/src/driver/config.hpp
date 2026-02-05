#pragma once

#include "common.hpp"
#include <string>
#include <unordered_map>
#include <chrono>

namespace mock_odbc {

// Driver behavior mode
enum class BehaviorMode {
    Success,    // All operations succeed
    Failure,    // Operations fail
    Random,     // Random success/failure
    Partial     // Some operations fail based on FailOn
};

// Configuration from connection string
struct DriverConfig {
    // Behavior mode
    BehaviorMode mode = BehaviorMode::Success;
    
    // Catalog preset
    std::string catalog = "Default";
    
    // Data types
    std::string types = "AllTypes";
    
    // Result set size
    int result_set_size = 100;
    
    // Functions to fail on
    std::vector<std::string> fail_on;
    
    // SQLSTATE to return on failure
    std::string error_code = "42000";
    
    // Simulated latency
    std::chrono::milliseconds latency{0};
    
    // Max connections
    int max_connections = 0;  // 0 = unlimited
    
    // Transaction mode
    std::string transaction_mode = "Autocommit";
    
    // Isolation level
    int isolation_level = SQL_TXN_READ_COMMITTED;
    
    // Random failure probability (0-100)
    int failure_probability = 50;
    
    // Driver name for SQLGetInfo
    std::string driver_name = "Mock ODBC Driver";
    std::string driver_version = "01.00.0000";
    std::string driver_odbc_version = "03.80";
    std::string dbms_name = "MockDB";
    std::string dbms_version = "01.00.0000";
    
    // Phase 10.1: Buffer validation mode
    enum class BufferValidationMode {
        Strict,   // Strictly validate buffers (null termination, no overflow)
        Lenient   // Allow some buffer issues for testing app resilience
    };
    BufferValidationMode buffer_validation = BufferValidationMode::Strict;
    
    // Phase 10.2: Error queue management
    int error_count = 1;  // Number of diagnostic records to generate per error
    
    // Phase 10.3: State machine validation
    enum class StateCheckingMode {
        Strict,   // Strict state machine validation
        Lenient   // Lenient - allow some state violations
    };
    StateCheckingMode state_checking = StateCheckingMode::Strict;
    
    // Check if a function should fail
    bool should_fail(const std::string& function_name) const;
    
    // Apply latency if configured
    void apply_latency() const;
};

// Parse connection string into configuration
DriverConfig parse_connection_string(const std::string& conn_str);

// Parse key=value pairs from connection string
std::unordered_map<std::string, std::string> parse_connection_string_pairs(
    const std::string& conn_str);

// Get a string value from parsed pairs (case-insensitive)
std::string get_string_value(const std::unordered_map<std::string, std::string>& pairs,
                              const std::string& key, 
                              const std::string& default_value = "");

// Get an integer value from parsed pairs
int get_int_value(const std::unordered_map<std::string, std::string>& pairs,
                   const std::string& key, 
                   int default_value = 0);

} // namespace mock_odbc
