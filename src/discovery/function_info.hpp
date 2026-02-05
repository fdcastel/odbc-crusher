#pragma once

#include "core/odbc_connection.hpp"
#include <string>
#include <vector>
#include <array>

namespace odbc_crusher::discovery {

// ODBC function availability information
struct FunctionAvailability {
    SQLUSMALLINT function_id;
    std::string function_name;
    bool supported;
};

// Function information collected via SQLGetFunctions
class FunctionInfo {
public:
    explicit FunctionInfo(core::OdbcConnection& conn);
    
    // Collect all function information
    void collect();
    
    // Check if a specific function is supported
    bool is_supported(SQLUSMALLINT function_id) const;
    
    // Get all function availability
    const std::vector<FunctionAvailability>& functions() const { return functions_; }
    
    // Get supported count
    size_t supported_count() const;
    
    // Get unsupported count
    size_t unsupported_count() const;
    
    // Display summary
    std::string format_summary() const;
    
private:
    core::OdbcConnection& conn_;
    std::vector<FunctionAvailability> functions_;
    
    // Bitmap for all ODBC 3.x functions
    std::array<SQLUSMALLINT, SQL_API_ODBC3_ALL_FUNCTIONS_SIZE> function_bitmap_;
    
    // Helper to get function name
    static std::string get_function_name(SQLUSMALLINT function_id);
};

} // namespace odbc_crusher::discovery
