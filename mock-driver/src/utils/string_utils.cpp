#include "string_utils.hpp"
#include "../driver/common.hpp"
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mock_odbc {

// ============================================================
// SQLCHAR (ANSI / UTF-8) helpers
// ============================================================

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

std::string sql_to_string(const SQLCHAR* sql_str, SQLSMALLINT length) {
    if (!sql_str) return "";
    
    if (length == SQL_NTS) {
        return std::string(reinterpret_cast<const char*>(sql_str));
    } else if (length > 0) {
        return std::string(reinterpret_cast<const char*>(sql_str), length);
    }
    
    return "";
}

// ============================================================
// SQLWCHAR (UTF-16) helpers
// ============================================================

// Internal: count SQLWCHAR units in a null-terminated string
static SQLINTEGER wcslen_sqlw(const SQLWCHAR* s) {
    if (!s) return 0;
    SQLINTEGER n = 0;
    while (s[n]) ++n;
    return n;
}

// Internal: UTF-16 → UTF-8 conversion
// Returns UTF-8 std::string.  Handles BMP-only codepoints (the vast
// majority of ODBC identifiers) plus surrogate pairs for full Unicode.
static std::string utf16_to_utf8(const SQLWCHAR* src, SQLINTEGER char_count) {
    if (!src || char_count == 0) return "";

#ifdef _WIN32
    // Use Win32 API for correct conversion
    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     reinterpret_cast<const wchar_t*>(src),
                                     char_count, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string result(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        reinterpret_cast<const wchar_t*>(src),
                        char_count, &result[0], needed, nullptr, nullptr);
    return result;
#else
    // Portable implementation for Linux/macOS
    // SQLWCHAR is typically 2 bytes (UTF-16) on all platforms for ODBC
    std::string result;
    result.reserve(char_count * 3); // worst case
    for (SQLINTEGER i = 0; i < char_count; ++i) {
        uint32_t cp = static_cast<uint16_t>(src[i]);
        // Handle surrogate pairs
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < char_count) {
            uint32_t lo = static_cast<uint16_t>(src[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
#endif
}

// Internal: UTF-8 → UTF-16 conversion
// Returns number of SQLWCHAR characters written (excluding null terminator).
// If target is null, just returns the required character count.
static SQLINTEGER utf8_to_utf16(const std::string& src,
                                SQLWCHAR* target,
                                SQLINTEGER max_chars) {
#ifdef _WIN32
    if (src.empty()) {
        if (target && max_chars > 0) target[0] = 0;
        return 0;
    }
    // First get the required size
    int needed = MultiByteToWideChar(CP_UTF8, 0,
                                     src.c_str(), static_cast<int>(src.length()),
                                     nullptr, 0);
    if (!target || max_chars <= 0) {
        return static_cast<SQLINTEGER>(needed);
    }
    // Convert into buffer (leave room for null terminator)
    int written = MultiByteToWideChar(CP_UTF8, 0,
                                      src.c_str(), static_cast<int>(src.length()),
                                      reinterpret_cast<wchar_t*>(target),
                                      max_chars);
    if (written < max_chars) {
        target[written] = 0;
    } else {
        target[max_chars - 1] = 0;
        written = max_chars - 1;
    }
    return static_cast<SQLINTEGER>(needed); // total chars needed (may > written)
#else
    // Portable implementation
    if (src.empty()) {
        if (target && max_chars > 0) target[0] = 0;
        return 0;
    }
    SQLINTEGER out_idx = 0;
    SQLINTEGER total_chars = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(src.c_str());
    const unsigned char* end = p + src.length();
    while (p < end) {
        uint32_t cp;
        if (*p < 0x80) {
            cp = *p++;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = (*p++ & 0x1F) << 6;
            if (p < end) cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF0) == 0xE0) {
            cp = (*p++ & 0x0F) << 12;
            if (p < end) cp |= (*p++ & 0x3F) << 6;
            if (p < end) cp |= (*p++ & 0x3F);
        } else if ((*p & 0xF8) == 0xF0) {
            cp = (*p++ & 0x07) << 18;
            if (p < end) cp |= (*p++ & 0x3F) << 12;
            if (p < end) cp |= (*p++ & 0x3F) << 6;
            if (p < end) cp |= (*p++ & 0x3F);
        } else {
            ++p; // skip invalid byte
            continue;
        }
        if (cp < 0x10000) {
            ++total_chars;
            if (target && out_idx < max_chars - 1) {
                target[out_idx++] = static_cast<SQLWCHAR>(cp);
            }
        } else {
            // surrogate pair
            total_chars += 2;
            cp -= 0x10000;
            if (target && out_idx < max_chars - 2) {
                target[out_idx++] = static_cast<SQLWCHAR>(0xD800 + (cp >> 10));
                target[out_idx++] = static_cast<SQLWCHAR>(0xDC00 + (cp & 0x3FF));
            }
        }
    }
    if (target && max_chars > 0) {
        if (out_idx < max_chars)
            target[out_idx] = 0;
        else
            target[max_chars - 1] = 0;
    }
    return total_chars;
#endif
}

// --- Public API ---

SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLSMALLINT buffer_length,
    SQLSMALLINT* string_length) {
    return copy_string_to_wbuffer(src, target,
                                  static_cast<SQLINTEGER>(buffer_length),
                                  string_length);
}

SQLRETURN copy_string_to_wbuffer(
    const std::string& src,
    SQLWCHAR* target,
    SQLINTEGER buffer_length,
    SQLSMALLINT* string_length) {
    
    // How many SQLWCHAR characters does src need?
    SQLINTEGER total_chars = utf8_to_utf16(src, nullptr, 0);
    SQLINTEGER total_bytes = total_chars * static_cast<SQLINTEGER>(sizeof(SQLWCHAR));
    
    if (string_length) {
        *string_length = static_cast<SQLSMALLINT>(total_bytes);
    }
    
    if (!target || buffer_length <= 0) {
        return SQL_SUCCESS;
    }
    
    SQLINTEGER max_chars = buffer_length / static_cast<SQLINTEGER>(sizeof(SQLWCHAR));
    if (max_chars <= 0) {
        return SQL_SUCCESS;
    }
    
    utf8_to_utf16(src, target, max_chars);
    
    if (total_bytes >= buffer_length) {
        return SQL_SUCCESS_WITH_INFO;  // Truncation
    }
    
    return SQL_SUCCESS;
}

std::string sqlw_to_string(const SQLWCHAR* sql_str, SQLSMALLINT length) {
    return sqlw_to_string(sql_str, static_cast<SQLINTEGER>(length));
}

std::string sqlw_to_string(const SQLWCHAR* sql_str, SQLINTEGER length) {
    if (!sql_str) return "";
    
    SQLINTEGER char_count;
    if (length == SQL_NTS) {
        char_count = wcslen_sqlw(sql_str);
    } else if (length > 0) {
        // length is in bytes for W functions
        char_count = length / static_cast<SQLINTEGER>(sizeof(SQLWCHAR));
    } else {
        return "";
    }
    
    return utf16_to_utf8(sql_str, char_count);
}

} // namespace mock_odbc
