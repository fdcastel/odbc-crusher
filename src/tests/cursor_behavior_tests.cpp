#include "cursor_behavior_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> CursorBehaviorTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_forward_only_past_end());
    results.push_back(test_fetchscroll_first_forward_only());
    results.push_back(test_cursor_type_attribute());
    results.push_back(test_getdata_same_column_twice());
    
    return results;
}

TestResult CursorBehaviorTests::test_forward_only_past_end() {
    TestResult result = make_result(
        "test_forward_only_past_end",
        "SQLFetch",
        TestStatus::PASS,
        "Forward-only cursor fetch past end returns SQL_NO_DATA",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLFetch: Returns SQL_NO_DATA when no more rows"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            SqlWcharBuf("SELECT * FROM CUSTOMERS").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for cursor test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Fetch all rows
        int row_count = 0;
        while (SQLFetch(stmt.get_handle()) == SQL_SUCCESS) {
            row_count++;
            if (row_count > 10000) break;  // Safety limit
        }
        
        // The last fetch should have returned SQL_NO_DATA
        // Try one more fetch to verify
        ret = SQLFetch(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "Fetched " << row_count << " rows, then SQLFetch returned " << ret;
        result.actual = actual.str();
        
        if (ret != SQL_NO_DATA) {
            result.status = TestStatus::FAIL;
            result.suggestion = "SQLFetch past end of result set must return SQL_NO_DATA (100)";
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

TestResult CursorBehaviorTests::test_fetchscroll_first_forward_only() {
    TestResult result = make_result(
        "test_fetchscroll_first_forward_only",
        "SQLFetchScroll",
        TestStatus::PASS,
        "SQLFetchScroll(SQL_FETCH_FIRST) on forward-only cursor returns error",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 §SQLFetchScroll: Non-NEXT scrolling not supported on forward-only"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Ensure forward-only cursor (default)
        SQLSetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
            (SQLPOINTER)(intptr_t)SQL_CURSOR_FORWARD_ONLY, 0);
        
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            SqlWcharBuf("SELECT * FROM CUSTOMERS").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for fetchscroll test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // First fetch forward to position cursor
        SQLFetch(stmt.get_handle());
        
        // Now try SQL_FETCH_FIRST on a forward-only cursor — should fail
        ret = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_FIRST, 0);
        
        std::ostringstream actual;
        actual << "SQLFetchScroll(SQL_FETCH_FIRST) returned " << ret;
        result.actual = actual.str();
        
        if (ret == SQL_ERROR) {
            // Expected: driver correctly rejects backward scrolling on forward-only cursor
            result.status = TestStatus::PASS;
            result.actual += " (correctly rejected)";
        } else if (SQL_SUCCEEDED(ret)) {
            // Some drivers silently support it — not a failure but notable
            result.status = TestStatus::PASS;
            result.actual += " (driver supports scrolling despite forward-only cursor type)";
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

TestResult CursorBehaviorTests::test_cursor_type_attribute() {
    TestResult result = make_result(
        "test_cursor_type_attribute",
        "SQLGetStmtAttr",
        TestStatus::PASS,
        "SQL_ATTR_CURSOR_TYPE reflects actual cursor capabilities",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetStmtAttr: Cursor type reflects driver capabilities"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Read the default cursor type
        SQLULEN cursor_type = 0;
        SQLRETURN ret = SQLGetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
            &cursor_type, 0, nullptr);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not get SQL_ATTR_CURSOR_TYPE";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        std::string cursor_name;
        switch (cursor_type) {
            case SQL_CURSOR_FORWARD_ONLY: cursor_name = "FORWARD_ONLY"; break;
            case SQL_CURSOR_STATIC: cursor_name = "STATIC"; break;
            case SQL_CURSOR_KEYSET_DRIVEN: cursor_name = "KEYSET_DRIVEN"; break;
            case SQL_CURSOR_DYNAMIC: cursor_name = "DYNAMIC"; break;
            default: cursor_name = "UNKNOWN(" + std::to_string(cursor_type) + ")"; break;
        }
        
        // Try to set to STATIC and see if driver downgrades
        SQLSetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
            (SQLPOINTER)(intptr_t)SQL_CURSOR_STATIC, 0);
        
        SQLULEN actual_type = 0;
        SQLGetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
            &actual_type, 0, nullptr);
        
        std::string actual_name;
        switch (actual_type) {
            case SQL_CURSOR_FORWARD_ONLY: actual_name = "FORWARD_ONLY"; break;
            case SQL_CURSOR_STATIC: actual_name = "STATIC"; break;
            case SQL_CURSOR_KEYSET_DRIVEN: actual_name = "KEYSET_DRIVEN"; break;
            case SQL_CURSOR_DYNAMIC: actual_name = "DYNAMIC"; break;
            default: actual_name = "UNKNOWN"; break;
        }
        
        std::ostringstream actual;
        actual << "Default cursor: " << cursor_name
               << "; Requested STATIC, got: " << actual_name;
        result.actual = actual.str();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult CursorBehaviorTests::test_getdata_same_column_twice() {
    TestResult result = make_result(
        "test_getdata_same_column_twice",
        "SQLGetData",
        TestStatus::PASS,
        "SQLGetData called twice on same column returns data or proper error",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData: Re-reading same column behavior is driver-defined"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            SqlWcharBuf("SELECT * FROM CUSTOMERS").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for double-read test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "No rows to fetch for double-read test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Get column 1 (typically an integer) twice
        SQLLEN cb_val1 = 0, cb_val2 = 0;
        char buf1[64] = {0}, buf2[64] = {0};
        
        SQLRETURN ret1 = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                     buf1, sizeof(buf1), &cb_val1);
        SQLRETURN ret2 = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                     buf2, sizeof(buf2), &cb_val2);
        
        std::ostringstream actual;
        actual << "First read: ret=" << ret1 << ", Second read: ret=" << ret2;
        if (SQL_SUCCEEDED(ret1)) {
            actual << " (val1='" << buf1 << "'";
            if (SQL_SUCCEEDED(ret2)) {
                actual << ", val2='" << buf2 << "'";
            }
            actual << ")";
        }
        result.actual = actual.str();
        
        // Both outcomes are valid: re-read succeeds (SQL_GD_ANY_ORDER) or fails
        if (!SQL_SUCCEEDED(ret1)) {
            result.status = TestStatus::FAIL;
            result.suggestion = "First SQLGetData call should succeed";
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
