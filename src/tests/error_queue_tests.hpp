#pragma once

#include "test_base.hpp"
#include "../core/odbc_connection.hpp"
#include <vector>

namespace odbc_crusher {
namespace tests {

/**
 * @brief Error Queue Management Tests
 * 
 * Tests inspired by Microsoft ODBCTest to verify proper diagnostic record handling:
 * - Accumulation of diagnostic records
 * - Clearing error queues (new operations)
 * - Multiple diagnostic records per handle
 * - Error propagation across handle hierarchy
 * - SQLGetDiagRec iteration
 */
class ErrorQueueTests : public TestBase {
public:
    explicit ErrorQueueTests(core::OdbcConnection& connection);
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Error Queue Management"; }

private:
    // Test 1: One error, one diagnostic record
    TestResult test_single_error();
    
    // Test 2: Queue multiple diagnostics, retrieve all
    TestResult test_multiple_errors();
    
    // Test 3: Successful operation clears queue
    TestResult test_error_clearing();
    
    // Test 4: Errors propagate from statement to connection
    TestResult test_hierarchy();
    
    // Test 5: Individual diagnostic field retrieval
    TestResult test_field_extraction();
    
    // Test 6: Loop through records until SQL_NO_DATA
    TestResult test_iteration();
};

} // namespace tests
} // namespace odbc_crusher
