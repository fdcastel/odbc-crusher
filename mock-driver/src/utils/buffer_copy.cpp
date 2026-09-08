#include "buffer_copy.hpp"

#include "../mock/behaviors.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace mock_odbc {

namespace {

// Length in bytes of the UTF-8 sequence starting with `lead`. Returns 1 for
// anything that is not a valid lead byte, so a malformed string advances
// rather than looping.
size_t utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

// UTF-16 units a codepoint needs: 2 for anything outside the BMP.
size_t utf16_units_for(size_t utf8_seq_len) {
    return utf8_seq_len == 4 ? 2u : 1u;
}

// Decode one UTF-8 sequence to a codepoint. `len` comes from
// utf8_sequence_length and is assumed to fit inside `s`.
uint32_t decode_utf8(const char* s, size_t len) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    switch (len) {
        case 2:  return ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        case 3:  return ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) |
                        (p[2] & 0x3Fu);
        case 4:  return ((p[0] & 0x07u) << 18) | ((p[1] & 0x3Fu) << 12) |
                        ((p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
        default: return p[0];
    }
}

// D62: BufferValidation=Lenient, applied where the terminator is written.
//
// D33 put this on SQLGetInfo alone and said so in its row: "broadening it
// would break unrelated paths". That scoping is what left D58/D60/D61/D63/D64
// enumerating buffers by hand - the fixture could only expose the sites
// SQLGetInfo reached, so every other unguarded buffer in the tool was a guess
// rather than a finding. Here the corruption sits in the one copy every
// string return passes through (D18), which reaches SQLGetData,
// SQLDescribeCol, SQLColAttribute, SQLGetCursorName, SQLNativeSql,
// SQLGetDiagRec and the descriptor string fields at once.
//
// The write stays inside the caller's buffer: it overwrites the terminator
// copy_chars/copy_wchars has just written, which sits at most one unit below
// the declared capacity. What the caller loses is the terminator, not the
// bounds - which is the real-driver defect being modelled. An application or
// a driver manager then scans for a NUL that is not there.
//
// A filler byte rather than random data, so a probe that reports what it read
// can be read back: 'X' is what D53 identified the injection by.
bool lenient_buffers() {
    return BehaviorController::instance().lenient_buffers();
}

} // namespace

BufferCopyResult copy_chars(const std::string& src,
                            size_t offset,
                            void* target,
                            SQLLEN buffer_bytes) {
    BufferCopyResult out;

    // An offset past the end is how a caller asks "is there more?" after
    // consuming everything; the answer is nothing left, not an error.
    const size_t start = std::min(offset, src.size());
    const size_t available = src.size() - start;
    out.remaining = static_cast<SQLLEN>(available);

    // A null pointer or a zero-length buffer is a legal way to ask only for
    // the length. Nothing is written, and the caller still learns the size.
    if (!target || buffer_bytes <= 0) {
        // Only *say* truncation when there was something to truncate. A
        // zero-length ask for an empty value is not a warning.
        out.truncated = available > 0;
        out.rc = out.truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
        return out;
    }

    // One byte of the buffer belongs to the terminator, which is written
    // whatever else happens.
    const size_t capacity = static_cast<size_t>(buffer_bytes) - 1;
    out.copied = std::min(available, capacity);

    char* dst = static_cast<char*>(target);
    if (out.copied > 0) std::memcpy(dst, src.data() + start, out.copied);
    dst[out.copied] = '\0';
    if (lenient_buffers()) dst[out.copied] = 'X';  // D62

    out.truncated = out.copied < available;
    out.rc = out.truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    return out;
}

BufferCopyResult copy_wchars(const std::string& src,
                             size_t offset,
                             void* target,
                             SQLLEN buffer_bytes) {
    BufferCopyResult out;

    const size_t start = std::min(offset, src.size());

    // Total UTF-16 bytes still available from `start`. Counted rather than
    // estimated, because StrLen_or_IndPtr has to be exact for a caller
    // allocating from it.
    size_t remaining_units = 0;
    for (size_t i = start; i < src.size();) {
        const size_t len = std::min(utf8_sequence_length(
                               static_cast<unsigned char>(src[i])),
                               src.size() - i);
        remaining_units += utf16_units_for(len);
        i += len;
    }
    out.remaining = static_cast<SQLLEN>(remaining_units * sizeof(SQLWCHAR));

    if (!target || buffer_bytes < static_cast<SQLLEN>(sizeof(SQLWCHAR))) {
        out.truncated = remaining_units > 0;
        out.rc = out.truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
        return out;
    }

    // One unit for the terminator.
    const size_t capacity_units =
        static_cast<size_t>(buffer_bytes) / sizeof(SQLWCHAR) - 1;

    SQLWCHAR* dst = static_cast<SQLWCHAR*>(target);
    size_t written = 0;
    size_t i = start;
    while (i < src.size()) {
        const size_t len = std::min(utf8_sequence_length(
                               static_cast<unsigned char>(src[i])),
                               src.size() - i);
        const size_t units = utf16_units_for(len);
        // Stop on a codepoint boundary: half a surrogate pair is not a
        // character, and a caller reassembling chunks would never recover it.
        if (written + units > capacity_units) break;

        const uint32_t cp = decode_utf8(src.data() + i, len);
        if (units == 2) {
            const uint32_t v = cp - 0x10000u;
            dst[written++] = static_cast<SQLWCHAR>(0xD800u + (v >> 10));
            dst[written++] = static_cast<SQLWCHAR>(0xDC00u + (v & 0x3FFu));
        } else {
            dst[written++] = static_cast<SQLWCHAR>(cp);
        }
        i += len;
    }
    dst[written] = 0;
    if (lenient_buffers()) dst[written] = static_cast<SQLWCHAR>('X');  // D62

    out.copied = written;
    out.truncated = written < remaining_units;
    out.rc = out.truncated ? SQL_SUCCESS_WITH_INFO : SQL_SUCCESS;
    return out;
}

} // namespace mock_odbc
