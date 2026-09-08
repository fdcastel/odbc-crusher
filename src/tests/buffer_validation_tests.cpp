#include "buffer_validation_tests.hpp"
#include "core/odbc_error.hpp"
#include "core/guarded_buffer.hpp"
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

namespace {

// D54: a failing buffer probe has to say what it found, and the bytes are
// rarely printable - a sentinel run interrupted by a stray NUL is the usual
// shape. std::to_string on a char would render it as a number and hide that.
std::string hex_byte(char c) {
    static const char kDigits[] = "0123456789ABCDEF";
    const auto b = static_cast<unsigned char>(c);
    return std::string("0x") + kDigits[(b >> 4) & 0x0F] + kDigits[b & 0x0F];
}

std::string hex_bytes(const char* data, size_t count) {
    std::string out;
    for (size_t i = 0; i < count; ++i) {
        if (i) out += ' ';
        out += hex_byte(data[i]);
    }
    return out;
}

}  // namespace

BufferValidationTests::BufferValidationTests(core::OdbcConnection& connection)
    : TestBase(connection) {}

std::vector<TestResult> BufferValidationTests::run() {
    return {
        test_null_termination(),
        test_buffer_overflow_protection(),
        test_truncation_indicators(),
        test_undersized_buffer(),
        test_sentinel_values()
    };
}

TestResult BufferValidationTests::test_null_termination() {
    return run_test(
        "test_null_termination", "SQLGetInfo",
        "Null-terminated with correct length",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            // Test that SQLGetInfo returns null-terminated strings.
            //
            // D63: guarded, and D61 was wrong to exempt it. The argument for
            // leaving it bare was that memchr below reads only the declared
            // region, which is true and is not where the fault was: ASan named
            // the SQLGetInfo call on the next line, with libodbc in frame #1.
            // The driver manager runs strlen over this buffer while filling
            // it, before the probe looks at anything - so a probe cannot
            // detect a missing terminator by handing the manager a buffer with
            // no terminator in it and hoping the manager will not scan.
            //
            // Nothing about what this detects changes: the driver is still
            // told 256, and memchr still reads only those 256, so an
            // unterminated value is still nullptr and still FAILs below. The
            // guard adds a zero one place past what the driver may write,
            // which is where the manager's scan now stops.
            constexpr size_t kCapacity = 256;
            core::GuardedBuffer<char> buffer(kCapacity, 'X');
            SQLSMALLINT buffer_length = 0;

            SQLRETURN rc = SQLGetInfo(
                conn_.get_handle(),
                SQL_DRIVER_NAME,
                buffer.data(),
                static_cast<SQLSMALLINT>(kCapacity),
                &buffer_length
            );

            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "Failed to get driver name";
                try {
                    auto err = core::OdbcError::from_handle(SQL_HANDLE_DBC, conn_.get_handle(), "SQLGetInfo");
                    r.diagnostic = err.format_diagnostics();
                } catch(...) {}
                r.severity = Severity::ERR;
            } else {
                // A5. This used to call std::strlen() on a buffer memset
                // to 'X' with no NUL - undefined behaviour in exactly the
                // case the probe exists to detect, reading off the end of
                // the stack when the driver fails to terminate. And the
                // follow-up check, buffer[strlen(buffer)] != NUL, is a
                // tautology, so the FAIL branch was unreachable: the probe
                // could not detect the thing it is named for.
                //
                // memchr is bounded, so a missing terminator comes back as
                // nullptr rather than as a fault.
                const void* nul = std::memchr(buffer.data(), '\0', kCapacity);

                if (nul == nullptr) {
                    r.status = TestStatus::FAIL;
                    r.actual = "No NUL within " + std::to_string(kCapacity) +
                               " bytes; driver reported length " +
                               std::to_string(buffer_length);
                    r.suggestion = "Driver must null-terminate string outputs";
                    r.severity = Severity::ERR;
                } else {
                    const size_t actual_length = static_cast<size_t>(
                        static_cast<const char*>(nul) - buffer.data());

                    if (buffer_length != static_cast<SQLSMALLINT>(actual_length)) {
                        r.status = TestStatus::FAIL;
                        r.actual = std::to_string(buffer_length) +
                                   " bytes (expected " +
                                   std::to_string(actual_length) + ")";
                        r.suggestion =
                            "Buffer length indicator should match string length";
                        r.severity = Severity::WARNING;
                    } else {
                        r.status = TestStatus::PASS;
                        r.actual = "Null-terminated with correct length (" +
                                   std::to_string(actual_length) + " bytes)";
                    }
                }
            }
        });
}

