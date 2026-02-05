#include "config.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <thread>
#include <random>

namespace mock_odbc {

namespace {

std::string to_lower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // anonymous namespace

bool DriverConfig::should_fail(const std::string& function_name) const {
    switch (mode) {
        case BehaviorMode::Success:
            return false;
            
        case BehaviorMode::Failure:
            return true;
            
        case BehaviorMode::Random: {
            static std::random_device rd;
            static std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1, 100);
            return dis(gen) <= failure_probability;
        }
        
        case BehaviorMode::Partial: {
            std::string lower_name = to_lower(function_name);
            for (const auto& f : fail_on) {
                if (to_lower(f) == lower_name) {
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

void DriverConfig::apply_latency() const {
    if (latency.count() > 0) {
        std::this_thread::sleep_for(latency);
    }
}

std::unordered_map<std::string, std::string> parse_connection_string_pairs(
    const std::string& conn_str) {
    std::unordered_map<std::string, std::string> result;
    
    std::string current;
    bool in_braces = false;
    
    for (char c : conn_str) {
        if (c == '{') {
            in_braces = true;
            current += c;
        } else if (c == '}') {
            in_braces = false;
            current += c;
        } else if (c == ';' && !in_braces) {
            // Parse key=value
            auto eq_pos = current.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = trim(current.substr(0, eq_pos));
                std::string value = trim(current.substr(eq_pos + 1));
                
                // Remove braces from value
                if (!value.empty() && value.front() == '{' && value.back() == '}') {
                    value = value.substr(1, value.length() - 2);
                }
                
                result[to_lower(key)] = value;
            }
            current.clear();
        } else {
            current += c;
        }
    }
    
    // Handle last pair (no trailing semicolon)
    if (!current.empty()) {
        auto eq_pos = current.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = trim(current.substr(0, eq_pos));
            std::string value = trim(current.substr(eq_pos + 1));
            
            if (!value.empty() && value.front() == '{' && value.back() == '}') {
                value = value.substr(1, value.length() - 2);
            }
            
            result[to_lower(key)] = value;
        }
    }
    
    return result;
}

std::string get_string_value(const std::unordered_map<std::string, std::string>& pairs,
                              const std::string& key,
                              const std::string& default_value) {
    auto it = pairs.find(to_lower(key));
    if (it != pairs.end()) {
        return it->second;
    }
    return default_value;
}

int get_int_value(const std::unordered_map<std::string, std::string>& pairs,
                   const std::string& key,
                   int default_value) {
    auto it = pairs.find(to_lower(key));
    if (it != pairs.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

DriverConfig parse_connection_string(const std::string& conn_str) {
    DriverConfig config;
    
    auto pairs = parse_connection_string_pairs(conn_str);
    
    // Mode
    std::string mode_str = to_lower(get_string_value(pairs, "mode", "success"));
    if (mode_str == "failure" || mode_str == "fail") {
        config.mode = BehaviorMode::Failure;
    } else if (mode_str == "random") {
        config.mode = BehaviorMode::Random;
    } else if (mode_str == "partial") {
        config.mode = BehaviorMode::Partial;
    } else {
        config.mode = BehaviorMode::Success;
    }
    
    // Catalog
    config.catalog = get_string_value(pairs, "catalog", "Default");
    
    // Types
    config.types = get_string_value(pairs, "types", "AllTypes");
    
    // Result set size
    config.result_set_size = get_int_value(pairs, "resultsetsize", 100);
    
    // FailOn - comma-separated list of functions
    std::string fail_on_str = get_string_value(pairs, "failon", "");
    if (!fail_on_str.empty()) {
        std::istringstream iss(fail_on_str);
        std::string func;
        while (std::getline(iss, func, ',')) {
            config.fail_on.push_back(trim(func));
        }
    }
    
    // Error code
    config.error_code = get_string_value(pairs, "errorcode", "42000");
    
    // Latency
    std::string latency_str = get_string_value(pairs, "latency", "0");
    if (!latency_str.empty()) {
        int value = 0;
        try {
            value = std::stoi(latency_str);
        } catch (...) {}
        
        if (latency_str.find("ms") != std::string::npos) {
            config.latency = std::chrono::milliseconds(value);
        } else if (latency_str.find("us") != std::string::npos) {
            config.latency = std::chrono::milliseconds(value / 1000);
        } else {
            config.latency = std::chrono::milliseconds(value);
        }
    }
    
    // Max connections
    config.max_connections = get_int_value(pairs, "maxconnections", 0);
    
    // Transaction mode
    config.transaction_mode = get_string_value(pairs, "transactionmode", "Autocommit");
    
    // Failure probability
    config.failure_probability = get_int_value(pairs, "failureprobability", 50);
    
    // Phase 10.1: Buffer validation mode
    std::string buffer_val_str = to_lower(get_string_value(pairs, "buffervalidation", "strict"));
    if (buffer_val_str == "lenient") {
        config.buffer_validation = DriverConfig::BufferValidationMode::Lenient;
    } else {
        config.buffer_validation = DriverConfig::BufferValidationMode::Strict;
    }
    
    // Phase 10.2: Error count
    config.error_count = get_int_value(pairs, "errorcount", 1);
    if (config.error_count < 1) config.error_count = 1;
    if (config.error_count > 10) config.error_count = 10;  // Max 10 errors
    
    // Phase 10.3: State checking mode
    std::string state_check_str = to_lower(get_string_value(pairs, "statechecking", "strict"));
    if (state_check_str == "lenient") {
        config.state_checking = DriverConfig::StateCheckingMode::Lenient;
    } else {
        config.state_checking = DriverConfig::StateCheckingMode::Strict;
    }
    
    return config;
}

} // namespace mock_odbc
