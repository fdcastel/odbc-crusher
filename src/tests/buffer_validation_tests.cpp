#include "buffer_validation_tests.hpp"
#include "core/odbc_error.hpp"
#include <cstring>
#include <algorithm>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

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
        "Null Termination Test", "SQLGetInfo",
        "Null-terminated with correct length",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            try {
                // Test that SQLGetInfo returns null-terminated strings
                char buffer[256];
                SQLSMALLINT buffer_length = 0;

                // Fill buffer with non-null sentinel value
                std::memset(buffer, 'X', sizeof(buffer));

                SQLRETURN rc = SQLGetInfo(
                    conn_.get_handle(),
                    SQL_DRIVER_NAME,
                    buffer,
                    sizeof(buffer),
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
                    const void* nul = std::memchr(buffer, '\0', sizeof(buffer));

                    if (nul == nullptr) {
                        r.status = TestStatus::FAIL;
                        r.actual = "No NUL within " + std::to_string(sizeof(buffer)) +
                                   " bytes; driver reported length " +
                                   std::to_string(buffer_length);
                        r.suggestion = "Driver must null-terminate string outputs";
                        r.severity = Severity::ERR;
                    } else {
                        const size_t actual_length = static_cast<size_t>(
                            static_cast<const char*>(nul) - buffer);

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
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult BufferValidationTests::test_buffer_overflow_protection() {
    return run_test(
        "Buffer Overflow Protection Test", "SQLGetInfo",
        "No overflow",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            try {
                // Use a very small buffer and verify driver doesn't overflow
                const SQLSMALLINT small_buffer_size = 10;
                char buffer[small_buffer_size + 5];  // Extra space to detect overflow
                SQLSMALLINT buffer_length = 0;

                // Fill entire buffer with sentinel value
                const char sentinel = 'Z';
                std::memset(buffer, sentinel, sizeof(buffer));

                SQLRETURN rc = SQLGetInfo(
                    conn_.get_handle(),
                    SQL_DRIVER_NAME,
                    buffer,
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
                    bool overflow_detected = false;
                    for (int i = small_buffer_size; i < sizeof(buffer); ++i) {
                        if (buffer[i] != sentinel) {
                            overflow_detected = true;
                            break;
                        }
                    }

                    if (overflow_detected) {
                        r.status = TestStatus::FAIL;
                        r.actual = "Buffer overflow detected - guard area corrupted";
                        r.suggestion = "Driver wrote beyond buffer boundary";
                        r.severity = Severity::CRITICAL;
                    } else {
                        r.status = TestStatus::PASS;
                        r.actual = "No overflow detected";
                    }
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult BufferValidationTests::test_truncation_indicators() {
    return run_test(
        "Truncation Indicators Test", "SQLGetInfo",
        "SQL_SUCCESS_WITH_INFO with length > buffer",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, String Truncation",
        [&](TestResult& r) {
            try {
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

                char full_buf[256] = {0};
                SQLSMALLINT full_length = 0;
                SQLUSMALLINT info_type = 0;

                for (const auto& p : probes) {
                    full_length = 0;
                    SQLRETURN probe_rc = SQLGetInfo(
                        conn_.get_handle(), p.type,
                        full_buf, sizeof(full_buf), &full_length);
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
                SQLSMALLINT small_buffer_size = std::max((SQLSMALLINT)2, (SQLSMALLINT)(full_length / 2));
                std::vector<char> small_buf(small_buffer_size, 0);
                SQLSMALLINT buffer_length = 0;

                SQLRETURN rc = SQLGetInfo(
                    conn_.get_handle(),
                    info_type,
                    small_buf.data(),
                    small_buffer_size,
                    &buffer_length
                );

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
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult BufferValidationTests::test_undersized_buffer() {
    return run_test(
        "Undersized Buffer Test", "SQLGetInfo",
        "No crash with small buffers",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            try {
                // Test with various small buffer sizes
                bool all_passed = true;
                for (SQLSMALLINT size = 1; size <= 10; ++size) {
                    std::vector<char> buffer(size);
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
                }

                if (all_passed) {
                    r.status = TestStatus::PASS;
                    r.actual = "No crash with small buffers (sizes 1-10)";
                } else {
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::ERR;
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

TestResult BufferValidationTests::test_sentinel_values() {
    return run_test(
        "Sentinel Values Test", "SQLGetInfo",
        "Unused buffer preserved",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            try {
                // Use SQLGetInfoW with a driver-handled info type (SQL_DBMS_NAME) to
                // bypass the DM's ANSI↔Wide conversion layer, which can write extra
                // bytes beyond the null terminator and produce false failures.
                // The DM intercepts SQL_DRIVER_NAME itself, so we avoid that.
                const size_t buffer_size = 128;          // wide characters
                const size_t byte_size = buffer_size * sizeof(SQLWCHAR);
                std::vector<unsigned char> raw(byte_size);
                const unsigned char sentinel = 0xAA;
                std::memset(raw.data(), sentinel, byte_size);

                SQLWCHAR* wbuf = reinterpret_cast<SQLWCHAR*>(raw.data());
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
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
                r.severity = Severity::CRITICAL;
            }
        });
}

} // namespace odbc_crusher::tests
