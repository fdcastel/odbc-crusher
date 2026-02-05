#include "string_utils.hpp"
#include "../driver/common.hpp"
#include <cstring>
#include <algorithm>

namespace mock_odbc {

SQLRETURN copy_string_to_buffer(
    const std::string& src,
    SQLCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length) {
    
    SQLSMALLINT src_len = static_cast<SQLSMALLINT>(src.length());
    
    if (string_length) {
        *string_length = src_len;
    }
    
    if (!target || buffer_length <= 0) {
        return SQL_SUCCESS;
    }
    
    // Copy with truncation check
    SQLSMALLINT copy_len = std::min(src_len, static_cast<SQLSMALLINT>(buffer_length - 1));
    std::memcpy(target, src.c_str(), copy_len);
    target[copy_len] = '\0';
    
    if (src_len >= buffer_length) {
        return SQL_SUCCESS_WITH_INFO;  // Truncation
    }
    
    return SQL_SUCCESS;
}

SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length) {
    
    SQLSMALLINT src_len = static_cast<SQLSMALLINT>(src.length());
    
    if (string_length) {
        *string_length = src_len * sizeof(SQLWCHAR);
    }
    
    if (!target || buffer_length <= 0) {
        return SQL_SUCCESS;
    }
    
    // Convert and copy
    SQLSMALLINT max_chars = buffer_length / sizeof(SQLWCHAR) - 1;
    SQLSMALLINT copy_len = std::min(src_len, max_chars);
    
    for (SQLSMALLINT i = 0; i < copy_len; ++i) {
        target[i] = static_cast<SQLWCHAR>(src[i]);
    }
    target[copy_len] = 0;
    
    if (src_len > max_chars) {
        return SQL_SUCCESS_WITH_INFO;
    }
    
    return SQL_SUCCESS;
}

std::string sql_to_string(const SQLCHAR* sql_str, SQLSMALLINT length) {
    if (!sql_str) return "";
    
    if (length == SQL_NTS) {
        return std::string(reinterpret_cast<const char*>(sql_str));
    } else if (length > 0) {
        return std::string(reinterpret_cast<const char*>(sql_str), length);
    }
    
    return "";
}

std::string sql_to_string(const SQLWCHAR* sql_str, SQLSMALLINT length) {
    if (!sql_str) return "";
    
    std::string result;
    
    if (length == SQL_NTS) {
        const SQLWCHAR* p = sql_str;
        while (*p) {
            result += static_cast<char>(*p & 0xFF);
            ++p;
        }
    } else if (length > 0) {
        SQLSMALLINT char_count = length / sizeof(SQLWCHAR);
        for (SQLSMALLINT i = 0; i < char_count; ++i) {
            result += static_cast<char>(sql_str[i] & 0xFF);
        }
    }
    
    return result;
}

} // namespace mock_odbc
