#pragma once

#include "../driver/common.hpp"
#include <string>

namespace mock_odbc {

// Copy string to SQLCHAR buffer with proper truncation handling
SQLRETURN copy_string_to_buffer(
    const std::string& src,
    SQLCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length);

// Copy string to SQLWCHAR buffer
SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length);

// Convert SQLCHAR* to std::string
std::string sql_to_string(const SQLCHAR* sql_str, SQLSMALLINT length);

// Convert SQLWCHAR* to std::string
std::string sql_to_string(const SQLWCHAR* sql_str, SQLSMALLINT length);

} // namespace mock_odbc
