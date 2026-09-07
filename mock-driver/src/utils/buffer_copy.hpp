#pragma once

// One "copy into the caller's buffer with truncation" — D18.
//
// This block was written eight times across the driver, each copy re-deriving
// the same three decisions and getting a different subset of them right:
//
//   * how much may be written  — `buffer_bytes - 1`, reserving the terminator,
//     but only when `buffer_bytes > 0` and the pointer is non-null;
//   * what to report as the length — the bytes *still available*, which is not
//     the bytes written and is what an application uses to size its next
//     buffer;
//   * whether to say so — SQL_SUCCESS_WITH_INFO plus SQLSTATE 01004, which
//     several of the copies returned without ever posting the diagnostic
//     (D24), leaving an application unable to tell truncation from any other
//     warning.
//
// It also takes an `offset`, which is what makes chunked SQLGetData possible
// (D7): a caller retrieving a long value calls repeatedly, each call
// continuing where the last stopped.
//
// Lengths are SQLLEN throughout. The previous ANSI helper did its arithmetic
// in SQLSMALLINT, so a string of 32768 bytes or more wrapped negative and the
// truncation test went the wrong way (D23).

#include "../driver/common.hpp"

#include <string>

namespace mock_odbc {

struct BufferCopyResult {
    SQLRETURN rc = SQL_SUCCESS;   // SQL_SUCCESS or SQL_SUCCESS_WITH_INFO
    size_t copied = 0;            // units written, excluding the terminator
    SQLLEN remaining = 0;         // units available from `offset` onward,
                                  // excluding the terminator — this is what
                                  // belongs in StrLen_or_IndPtr
    bool truncated = false;       // caller should post 01004
};

// Copy `src` from byte `offset` into a character buffer of `buffer_bytes`
// bytes. Always terminates when it writes anything, never writes past the
// buffer, and tolerates a null target or a zero length (both are legal ways
// to ask only for the length).
BufferCopyResult copy_chars(const std::string& src,
                            size_t offset,
                            void* target,
                            SQLLEN buffer_bytes);

// The same for a UTF-16 target. `buffer_bytes` and the returned `remaining`
// are in **bytes**, matching the ODBC convention for SQL_C_WCHAR, while
// `offset` is in bytes into the UTF-8 `src`. Splitting a surrogate pair
// across two calls is avoided: a chunk ends on a codepoint boundary.
BufferCopyResult copy_wchars(const std::string& src,
                             size_t offset,
                             void* target,
                             SQLLEN buffer_bytes);

} // namespace mock_odbc
