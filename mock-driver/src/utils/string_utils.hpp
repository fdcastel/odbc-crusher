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

// Copy UTF-8 string to SQLWCHAR buffer (UTF-16) with proper truncation handling
// buffer_length is in BYTES, string_length output is in BYTES
SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length);

// Overload taking SQLINTEGER for larger buffers (used by SQLGetInfo etc.)
SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLINTEGER buffer_length,
    SQLSMALLINT* string_length);

// Convert SQLCHAR* to std::string (UTF-8 passthrough)
std::string sql_to_string(const SQLCHAR* sql_str, SQLSMALLINT length);

// Convert SQLWCHAR* (UTF-16) to std::string (UTF-8)
std::string sqlw_to_string(const SQLWCHAR* sql_str, SQLSMALLINT length);

// Overload taking SQLINTEGER length
std::string sqlw_to_string(const SQLWCHAR* sql_str, SQLINTEGER length);

} // namespace mock_odbc
