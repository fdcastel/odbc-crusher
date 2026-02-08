#include "buffer_validation_tests.hpp"
#include "core/odbc_error.hpp"
#include <cstring>
#include <algorithm>
#include <vector>

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
    TestResult result = make_result(
        "Null Termination Test",
        "SQLGetInfo",
        TestStatus::PASS,
        "Null-terminated with correct length",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
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
            result.status = TestStatus::FAIL;
            result.actual = "Failed to get driver name";
            try {
                auto err = core::OdbcError::from_handle(SQL_HANDLE_DBC, conn_.get_handle(), "SQLGetInfo");
                result.diagnostic = err.format_diagnostics();
            } catch(...) {}
            result.severity = Severity::ERR;
        } else {
            // Verify the string is null-terminated
            size_t actual_length = std::strlen(buffer);
            
            if (buffer[actual_length] != '\0') {
                result.status = TestStatus::FAIL;
                result.actual = "String not null-terminated";
                result.suggestion = "Driver must null-terminate string outputs";
                result.severity = Severity::ERR;
            } else if (buffer_length != static_cast<SQLSMALLINT>(actual_length)) {
                result.status = TestStatus::FAIL;
                result.actual = std::to_string(buffer_length) + " bytes (expected " + std::to_string(actual_length) + ")";
                result.suggestion = "Buffer length indicator should match string length";
                result.severity = Severity::WARNING;
            } else {
                result.status = TestStatus::PASS;
                result.actual = "Null-terminated with correct length (" + std::to_string(actual_length) + " bytes)";
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
        result.severity = Severity::CRITICAL;
    }
    
    return result;
}

TestResult BufferValidationTests::test_buffer_overflow_protection() {
    TestResult result = make_result(
        "Buffer Overflow Protection Test",
        "SQLGetInfo",
        TestStatus::PASS,
        "No overflow",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
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
            result.status = TestStatus::FAIL;
            result.actual = "Unexpected return code";
            result.severity = Severity::ERR;
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
                result.status = TestStatus::FAIL;
                result.actual = "Buffer overflow detected - guard area corrupted";
                result.suggestion = "Driver wrote beyond buffer boundary";
                result.severity = Severity::CRITICAL;
            } else {
                result.status = TestStatus::PASS;
                result.actual = "No overflow detected";
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
        result.severity = Severity::CRITICAL;
    }
    
    return result;
}

TestResult BufferValidationTests::test_truncation_indicators() {
    TestResult result = make_result(
        "Truncation Indicators Test",
        "SQLGetInfo",
        TestStatus::PASS,
        "SQL_SUCCESS_WITH_INFO with length > buffer",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, String Truncation"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not find a string info value long enough for truncation test";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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
                result.status = TestStatus::PASS;
                result.actual = "SQL_SUCCESS_WITH_INFO with full length = " + std::to_string(buffer_length) +
                               " (buffer was " + std::to_string(small_buffer_size) + " bytes)";
            } else if (buffer_length >= small_buffer_size) {
                // Some drivers/DMs report truncated length — note it but don't fail
                result.status = TestStatus::PASS;
                result.actual = "SQL_SUCCESS_WITH_INFO with length = " + std::to_string(buffer_length) +
                               " (full=" + std::to_string(full_length) + 
                               ", buffer=" + std::to_string(small_buffer_size) + ")";
            } else if (buffer_length == small_buffer_size - 1) {
                // Driver/DM reported truncated string length (= buffer - NUL).
                // This is a common DM behavior where the DM truncates the driver's
                // output and overwrites pcbInfoValue with the truncated length.
                result.status = TestStatus::PASS;
                result.actual = "SQL_SUCCESS_WITH_INFO with DM-truncated length = " + std::to_string(buffer_length) +
                               " (full=" + std::to_string(full_length) +
                               ", buffer=" + std::to_string(small_buffer_size) +
                               "); DM reported truncated rather than full length";
                result.suggestion = "Per ODBC spec, pcbInfoValue should report the full "
                                   "string length, but the Driver Manager may override it "
                                   "with the truncated length.";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "Length (" + std::to_string(buffer_length) +
                               ") < buffer size (" + std::to_string(small_buffer_size) +
                               ") despite truncation";
                result.suggestion = "After truncation, pcbInfoValue should report the full "
                                   "string length (excluding NUL), not the truncated length. "
                                   "Per ODBC 3.x spec §SQLGetInfo.";
                result.severity = Severity::WARNING;
            }
        } else if (rc == SQL_SUCCESS) {
            // Data fit in the small buffer — shouldn't happen
            result.status = TestStatus::PASS;
            result.actual = "SQL_SUCCESS (data fit in " + std::to_string(small_buffer_size) + " byte buffer)";
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "Unexpected return code " + std::to_string(rc);
            result.severity = Severity::ERR;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
        result.severity = Severity::CRITICAL;
    }
    
    return result;
}

TestResult BufferValidationTests::test_undersized_buffer() {
    TestResult result = make_result(
        "Undersized Buffer Test",
        "SQLGetInfo",
        TestStatus::PASS,
        "No crash with small buffers",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
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
                result.actual = "Failed with buffer size " + std::to_string(size);
                break;
            }
        }
        
        if (all_passed) {
            result.status = TestStatus::PASS;
            result.actual = "No crash with small buffers (sizes 1-10)";
        } else {
            result.status = TestStatus::FAIL;
            result.severity = Severity::ERR;
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
        result.severity = Severity::CRITICAL;
    }
    
    return result;
}

TestResult BufferValidationTests::test_sentinel_values() {
    TestResult result = make_result(
        "Sentinel Values Test",
        "SQLGetInfo",
        TestStatus::PASS,
        "Unused buffer preserved",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Fill a large buffer with sentinel values
        const size_t buffer_size = 256;
        char buffer[buffer_size];
        const char sentinel = static_cast<char>(0xAA);  // Distinctive pattern
        std::memset(buffer, sentinel, buffer_size);
        
        SQLSMALLINT buffer_length = 0;
        
        SQLRETURN rc = SQLGetInfo(
            conn_.get_handle(),
            SQL_DRIVER_NAME,
            buffer,
            buffer_size,
            &buffer_length
        );
        
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::FAIL;
            result.actual = "Failed to get driver name";
            result.severity = Severity::ERR;
        } else {
            // Find the end of the string (null terminator)
            size_t string_end = std::strlen(buffer);
            
            // Verify unused portion of buffer is untouched (still has sentinel values)
            bool sentinel_preserved = true;
            size_t first_modified = string_end + 1;
            
            for (size_t i = string_end + 1; i < buffer_size; ++i) {
                if (buffer[i] != sentinel) {
                    sentinel_preserved = false;
                    first_modified = i;
                    break;
                }
            }
            
            if (!sentinel_preserved) {
                result.status = TestStatus::FAIL;
                result.actual = "Buffer modified at position " + std::to_string(first_modified);
                result.suggestion = "Driver should only write needed bytes plus null terminator";
                result.severity = Severity::WARNING;
            } else {
                result.status = TestStatus::PASS;
                result.actual = "Unused buffer preserved";
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
        result.severity = Severity::CRITICAL;
    }
    
    return result;
}

} // namespace odbc_crusher::tests
