#pragma once

// Cross-platform helpers for SQLWCHAR string literals.
// On Windows, SQLWCHAR == wchar_t (UTF-16), so L"..." works directly.
// On Linux/macOS, SQLWCHAR == unsigned short, but wchar_t is 4 bytes (UTF-32).
// This header provides a portable way to create SQLWCHAR buffers from narrow strings.

// C14: `<windows.h>` has to precede `<sql.h>` on Windows - sqltypes.h uses
// Win32 typedefs it does not declare - and this header did not do it, so it
// could not be included first. Every existing caller happened to include
// something else beforehand that did; the first test to include it directly
// failed to compile. Self-sufficient now, like core/guarded_buffer.hpp.
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace odbc_crusher::tests {

// Convert a UTF-8 string to a NUL-terminated SQLWCHAR buffer — C14.
//
// This used to push each *byte* as one code unit, so "café" (63 61 66 C3 A9)
// came out as U+0063 U+0061 U+0066 U+00C3 U+00A9 — "cafÃ©". Mojibake,
// silently, from a helper with 44 call sites. The comment said "ASCII subset
// is fine for test strings" and nothing enforced it, while unicode_tests.cpp
// — the file whose entire subject is non-ASCII — includes this header.
//
// The width handling is the same point A10 had to fix in `make_wchar_buf`:
// SQLWCHAR is 2 bytes on Windows and 4 on most unixODBC builds, so a
// supplementary codepoint is a surrogate pair on one and a single unit on the
// other. Encoding for the wrong width is what made a correct driver look like
// it had mangled the data.
//
// Ill-formed input becomes U+FFFD rather than passing bytes through. A test
// helper that silently invents characters is how a probe ends up asserting
// against mojibake and reporting a conforming driver as broken.
inline std::vector<SQLWCHAR> to_sqlwchar(const char* str) {
    std::vector<SQLWCHAR> result;
    if (!str) return result;

    const auto* p = reinterpret_cast<const unsigned char*>(str);
    const auto emit = [&result](uint32_t cp) {
        if (sizeof(SQLWCHAR) >= 4 || cp <= 0xFFFFu) {
            result.push_back(static_cast<SQLWCHAR>(cp));
            return;
        }
        // 2-byte SQLWCHAR and a supplementary codepoint: surrogate pair.
        cp -= 0x10000u;
        result.push_back(static_cast<SQLWCHAR>(0xD800u + (cp >> 10)));
        result.push_back(static_cast<SQLWCHAR>(0xDC00u + (cp & 0x3FFu)));
    };

    while (*p) {
        const unsigned char b0 = *p;
        size_t len = 0;
        uint32_t cp = 0;
        if (b0 < 0x80u)            { len = 1; cp = b0; }
        else if ((b0 & 0xE0u) == 0xC0u) { len = 2; cp = b0 & 0x1Fu; }
        else if ((b0 & 0xF0u) == 0xE0u) { len = 3; cp = b0 & 0x0Fu; }
        else if ((b0 & 0xF8u) == 0xF0u) { len = 4; cp = b0 & 0x07u; }
        else { emit(0xFFFDu); ++p; continue; }   // continuation or 0xF8+

        bool ok = true;
        for (size_t i = 1; i < len; ++i) {
            if ((p[i] & 0xC0u) != 0x80u) { ok = false; break; }
            cp = (cp << 6) | (p[i] & 0x3Fu);
        }
        // Overlong forms, surrogate halves and out-of-range scalars parse and
        // are still ill-formed.
        static const uint32_t kSmallest[5] = {0, 0, 0x80u, 0x800u, 0x10000u};
        if (!ok || cp < kSmallest[len] || cp > 0x10FFFFu ||
            (cp >= 0xD800u && cp <= 0xDFFFu)) {
            emit(0xFFFDu);
            ++p;
            continue;
        }
        emit(cp);
        p += len;
    }

    result.push_back(0);  // null terminator
    return result;
}

// Convenience wrapper that returns pointer (valid as long as vector lives)
class SqlWcharBuf {
public:
    explicit SqlWcharBuf(const char* str) : data_(to_sqlwchar(str)) {}
    SQLWCHAR* ptr() { return data_.data(); }
    // C6: `byte_len()` stood here with no callers, returning the *data*
    // length in bytes - the buffer minus its terminator. C6 suggested
    // adopting it in param_binding_tests.cpp, where the buffer length is
    // computed by hand; that would have been a defect. The argument there is
    // SQLBindParameter's BufferLength, which the spec defines as the length
    // of the buffer, and the buffer does contain the terminator. A helper
    // that reads like a buffer length and is not is worse than no helper.
private:
    std::vector<SQLWCHAR> data_;
};

} // namespace odbc_crusher::tests
