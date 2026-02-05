#pragma once

#include "test_base.hpp"
#include "../core/odbc_connection.hpp"
#include <vector>

namespace odbc_crusher {
namespace tests {

/**
 * @brief State Machine Validation Tests
 * 
 * Tests to verify proper ODBC state machine compliance:
 * - Handle state transitions
 * - Operations in invalid states
 * - State changes after operations
 * - State reset operations
 * - Proper error returns for state violations
 */
class StateMachineTests : public TestBase {
public:
    explicit StateMachineTests(core::OdbcConnection& connection);
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "State Machine Validation"; }

private:
    // Test 1: Normal operation sequence works
    TestResult test_valid_transitions();
    
    // Test 2: Operations in wrong state fail with proper error
    TestResult test_invalid_operation();
    
    // Test 3: SQLCloseCursor, SQLFreeStmt reset state correctly
    TestResult test_state_reset();
    
    // Test 4: Repeated prepare/execute transitions
    TestResult test_prepare_execute_cycle();
    
    // Test 5: Operations fail on disconnected connection
    TestResult test_connection_state();
    
    // Test 6: Independent state tracking per statement
    TestResult test_multiple_statements();
};

} // namespace tests
} // namespace odbc_crusher
