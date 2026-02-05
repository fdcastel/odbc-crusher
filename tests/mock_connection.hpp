// Mock ODBC Driver Connection Utilities
// Provides standardized connection strings for testing without real databases

#pragma once

#include <string>

namespace odbc_crusher::test {

/**
 * Get mock driver connection string with default configuration
 * Mode=Success, Catalog=Default, ResultSetSize=100
 */
inline const char* get_mock_connection() {
    return "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;";
}

/**
 * Get mock driver connection string for error testing
 * Mode=Partial, allows specifying which functions should fail
 */
inline std::string get_mock_connection_with_failure(const char* fail_on, const char* error_code = "42000") {
    return std::string("Driver={Mock ODBC Driver};Mode=Partial;FailOn=") + fail_on + 
           ";ErrorCode=" + error_code + ";Catalog=Default;";
}

/**
 * Get mock driver connection string with custom result set size
 */
inline std::string get_mock_connection_with_size(int result_set_size) {
    return std::string("Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=") + 
           std::to_string(result_set_size) + ";";
}

/**
 * Get mock driver connection string with empty catalog (no tables)
 */
inline const char* get_mock_connection_empty() {
    return "Driver={Mock ODBC Driver};Mode=Success;Catalog=Empty;";
}

/**
 * Get connection string - prefers mock driver, falls back to real database if needed
 * This is a TRANSITION function - eventually all tests should use mock driver only!
 * 
 * @param env_var Environment variable name (e.g., "FIREBIRD_ODBC_CONNECTION")
 * @param driver_name Friendly name for logging (e.g., "Firebird")
 * @return Connection string (mock or real)
 */
inline std::string get_connection_or_mock(const char* env_var, const char* driver_name) {
    // Try environment variable first (for backward compatibility during transition)
    const char* conn_str = std::getenv(env_var);
    if (conn_str && conn_str[0] != '\0') {
        return std::string(conn_str);
    }
    
    // Use mock driver as default
    return get_mock_connection();
}

} // namespace odbc_crusher::test
