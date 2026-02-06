#pragma once

// Cross-platform helpers for SQLWCHAR string literals.
// On Windows, SQLWCHAR == wchar_t (UTF-16), so L"..." works directly.
// On Linux/macOS, SQLWCHAR == unsigned short, but wchar_t is 4 bytes (UTF-32).
// This header provides a portable way to create SQLWCHAR buffers from narrow strings.

#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include <cstring>

namespace odbc_crusher::tests {

// Convert a narrow (ASCII/UTF-8) string to a vector of SQLWCHAR (UCS-2/UTF-16)
// Only handles BMP characters (ASCII subset is fine for test strings)
inline std::vector<SQLWCHAR> to_sqlwchar(const char* str) {
    std::vector<SQLWCHAR> result;
    if (!str) return result;
    while (*str) {
        result.push_back(static_cast<SQLWCHAR>(static_cast<unsigned char>(*str)));
        ++str;
    }
    result.push_back(0);  // null terminator
    return result;
}

// Convenience wrapper that returns pointer (valid as long as vector lives)
class SqlWcharBuf {
public:
    explicit SqlWcharBuf(const char* str) : data_(to_sqlwchar(str)) {}
    SQLWCHAR* ptr() { return data_.data(); }
    SQLLEN byte_len() const { return static_cast<SQLLEN>((data_.size() - 1) * sizeof(SQLWCHAR)); }
private:
    std::vector<SQLWCHAR> data_;
};

} // namespace odbc_crusher::tests
