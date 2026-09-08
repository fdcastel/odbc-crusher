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

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>

namespace odbc_crusher::tests {

template <typename T>
class GuardedBuffer {
public:
    // Wide enough for a whole small string written past the end, not merely
    // the stray terminator that is the common case.
    static constexpr size_t kGuardElements = 16;

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
        for (size_t i = declared_; i < storage_.size(); ++i) {
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
        for (size_t i = declared_; i < storage_.size(); ++i) {
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

} // namespace odbc_crusher::tests
