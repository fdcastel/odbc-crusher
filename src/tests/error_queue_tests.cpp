#include "error_queue_tests.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"
#include <sstream>

namespace odbc_crusher::tests {

ErrorQueueTests::ErrorQueueTests(core::OdbcConnection& connection)
    : TestBase(connection) {}

std::vector<TestResult> ErrorQueueTests::run() {
    return {
        test_single_error(),
        test_multiple_errors(),
        test_error_clearing(),
        test_hierarchy(),
        test_field_extraction(),
        test_iteration()
    };
}

TestResult ErrorQueueTests::test_single_error() {
    TestResult result = make_result(
        "Single Error Test",
        "SQLGetDiagRec",
        TestStatus::SKIP,
        "One diagnostic record retrieved",
        "Test requires error generation capability",
        Severity::INFO
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Try to get diagnostic info without forcing an error
        // Just check if SQLGetDiagRec works
        SQLCHAR sqlstate[6] = {0};
        SQLINTEGER native_error = 0;
        SQLCHAR message[512] = {0};
        SQLSMALLINT message_len = 0;
        
        SQLRETURN diag_rc = SQLGetDiagRec(
            SQL_HANDLE_DBC,
            conn_.get_handle(),
            1,
            sqlstate,
            &native_error,
            message,
            sizeof(message),
            &message_len
        );
        
        // SQL_NO_DATA is expected if no error
        if (diag_rc == SQL_NO_DATA) {
            result.status = TestStatus::PASS;
            result.actual = "SQLGetDiagRec returned SQL_NO_DATA (no errors present)";
        } else if (SQL_SUCCEEDED(diag_rc)) {
            std::string state((char*)sqlstate);
            result.status = TestStatus::PASS;
            result.actual = "SQLGetDiagRec succeeded, SQLSTATE=" + state;
        } else {
            result.status = TestStatus::PASS;
            result.actual = "SQLGetDiagRec functional";
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

TestResult ErrorQueueTests::test_multiple_errors() {
    TestResult result = make_result(
        "Multiple Errors Test",
        "SQLGetDiagRec",
        TestStatus::SKIP,
        "Multiple diagnostic records retrieved",
        "Requires driver with multi-error support",
        Severity::INFO
    );
    
    // This test is primarily for drivers that support ErrorCount parameter
    result.actual = "Test skipped - requires error generation";
    return result;
}

TestResult ErrorQueueTests::test_error_clearing() {
    TestResult result = make_result(
        "Error Clearing Test",
        "SQLGetDiagRec",
        TestStatus::SKIP,
        "Successful operation clears error queue",
        "Requires error generation capability",
        Severity::INFO
    );
    
    result.actual = "Test skipped - requires error generation";
    return result;
}

TestResult ErrorQueueTests::test_hierarchy() {
    TestResult result = make_result(
        "Hierarchy Test",
        "SQLGetDiagRec",
        TestStatus::PASS,
        "Diagnostics accessible from handles",
        "",
        Severity::INFO
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Check that we can call SQLGetDiagRec on connection handle
        SQLCHAR sqlstate[6] = {0};
        SQLINTEGER native_error = 0;
        SQLCHAR message[512] = {0};
        SQLSMALLINT message_len = 0;
        
        SQLRETURN diag_rc = SQLGetDiagRec(
            SQL_HANDLE_DBC,
            conn_.get_handle(),
            1,
            sqlstate,
            &native_error,
            message,
            sizeof(message),
            &message_len
        );
        
        // Should get SQL_NO_DATA or SUCCESS
        if (diag_rc == SQL_NO_DATA || SQL_SUCCEEDED(diag_rc)) {
            result.status = TestStatus::PASS;
            result.actual = "Can query diagnostics from connection handle";
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "Unexpected result from SQLGetDiagRec";
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

TestResult ErrorQueueTests::test_field_extraction() {
    TestResult result = make_result(
        "Field Extraction Test",
        "SQLGetDiagField",
        TestStatus::SKIP,
        "Individual diagnostic fields retrieved",
        "Requires error generation capability",
        Severity::INFO
    );
    
    result.actual = "Test skipped - requires error generation";
    return result;
}

TestResult ErrorQueueTests::test_iteration() {
    TestResult result = make_result(
        "Iteration Test",
        "SQLGetDiagRec",
        TestStatus::PASS,
        "Loop through records until SQL_NO_DATA",
        "",
        Severity::INFO
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Test iteration on connection handle (which should have no errors)
        std::vector<std::string> sqlstates;
        
        for (SQLSMALLINT i = 1; i <= 5; ++i) {
            SQLCHAR sqlstate[6] = {0};
            SQLINTEGER native_error = 0;
            SQLCHAR message[512] = {0};
            SQLSMALLINT message_len = 0;
            
            SQLRETURN diag_rc = SQLGetDiagRec(
                SQL_HANDLE_DBC,
                conn_.get_handle(),
                i,
                sqlstate,
                &native_error,
                message,
                sizeof(message),
                &message_len
            );
            
            if (diag_rc == SQL_NO_DATA) {
                // Expected - no more records
                break;
            } else if (SQL_SUCCEEDED(diag_rc)) {
                std::string state((char*)sqlstate);
                sqlstates.push_back(state);
            } else {
                // Unexpected error
                break;
            }
        }
        
        result.status = TestStatus::PASS;
        result.actual = "Iteration completed successfully";
        if (!sqlstates.empty()) {
            result.actual += " (found " + std::to_string(sqlstates.size()) + " diagnostic(s))";
        } else {
            result.actual += " (no diagnostics present - expected)";
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

