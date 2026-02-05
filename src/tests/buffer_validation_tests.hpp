#pragma once

#include "test_base.hpp"
#include "../core/odbc_connection.hpp"
#include <vector>

namespace odbc_crusher {
namespace tests {

/**
 * @brief Buffer Validation Tests
 * 
 * Tests inspired by Microsoft ODBCTest to verify proper buffer handling:
 * - Null termination of string outputs
 * - Buffer overflow protection
 * - Truncation behavior and indicators
 * - Sentinel value preservation in unused buffer space
 * - SQL_NTS vs explicit length handling
 */
class BufferValidationTests : public TestBase {
public:
    explicit BufferValidationTests(core::OdbcConnection& connection);
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Buffer Validation"; }

private:
    // Test 1: Verify null termination of string outputs
    TestResult test_null_termination();
    
    // Test 2: Ensure drivers respect buffer boundaries
    TestResult test_buffer_overflow_protection();
    
    // Test 3: Check SQL_SUCCESS_WITH_INFO and length indicators
    TestResult test_truncation_indicators();
    
    // Test 4: Pass undersized buffers, verify no crash
    TestResult test_undersized_buffer();
    
    // Test 5: Fill buffers with sentinel, verify untouched areas
    TestResult test_sentinel_values();
};

} // namespace tests
} // namespace odbc_crusher