TestResult BufferValidationTests::test_buffer_overflow_protection() {
    return run_test(
        "test_buffer_overflow_protection", "SQLGetInfo",
        "No overflow",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            // Use a very small buffer and verify driver doesn't overflow.
            //
            // D64: this probe hand-rolled the guard - five bytes of 'Z' past
            // what it declared - and GuardedBuffer is that idea with the one
            // piece it was missing, a zero at the end so a driver manager
            // scanning for a terminator stops inside our memory rather than
            // past it. Same declared length, same sentinel, more room.
            const SQLSMALLINT small_buffer_size = 10;
            core::GuardedBuffer<char> buffer(
                static_cast<size_t>(small_buffer_size), 'Z', 'Z');
            SQLSMALLINT buffer_length = 0;

            SQLRETURN rc = SQLGetInfo(
                conn_.get_handle(),
                SQL_DRIVER_NAME,
                buffer.data(),
                small_buffer_size,
                &buffer_length
            );

            // Should succeed or return SQL_SUCCESS_WITH_INFO (truncation)
            if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
                r.status = TestStatus::FAIL;
                r.actual = "Unexpected return code";
                r.severity = Severity::ERR;
            } else {
                // Verify the guard area after the buffer wasn't touched
                const auto breach = buffer.guard_breach();

                if (breach.has_value()) {
                    const size_t overflow_at = *breach;
                    r.status = TestStatus::FAIL;
                    // D54: this said only "guard area corrupted", which names
                    // neither the offset nor the byte. A CRITICAL finding on a
                    // platform you cannot attach a debugger to then tells you
                    // nothing you can act on - the position the macOS runner
                    // put us in. The offset, the byte and the length the
                    // driver declared are what separate the two candidate
                    // causes: a whole string written past the limit, or a
                    // terminator placed one byte late.
                    r.actual = "Wrote past the " +
                               std::to_string(small_buffer_size) +
                               "-byte buffer: offset " +
                               std::to_string(overflow_at) + " holds " +
                               hex_byte(buffer.data()[overflow_at]) +
                               ", guard area " + buffer.guard_hex() +
                               ", declared length " +
                               std::to_string(buffer_length);
                    r.suggestion = "Driver wrote beyond buffer boundary";
                    r.severity = Severity::CRITICAL;
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "No overflow detected";
                }
            }
        });
}

