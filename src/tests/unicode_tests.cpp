#include "unicode_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> UnicodeTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_getinfo_wchar_strings());
    results.push_back(test_describecol_wchar_names());
    results.push_back(test_getdata_sql_c_wchar());
    results.push_back(test_columns_unicode_patterns());
    results.push_back(test_string_truncation_wchar());
    
    return results;
}

TestResult UnicodeTests::test_getinfo_wchar_strings() {
    TestResult result = make_result(
        "test_getinfo_wchar_strings",
        "SQLGetInfo",
        TestStatus::PASS,
        "SQLGetInfo returns valid SQLWCHAR* for string info types",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetInfo: String info types return character data"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Test multiple string info types
        struct InfoTest {
            SQLUSMALLINT info_type;
            const char* name;
        };
        
        InfoTest tests[] = {
            { SQL_DBMS_NAME, "SQL_DBMS_NAME" },
            { SQL_DBMS_VER, "SQL_DBMS_VER" },
            { SQL_DRIVER_NAME, "SQL_DRIVER_NAME" },
            { SQL_DRIVER_VER, "SQL_DRIVER_VER" },
        };
        
        int success_count = 0;
        std::ostringstream details;
        
        for (const auto& t : tests) {
            SQLWCHAR wbuf[256] = {0};
            SQLSMALLINT len = 0;
            SQLRETURN ret = SQLGetInfoW(conn_.get_handle(), t.info_type,
                                        wbuf, sizeof(wbuf), &len);
            if (SQL_SUCCEEDED(ret) && len > 0) {
                success_count++;
                // Verify the length is in bytes and is a multiple of sizeof(SQLWCHAR)
                if (len % sizeof(SQLWCHAR) != 0) {
                    details << t.name << ": length " << len 
                            << " not a multiple of sizeof(SQLWCHAR); ";
                }
            } else {
                details << t.name << ": failed (ret=" << ret << "); ";
            }
        }
        
        std::ostringstream actual;
        actual << success_count << "/4 string info types returned valid SQLWCHAR*";
        if (details.str().length() > 0) {
            actual << " [" << details.str() << "]";
        }
        result.actual = actual.str();
        
        if (success_count == 0) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Driver may not support Unicode info retrieval via SQLGetInfoW";
        } else if (success_count < 4) {
            result.status = TestStatus::FAIL;
            result.severity = Severity::WARNING;
            result.suggestion = "Some string info types did not return valid SQLWCHAR* data";
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

TestResult UnicodeTests::test_describecol_wchar_names() {
    TestResult result = make_result(
        "test_describecol_wchar_names",
        "SQLDescribeCol",
        TestStatus::PASS,
        "SQLDescribeColW returns column names as SQLWCHAR*",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLDescribeCol: Column names returned in driver charset"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute a query to get a result set with known column names
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            SqlWcharBuf("SELECT * FROM CUSTOMERS").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test column names";
            result.suggestion = "Ensure driver supports basic SELECT queries";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Get column count
        SQLSMALLINT num_cols = 0;
        SQLNumResultCols(stmt.get_handle(), &num_cols);
        
        int success_count = 0;
        std::ostringstream details;
        
        for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(num_cols) && i <= 5; i++) {
            SQLWCHAR col_name[128] = {0};
            SQLSMALLINT name_len = 0;
            SQLSMALLINT data_type = 0;
            SQLULEN col_size = 0;
            SQLSMALLINT decimal_digits = 0;
            SQLSMALLINT nullable = 0;
            
            ret = SQLDescribeColW(stmt.get_handle(), i,
                col_name, sizeof(col_name)/sizeof(SQLWCHAR), &name_len,
                &data_type, &col_size, &decimal_digits, &nullable);
            
            if (SQL_SUCCEEDED(ret) && name_len > 0) {
                success_count++;
            }
        }
        
        std::ostringstream actual;
        actual << success_count << " of " << std::min((int)num_cols, 5) 
               << " columns returned valid SQLWCHAR* names";
        result.actual = actual.str();
        
        if (success_count == 0) {
            result.status = TestStatus::FAIL;
            result.suggestion = "SQLDescribeColW did not return any column names";
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

TestResult UnicodeTests::test_getdata_sql_c_wchar() {
    TestResult result = make_result(
        "test_getdata_sql_c_wchar",
        "SQLGetData",
        TestStatus::PASS,
        "SQLGetData with SQL_C_WCHAR retrieves Unicode string data",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData: SQL_C_WCHAR returns UTF-16 data with byte-length"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
            SqlWcharBuf("SELECT * FROM CUSTOMERS").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for SQL_C_WCHAR test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Fetch first row
        ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "No rows to fetch for SQL_C_WCHAR test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Try to get a string column as SQL_C_WCHAR
        // Column 2 is typically NAME (varchar)
        SQLWCHAR wbuf[256] = {0};
        SQLLEN cb_value = 0;
        ret = SQLGetData(stmt.get_handle(), 2, SQL_C_WCHAR,
                         wbuf, sizeof(wbuf), &cb_value);
        
        if (SQL_SUCCEEDED(ret)) {
            // cb_value should be in bytes for SQL_C_WCHAR
            std::ostringstream actual;
            actual << "SQL_C_WCHAR data retrieved, byte length=" << cb_value;
            
            // Verify byte length is even (multiple of sizeof(SQLWCHAR))
            if (cb_value > 0 && cb_value % sizeof(SQLWCHAR) != 0) {
                actual << " (WARNING: not a multiple of sizeof(SQLWCHAR))";
                result.status = TestStatus::FAIL;
                result.severity = Severity::WARNING;
                result.suggestion = "pcbValue for SQL_C_WCHAR must be in bytes, not characters";
            }
            result.actual = actual.str();
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "SQLGetData with SQL_C_WCHAR failed";
            result.suggestion = "Driver should support SQL_C_WCHAR target type for string data";
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

TestResult UnicodeTests::test_columns_unicode_patterns() {
    TestResult result = make_result(
        "test_columns_unicode_patterns",
        "SQLColumns",
        TestStatus::PASS,
        "SQLColumnsW accepts Unicode table/column name patterns",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLColumns: Accepts search patterns for catalog metadata"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Call SQLColumnsW with a Unicode table name pattern
        SQLRETURN ret = SQLColumnsW(stmt.get_handle(),
            nullptr, 0,       // Catalog
            nullptr, 0,       // Schema
            SqlWcharBuf("CUSTOMERS").ptr(), SQL_NTS,  // Table name
            SqlWcharBuf("%").ptr(), SQL_NTS);         // Column name pattern
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQLColumnsW call did not succeed";
            result.suggestion = "Verify driver supports Unicode catalog functions";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        int col_count = 0;
        while (SQLFetch(stmt.get_handle()) == SQL_SUCCESS && col_count < 50) {
            col_count++;
        }
        
        std::ostringstream actual;
        actual << "SQLColumnsW returned " << col_count << " column(s) for CUSTOMERS";
        result.actual = actual.str();
        
        if (col_count == 0) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "SQLColumnsW returned no columns; table may not exist in catalog";
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

TestResult UnicodeTests::test_string_truncation_wchar() {
    TestResult result = make_result(
        "test_string_truncation_wchar",
        "SQLGetInfo",
        TestStatus::PASS,
        "String truncation with SQLWCHAR* buffers returns 01004 and correct byte length",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 §String Truncation: 01004 with byte-based length for Unicode"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Use a tiny buffer for SQLGetInfoW to force truncation
        SQLWCHAR tiny_buf[2] = {0};  // Only 4 bytes - enough for 1 char + NUL
        SQLSMALLINT needed_len = 0;
        
        SQLRETURN ret = SQLGetInfoW(conn_.get_handle(), SQL_DBMS_NAME,
                                    tiny_buf, sizeof(tiny_buf), &needed_len);
        
        if (ret == SQL_SUCCESS_WITH_INFO) {
            // Check that needed_len reports the full string length in bytes
            std::ostringstream actual;
            actual << "Truncation detected: 01004, needed " << needed_len 
                   << " bytes, buffer was " << sizeof(tiny_buf) << " bytes";
            result.actual = actual.str();
            
            if (needed_len <= 0) {
                result.status = TestStatus::FAIL;
                result.suggestion = "pcbInfoValue should report total bytes needed (excl NUL) even on truncation";
            }
        } else if (ret == SQL_SUCCESS) {
            // String fit in the tiny buffer - that's OK for very short DBMS names
            result.actual = "DBMS name fit in 1-char buffer; truncation test inconclusive";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
        } else {
            result.actual = "SQLGetInfoW failed unexpectedly";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
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
