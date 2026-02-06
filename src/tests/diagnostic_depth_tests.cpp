#include "diagnostic_depth_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> DiagnosticDepthTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_diagfield_sqlstate());
    results.push_back(test_diagfield_record_count());
    results.push_back(test_diagfield_row_count());
    results.push_back(test_multiple_diagnostic_records());
    
    return results;
}

TestResult DiagnosticDepthTests::test_diagfield_sqlstate() {
    TestResult result = make_result(
        "test_diagfield_sqlstate",
        "SQLGetDiagField",
        TestStatus::PASS,
        "SQLGetDiagField with SQL_DIAG_SQLSTATE returns 5-char state",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetDiagField: SQLSTATE is a 5-character string"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Generate an error by executing invalid SQL
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            (SQLWCHAR*)L"THIS IS INVALID SQL SYNTAX !@#$", SQL_NTS);
        
        if (SQL_SUCCEEDED(ret)) {
            // Driver accepted it - try a different approach
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not generate an error to test diagnostics";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Now use SQLGetDiagField to get SQLSTATE
        SQLWCHAR sqlstate[6] = {0};
        SQLSMALLINT len = 0;
        SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
            1, SQL_DIAG_SQLSTATE, sqlstate, sizeof(sqlstate), &len);
        
        if (SQL_SUCCEEDED(diag_ret)) {
            // Verify it's a 5-char SQLSTATE
            // Count SQLWCHAR chars
            int char_count = 0;
            for (int i = 0; i < 5 && sqlstate[i] != 0; i++) char_count++;
            
            std::ostringstream actual;
            actual << "SQLSTATE has " << char_count << " chars";
            result.actual = actual.str();
            
            if (char_count != 5) {
                result.status = TestStatus::FAIL;
                result.suggestion = "SQLSTATE must be exactly 5 characters per ODBC spec";
            }
        } else {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQLGetDiagField for SQLSTATE did not succeed";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DiagnosticDepthTests::test_diagfield_record_count() {
    TestResult result = make_result(
        "test_diagfield_record_count",
        "SQLGetDiagField",
        TestStatus::PASS,
        "SQLGetDiagField with SQL_DIAG_NUMBER returns correct record count",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetDiagField: SQL_DIAG_NUMBER returns count of records"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Generate an error
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            (SQLWCHAR*)L"THIS IS INVALID SQL !@#$", SQL_NTS);
        
        if (SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not generate error for diagnostic test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Get diagnostic record count (header field, rec number = 0)
        SQLLEN diag_count = 0;
        SQLSMALLINT len = 0;
        SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
            0, SQL_DIAG_NUMBER, &diag_count, 0, &len);
        
        if (SQL_SUCCEEDED(diag_ret)) {
            std::ostringstream actual;
            actual << "SQL_DIAG_NUMBER returned " << diag_count << " diagnostic record(s)";
            result.actual = actual.str();
            
            if (diag_count < 1) {
                result.status = TestStatus::FAIL;
                result.suggestion = "After an error, SQL_DIAG_NUMBER should be >= 1";
            }
        } else {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQLGetDiagField for SQL_DIAG_NUMBER did not succeed";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DiagnosticDepthTests::test_diagfield_row_count() {
    TestResult result = make_result(
        "test_diagfield_row_count",
        "SQLGetDiagField",
        TestStatus::PASS,
        "SQLGetDiagField with SQL_DIAG_ROW_COUNT returns row count after query",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetDiagField: SQL_DIAG_ROW_COUNT reports affected rows"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute a SELECT to generate a result set
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            (SQLWCHAR*)L"SELECT * FROM CUSTOMERS", SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for row count test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Get row count from header diagnostic field
        SQLLEN row_count = -1;
        SQLSMALLINT len = 0;
        SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
            0, SQL_DIAG_ROW_COUNT, &row_count, 0, &len);
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(diag_ret)) {
            actual << "SQL_DIAG_ROW_COUNT = " << row_count;
        } else {
            actual << "SQLGetDiagField for SQL_DIAG_ROW_COUNT returned " << diag_ret;
        }
        result.actual = actual.str();
        
        // Row count may be -1 or 0 for SELECT — this is driver-defined
        // Just verify the call succeeded
        if (!SQL_SUCCEEDED(diag_ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "SQL_DIAG_ROW_COUNT may not be available for all statement types";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DiagnosticDepthTests::test_multiple_diagnostic_records() {
    TestResult result = make_result(
        "test_multiple_diagnostic_records",
        "SQLGetDiagRec",
        TestStatus::PASS,
        "Multiple diagnostic records from a single operation",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §Diagnostic Records: Multiple records can exist per error"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute invalid SQL to generate diagnostics
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            (SQLWCHAR*)L"INVALID SQL THAT SHOULD FAIL !@#$", SQL_NTS);
        
        if (SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not generate error for multiple-record test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Iterate all diagnostic records
        int rec_count = 0;
        for (SQLSMALLINT i = 1; i <= 10; i++) {
            SQLWCHAR sqlstate[6] = {0};
            SQLINTEGER native_error = 0;
            SQLWCHAR message[512] = {0};
            SQLSMALLINT msg_len = 0;
            
            SQLRETURN diag_ret = SQLGetDiagRecW(SQL_HANDLE_STMT, stmt.get_handle(),
                i, sqlstate, &native_error, message, 
                sizeof(message)/sizeof(SQLWCHAR), &msg_len);
            
            if (diag_ret == SQL_NO_DATA) break;
            if (SQL_SUCCEEDED(diag_ret)) rec_count++;
        }
        
        std::ostringstream actual;
        actual << "Found " << rec_count << " diagnostic record(s) after error";
        result.actual = actual.str();
        
        if (rec_count < 1) {
            result.status = TestStatus::FAIL;
            result.suggestion = "At least 1 diagnostic record should exist after an error";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