TestResult BufferValidationTests::test_truncation_indicators() {
    return run_test(
        "test_truncation_indicators", "SQLGetInfo",
        "SQL_SUCCESS_WITH_INFO with length > buffer",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, String Truncation",
        [&](TestResult& r) {
            // Step 1: Find a string info type with a long enough value to truncate.
            // IMPORTANT: Do NOT use SQL_DRIVER_NAME — on Windows, the Driver Manager
            // intercepts it and returns the DLL filename itself.  The DM's truncation
            // behaviour reports the *truncated* length in pcbInfoValue, not the full
            // string length, which causes a false test failure.
            // Use driver-handled info types that the DM always passes through.
            struct InfoProbe { SQLUSMALLINT type; const char* name; };
            InfoProbe probes[] = {
                { SQL_DBMS_NAME, "SQL_DBMS_NAME" },
                { SQL_DBMS_VER, "SQL_DBMS_VER" },
                { SQL_SERVER_NAME, "SQL_SERVER_NAME" },
            };

            // D64: guarded. D58 guarded Step 2's buffer and left this one,
            // declared thirty lines above its call and so invisible to the
            // proximity grep that found the others; ASan named it directly.
            constexpr size_t kFullCapacity = 256;
            core::GuardedBuffer<char> full_buf(kFullCapacity, '\0');
            SQLSMALLINT full_length = 0;
            SQLUSMALLINT info_type = 0;

            for (const auto& p : probes) {
                full_length = 0;
                SQLRETURN probe_rc = SQLGetInfo(
                    conn_.get_handle(), p.type, full_buf.data(),
                    static_cast<SQLSMALLINT>(kFullCapacity), &full_length);
                if (SQL_SUCCEEDED(probe_rc) && full_length >= 4) {
                    info_type = p.type;
                    break;
                }
            }

            if (info_type == 0 || full_length < 4) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not find a string info value long enough for truncation test";
                return;
            }

            // Step 2: Use a buffer that's definitely too small — half the actual length
            //
            // D58: guarded, because this exact call is where ASan caught
            // unixODBC calling strlen() on a 3-byte heap buffer and reading
            // byte 4. The length declared to ODBC is unchanged; only the
            // allocation is bigger, so an overrun lands in a sentinel this
            // probe owns.
            SQLSMALLINT small_buffer_size = std::max((SQLSMALLINT)2, (SQLSMALLINT)(full_length / 2));
            core::GuardedBuffer<char> small_buf(static_cast<size_t>(small_buffer_size));
            SQLSMALLINT buffer_length = 0;

            SQLRETURN rc = SQLGetInfo(
                conn_.get_handle(),
                info_type,
                small_buf.data(),
                small_buffer_size,
                &buffer_length
            );

            // A write past the declared length is a more serious finding than
            // whatever the truncation semantics turn out to be, so it is
            // reported first and on its own.
            if (auto breach = small_buf.guard_breach()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "Wrote past the " +
                           std::to_string(small_buffer_size) +
                           "-byte buffer while truncating: offset " +
                           std::to_string(*breach) + ", guard area " +
                           small_buf.guard_hex() + ", declared length " +
                           std::to_string(buffer_length);
                r.suggestion =
                    "A truncating driver must write no more than "
                    "BufferLength bytes, terminator included";
                return;
            }

            // Should return SQL_SUCCESS_WITH_INFO when truncated
            if (rc == SQL_SUCCESS_WITH_INFO) {
                // buffer_length should report the full length, not the truncated length
                if (buffer_length >= full_length) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_SUCCESS_WITH_INFO with full length = " + std::to_string(buffer_length) +
                                   " (buffer was " + std::to_string(small_buffer_size) + " bytes)";
                } else if (buffer_length >= small_buffer_size) {
                    // Some drivers/DMs report truncated length — note it but don't fail
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_SUCCESS_WITH_INFO with length = " + std::to_string(buffer_length) +
                                   " (full=" + std::to_string(full_length) +
                                   ", buffer=" + std::to_string(small_buffer_size) + ")";
                } else if (buffer_length == small_buffer_size - 1) {
                    // Driver/DM reported truncated string length (= buffer - NUL).
                    // This is a common DM behavior where the DM truncates the driver's
                    // output and overwrites pcbInfoValue with the truncated length.
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_SUCCESS_WITH_INFO with DM-truncated length = " + std::to_string(buffer_length) +
                                   " (full=" + std::to_string(full_length) +
                                   ", buffer=" + std::to_string(small_buffer_size) +
                                   "); DM reported truncated rather than full length";
                    r.suggestion = "Per ODBC spec, pcbInfoValue should report the full "
                                       "string length, but the Driver Manager may override it "
                                       "with the truncated length.";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Length (" + std::to_string(buffer_length) +
                                   ") < buffer size (" + std::to_string(small_buffer_size) +
                                   ") despite truncation";
                    r.suggestion = "After truncation, pcbInfoValue should report the full "
                                       "string length (excluding NUL), not the truncated length. "
                                       "Per ODBC 3.x spec §SQLGetInfo.";
                    r.severity = Severity::WARNING;
                }
            } else if (rc == SQL_SUCCESS) {
                // Data fit in the small buffer — shouldn't happen
                r.status = TestStatus::PASS;
                r.actual = "SQL_SUCCESS (data fit in " + std::to_string(small_buffer_size) + " byte buffer)";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Unexpected return code " + std::to_string(rc);
                r.severity = Severity::ERR;
            }
        });
}

