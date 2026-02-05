#pragma once

#include "common.hpp"
#include <string>
#include <vector>

namespace mock_odbc {

// Diagnostic record structure
struct DiagnosticRecord {
    std::string sqlstate;       // 5-character SQLSTATE
    SQLINTEGER native_error;    // Native error code
    std::string message;        // Error message
    std::string class_origin;   // Class origin
    std::string subclass_origin; // Subclass origin
    std::string connection_name; // Connection name
    std::string server_name;    // Server name
    SQLINTEGER column_number = SQL_NO_COLUMN_NUMBER;
    SQLLEN row_number = SQL_NO_ROW_NUMBER;
};

// Common SQLSTATE codes
namespace sqlstate {
    constexpr const char* SUCCESS = "00000";
    constexpr const char* GENERAL_WARNING = "01000";
    constexpr const char* STRING_TRUNCATED = "01004";
    constexpr const char* INVALID_CURSOR_STATE = "24000";
    constexpr const char* INVALID_TRANSACTION_STATE = "25000";
    constexpr const char* INVALID_CURSOR_POSITION = "34000";
    constexpr const char* SYNTAX_ERROR = "42000";
    constexpr const char* TABLE_NOT_FOUND = "42S02";
    constexpr const char* COLUMN_NOT_FOUND = "42S22";
    constexpr const char* CONNECTION_NOT_OPEN = "08003";
    constexpr const char* CONNECTION_FAILURE = "08001";
    constexpr const char* INVALID_HANDLE = "HY000";
    constexpr const char* INVALID_HANDLE_TYPE = "HY092";
    constexpr const char* FUNCTION_SEQUENCE_ERROR = "HY010";
    constexpr const char* INVALID_STRING_OR_BUFFER_LENGTH = "HY090";
    constexpr const char* INVALID_ATTRIBUTE_VALUE = "HY024";
    constexpr const char* OPTIONAL_FEATURE_NOT_IMPLEMENTED = "HYC00";
    constexpr const char* TIMEOUT_EXPIRED = "HYT00";
    constexpr const char* GENERAL_ERROR = "HY000";
    constexpr const char* MEMORY_ALLOCATION_ERROR = "HY001";
    constexpr const char* INVALID_ARGUMENT_VALUE = "HY009";
    constexpr const char* INVALID_PARAMETER_NUMBER = "07009";
    constexpr const char* DATA_TYPE_ATTRIBUTE_VIOLATION = "07006";
    constexpr const char* INDICATOR_REQUIRED = "22002";
    constexpr const char* NUMERIC_VALUE_OUT_OF_RANGE = "22003";
    constexpr const char* STRING_DATA_TRUNCATED = "22001";
    constexpr const char* INTEGRITY_CONSTRAINT_VIOLATION = "23000";
    constexpr const char* NO_DATA = "02000";
}

// Create diagnostic from SQLSTATE
DiagnosticRecord make_diagnostic(const std::string& sqlstate, 
                                  SQLINTEGER native_error,
                                  const std::string& message);

} // namespace mock_odbc
