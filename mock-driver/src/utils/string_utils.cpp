#include "string_utils.hpp"
#include "buffer_copy.hpp"
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

    // D18: one implementation of the copy, in copy_chars.
    //
    // D23: this used to compute `SQLSMALLINT src_len = src.length()`. At
    // 32768 bytes that wraps negative, `*string_length` reports a negative
    // length, and the truncation test `src_len >= buffer_length` then
    // answers *false* - so the one case the check exists for was the case it
    // got wrong. The arithmetic is SQLLEN inside copy_chars; only the
    // reported length is narrowed here, and only after being clamped.
    const BufferCopyResult res =
        copy_chars(src, /*offset=*/0, target, static_cast<SQLLEN>(buffer_length));

    if (string_length) {
        // The ODBC signature is SQLSMALLINT, so a length beyond its range
        // cannot be reported faithfully. Saturate rather than wrap, which at
        // least keeps "there is more than you asked for" true.
        constexpr SQLLEN kMaxSmallint = 32767;
        *string_length = static_cast<SQLSMALLINT>(
            res.remaining > kMaxSmallint ? kMaxSmallint : res.remaining);
    }
    return res.rc;
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

// D15/D18: utf8_to_utf16 is gone. It was the only place the driver behaved
// differently on Windows and POSIX - MultiByteToWideChar writes nothing and
// returns 0 when the source does not fit, so the Windows build handed back an
// empty string where the portable branch handed back the truncated prefix.
// copy_wchars in utils/buffer_copy.cpp is the one implementation now, and it
// also stops on a codepoint boundary rather than splitting a surrogate pair.

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

    // D15/D18: one implementation, in copy_wchars, and one behaviour on
    // every platform.
    //
    // The Windows branch of utf8_to_utf16 used MultiByteToWideChar, which
    // returns **0** with ERROR_INSUFFICIENT_BUFFER when the source does not
    // fit and writes nothing at all. So a truncating call handed the caller
    // an *empty* string alongside its 01004, while the POSIX branch of the
    // same function gave it the truncated prefix - the same driver behaving
    // differently on the two platforms, in exactly the area D1 is about.
    //
    // copy_wchars also stops on a codepoint boundary, so a chunk never ends
    // half way through a surrogate pair.
    const BufferCopyResult res =
        copy_wchars(src, /*offset=*/0, target, static_cast<SQLLEN>(buffer_length));

    if (string_length) {
        // The signature is SQLSMALLINT and the value is a *byte* count, so it
        // overflows at 16384 UTF-16 units. It used to wrap: a negative
        // StrLen_or_IndPtr is SQL_NULL_DATA to an application, which then
        // reads a perfectly good value as NULL. Saturate instead - "there is
        // more than you asked for" stays true, and NULL is never claimed.
        constexpr SQLLEN kMaxSmallint = 32767;
        *string_length = static_cast<SQLSMALLINT>(
            res.remaining > kMaxSmallint ? kMaxSmallint : res.remaining);
    }
    return res.rc;
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
        // length is in characters for W functions per ODBC spec
        char_count = length;
    } else {
        return "";
    }
    
    return utf16_to_utf8(sql_str, char_count);
}

} // namespace mock_odbc