TestResult BufferValidationTests::test_undersized_buffer() {
    return run_test(
        "test_undersized_buffer", "SQLGetInfo",
        "No crash with small buffers",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            // Test with various small buffer sizes
            bool all_passed = true;
            for (SQLSMALLINT size = 1; size <= 10; ++size) {
                // D58: guarded. A probe whose whole claim is "no crash with
                // small buffers" must not be the thing that crashes, and an
                // exactly-sized heap allocation made that a matter of luck.
                core::GuardedBuffer<char> buffer(static_cast<size_t>(size));
                SQLSMALLINT buffer_length = 0;

                SQLRETURN rc = SQLGetInfo(
                    conn_.get_handle(),
                    SQL_DRIVER_NAME,
                    buffer.data(),
                    size,
                    &buffer_length
                );

                // Should not crash - either SUCCESS or SUCCESS_WITH_INFO
                if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
                    all_passed = false;
                    r.actual = "Failed with buffer size " + std::to_string(size);
                    break;
                }

                if (auto breach = buffer.guard_breach()) {
                    all_passed = false;
                    r.actual = "Wrote past the " + std::to_string(size) +
                               "-byte buffer: offset " +
                               std::to_string(*breach) + ", guard area " +
                               buffer.guard_hex();
                    r.suggestion = "Driver wrote beyond buffer boundary";
                    break;
                }
            }

            if (all_passed) {
                r.status = TestStatus::PASS;
                r.actual = "No crash with small buffers (sizes 1-10)";
            } else {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
            }
        });
}

TestResult BufferValidationTests::test_sentinel_values() {
    return run_test(
        "test_sentinel_values", "SQLGetInfo",
        "Unused buffer preserved",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            // Use SQLGetInfoW with a driver-handled info type (SQL_DBMS_NAME) to
            // bypass the DM's ANSI↔Wide conversion layer, which can write extra
            // bytes beyond the null terminator and produce false failures.
            // The DM intercepts SQL_DRIVER_NAME itself, so we avoid that.
            // D64: guarded. The declared region keeps the 0xAA sentinel this
            // probe checks; the guard past it is what stops a manager scanning
            // for a wide terminator that the driver never wrote.
            const size_t buffer_size = 128;          // wide characters
            const size_t byte_size = buffer_size * sizeof(SQLWCHAR);
            const unsigned char sentinel = 0xAA;
            core::GuardedBuffer<SQLWCHAR> guarded(
                buffer_size, static_cast<SQLWCHAR>(0xAAAA),
                static_cast<SQLWCHAR>(0xAAAA));

            SQLWCHAR* wbuf = guarded.data();
            const unsigned char* raw =
                reinterpret_cast<const unsigned char*>(wbuf);
            SQLSMALLINT buffer_length = 0;

            SQLRETURN rc = SQLGetInfoW(
                conn_.get_handle(),
                SQL_DBMS_NAME,
                wbuf,
                static_cast<SQLSMALLINT>(byte_size),
                &buffer_length
            );

            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "Failed to get DBMS name via SQLGetInfoW";
                r.severity = Severity::ERR;
            } else {
                // buffer_length is in bytes (excluding null terminator) per ODBC spec for W-functions.
                // The driver should have written (buffer_length) bytes of data + a wide null terminator.
                // Total written bytes = buffer_length + sizeof(SQLWCHAR).
                size_t data_bytes = static_cast<size_t>(buffer_length);
                size_t total_written = data_bytes + sizeof(SQLWCHAR);  // data + wide NUL

                // Check that all bytes beyond total_written still have the sentinel
                bool sentinel_preserved = true;
                size_t first_modified = total_written;

                for (size_t i = total_written; i < byte_size; ++i) {
                    if (raw[i] != sentinel) {
                        sentinel_preserved = false;
                        first_modified = i;
                        break;
                    }
                }

                if (!sentinel_preserved) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Buffer modified at byte " + std::to_string(first_modified) +
                                   " (data=" + std::to_string(data_bytes) + " bytes + " +
                                   std::to_string(sizeof(SQLWCHAR)) + " byte NUL = " +
                                   std::to_string(total_written) + " bytes expected)";
                    r.suggestion = "Driver should only write needed bytes plus null terminator. "
                                       "This test uses SQLGetInfoW to bypass the DM's conversion layer.";
                    r.severity = Severity::WARNING;
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "Unused buffer preserved (data=" + std::to_string(data_bytes) +
                                   " bytes + wide NUL)";
                }
            }
        });
}

} // namespace odbc_crusher::tests
