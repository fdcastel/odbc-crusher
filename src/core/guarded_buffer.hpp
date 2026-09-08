#pragma once

// A buffer with a sentinel margin past the length the driver is told about —
// IMPROVEMENT_PLAN.md D58.
//
// Handing an ODBC call a heap buffer allocated to exactly the length you
// declare means that a driver or driver manager which writes one byte too
// many corrupts the heap, and one that reads one byte too many reads memory
// that is not ours. Both happen, and this tool exists to find drivers that do
// them — so the one outcome it cannot afford is to crash while looking.
//
// ASan caught the read on Linux: `test_truncation_indicators` allocated a
// 3-byte `std::vector<char>`, and unixODBC's SQLGetInfo called `strlen()` on
// it, reading byte 4 of a 3-byte region. macOS's driver manager does the
// write: `test_buffer_overflow_protection`, which had hand-rolled this guard
// with a stack array, caught it writing a NUL at offset 10 of a 10-byte
// buffer.
//
// The declared length passed to ODBC is unchanged. Only the allocation grows,
// so an overrun lands in a sentinel we own and becomes a finding instead of
// undefined behaviour.

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>   // SQL_NO_TOTAL

namespace odbc_crusher::core {

template <typename T>
class GuardedBuffer {
public:
    // Wide enough for a whole small string written past the end, not merely
    // the stray terminator that is the common case.
    static constexpr size_t kGuardElements = 16;

    // D60: the last guard element is a zero, and it is not part of the
    // sentinel run. A guard of pure sentinel catches a driver that writes too
    // far and does nothing about one that reads too far - and something
    // always reads too far, because a driver which omits its terminator
    // leaves every strlen() downstream scanning until it finds a zero byte
    // somewhere. ASan caught that twice: unixODBC's strlen over a 3-byte
    // buffer, and again over the 1024-byte one in DriverInfo when
    // BufferValidation=Lenient dropped the NUL. A stopper bounds the scan
    // inside memory this object owns; the 15 sentinel elements in front of it
    // still catch the write.
    static constexpr size_t kSentinelElements = kGuardElements - 1;

    // `fill` goes in the declared region, `sentinel` in the guard. They must
    // differ from what the driver is expected to write, or a breach is
    // invisible; the defaults ('X' region, 'Z' guard) match what the buffer
    // probes already used.
    explicit GuardedBuffer(size_t declared_elements,
                           T fill = static_cast<T>('X'),
                           T sentinel = static_cast<T>('Z'))
        : declared_(declared_elements),
          sentinel_(sentinel),
          storage_(declared_elements + kGuardElements, sentinel) {
        for (size_t i = 0; i < declared_; ++i) storage_[i] = fill;
        storage_.back() = static_cast<T>(0);          // D60: the stopper
    }

    // Reset the declared region without touching the guard - D60. A caller
    // reusing the buffer row by row would otherwise memset over the sentinel
    // and the stopper, and the next row would have neither.
    void clear_declared(T fill = static_cast<T>(0)) {
        for (size_t i = 0; i < declared_; ++i) storage_[i] = fill;
    }

    T* data() { return storage_.data(); }
    const T* data() const { return storage_.data(); }

    size_t declared_elements() const { return declared_; }

    // What to pass as the ODBC BufferLength argument. ODBC counts bytes for
    // every buffer-length parameter, including the wide ones.
    SQLSMALLINT declared_bytes() const {
        return static_cast<SQLSMALLINT>(declared_ * sizeof(T));
    }

    // Offset, in elements from the start of the declared region, of the first
    // guard element the driver disturbed; nullopt when the guard is intact.
    std::optional<size_t> guard_breach() const {
        // The stopper at the back is ours, so it is not evidence of anything.
        for (size_t i = declared_; i < declared_ + kSentinelElements; ++i) {
            if (storage_[i] != sentinel_) return i;
        }
        return std::nullopt;
    }

    // The guard region as hex, for a failure message. A breach is worth
    // reporting byte by byte: a lone 0x00 is a terminator written one place
    // late, and a run of text is a driver ignoring the length entirely.
    std::string guard_hex() const {
        static const char kDigits[] = "0123456789ABCDEF";
        std::string out;
        for (size_t i = declared_; i < declared_ + kSentinelElements; ++i) {
            if (!out.empty()) out += ' ';
            const auto value = static_cast<unsigned long long>(storage_[i]);
            out += "0x";
            // One byte per element for char buffers, two for SQLWCHAR.
            for (int shift = static_cast<int>(sizeof(T)) * 8 - 4; shift >= 0;
                 shift -= 4) {
                out += kDigits[(value >> shift) & 0xF];
            }
        }
        return out;
    }

private:
    size_t declared_;
    T sentinel_;
    std::vector<T> storage_;
};

// What a driver-filled buffer actually contained — A3, moved here by D60.
//
// This lived on TestBase, where only the probes could reach it. The discovery
// layer needed exactly the same thing and could not have it without depending
// on the tests library, so it grew its own `reinterpret_cast<char*>(buf)`
// instead — an unbounded read against any driver that omits its terminator.
// One implementation now, in the layer both can see.
struct BoundedString {
    std::string value;
    bool truncated = false;        // driver had more than the buffer could hold
    bool length_unknown = false;   // driver returned SQL_NO_TOTAL, or a negative
};

// Build a std::string from a buffer the driver filled, using the length the
// driver reported — safely. A3.
//
// The reported length is *total available* bytes, not bytes written, so on
// truncation it exceeds the buffer; `SQL_SUCCEEDED` accepts the 01004 warning
// that accompanies it, so callers reached `std::string(buf, len)` with a
// length past the end of their own stack buffer. `SQL_NO_TOTAL` (-4) is worse:
// converted to size_t it becomes SIZE_MAX - 3.
//
// In a tool whose stated philosophy is "never crash" — and where main.cpp's
// crash guard would have reported the resulting fault as a *driver* crash.
//
// `capacity` is the full buffer size (use sizeof); one byte is reserved for
// the terminator, matching what a driver is allowed to write.
inline BoundedString bounded_string(const char* buf, size_t capacity,
                                    SQLLEN reported) {
    BoundedString out;
    if (!buf || capacity == 0) {
        out.length_unknown = true;
        return out;
    }

    // A driver may use every byte but the last; the last is the terminator.
    const size_t max_chars = capacity - 1;

    // How much was actually written, bounded by the buffer. memchr rather than
    // strlen so an unterminated buffer is not undefined behaviour.
    const void* nul = std::memchr(buf, '\0', max_chars);
    const size_t written =
        nul ? static_cast<size_t>(static_cast<const char*>(nul) - buf) : max_chars;

    if (reported == SQL_NO_TOTAL) {
        // The driver cannot say how much there is. Take what is in the buffer.
        out.length_unknown = true;
        out.truncated = true;
        out.value.assign(buf, written);
        return out;
    }

    if (reported < 0) {
        // SQL_NULL_DATA, or a driver returning nonsense. Either way there is
        // no length to trust; report nothing rather than invent a string.
        out.length_unknown = true;
        return out;
    }

    const size_t reported_len = static_cast<size_t>(reported);
    if (reported_len > max_chars) {
        // The classic case: `reported` is the total available, not the amount
        // written, and SQL_SUCCEEDED accepted the 01004 that came with it.
        out.truncated = true;
        out.value.assign(buf, written);
        return out;
    }

    // Trust the driver's length, but never past what it can have written.
    out.value.assign(buf, std::min(reported_len, written));
    return out;
}

} // namespace odbc_crusher::core
