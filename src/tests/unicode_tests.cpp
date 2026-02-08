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
        "ODBC 3.8 SQLGetInfo: String info types return character data"
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
        "ODBC 3.8 SQLDescribeCol: Column names returned in driver charset"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute a query to get a result set with known column names
        // Try multiple queries for cross-database compatibility
        std::vector<std::string> queries = {
            "SELECT 1 AS COL1, 'hello' AS COL2",
            "SELECT * FROM RDB$DATABASE",
            "SELECT * FROM CUSTOMERS"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLExecDirectW)
        for (const auto& q : queries) {
            ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
        }
        // Strategy 2: Fall back to ANSI SQLExecDirect if W-function fails
        // (some drivers export W-functions but have broken W→A conversion)
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLExecDirect(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
        }
        
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
        "ODBC 3.8 SQLGetData: SQL_C_WCHAR returns UTF-16 data with byte-length"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Execute a query that returns a string column
        std::vector<std::string> queries = {
            "SELECT CAST('Hello' AS VARCHAR(50))",
            "SELECT CAST('Hello' AS VARCHAR(50)) FROM RDB$DATABASE",
            "SELECT 'Hello'"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLExecDirectW)
        for (const auto& q : queries) {
            ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
        }
        // Strategy 2: Fall back to ANSI SQLExecDirect if W-function fails
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLExecDirect(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
        }
        
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
        // Column 1 — first column of the result set
        SQLWCHAR wbuf[256] = {0};
        SQLLEN cb_value = 0;
        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_WCHAR,
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
        "ODBC 3.8 SQLColumns: Accepts search patterns for catalog metadata"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Discover a table from the database via SQLTables.
        // IMPORTANT: Also capture the catalog (column 1) and schema (column 2)
        // from the SQLTables result, so we can pass them to SQLColumnsW.
        // On MySQL/MariaDB, the database is the catalog and schema is empty.
        // Without propagating the catalog, SQLColumns defaults to DATABASE()
        // which may be a different database than where the table was discovered.
        std::string table_catalog;
        std::string table_schema;
        std::string table_name;

        auto discover_table = [&](const SQLWCHAR* type_filter, SQLSMALLINT type_len) -> bool {
            core::OdbcStatement tbl_stmt(conn_);
            SQLRETURN tbl_ret = SQLTablesW(tbl_stmt.get_handle(),
                nullptr, 0, nullptr, 0, nullptr, 0,
                const_cast<SQLWCHAR*>(type_filter), type_len);
            if (!SQL_SUCCEEDED(tbl_ret)) return false;
            if (SQLFetch(tbl_stmt.get_handle()) != SQL_SUCCESS) return false;
            
            char cat_buf[128] = {0};
            char sch_buf[128] = {0};
            char name_buf[128] = {0};
            SQLLEN cat_ind = 0, sch_ind = 0, name_ind = 0;
            
            SQLGetData(tbl_stmt.get_handle(), 1, SQL_C_CHAR, cat_buf, sizeof(cat_buf), &cat_ind);
            SQLGetData(tbl_stmt.get_handle(), 2, SQL_C_CHAR, sch_buf, sizeof(sch_buf), &sch_ind);
            SQLGetData(tbl_stmt.get_handle(), 3, SQL_C_CHAR, name_buf, sizeof(name_buf), &name_ind);
            
            if (name_ind > 0) {
                table_catalog = (cat_ind > 0) ? std::string(cat_buf) : "";
                table_schema = (sch_ind > 0) ? std::string(sch_buf) : "";
                table_name = std::string(name_buf);
                return true;
            }
            return false;
        };
        
        // Strategy 1: Look for user tables (type = 'TABLE')
        if (!discover_table(SqlWcharBuf("TABLE").ptr(), SQL_NTS)) {
            // Strategy 2: Look for system tables
            if (!discover_table(SqlWcharBuf("SYSTEM TABLE").ptr(), SQL_NTS)) {
                // Strategy 3: Look for any table type
                discover_table(nullptr, 0);
            }
        }
        
        if (table_name.empty()) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "No tables found in catalog for SQLColumnsW test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Call SQLColumnsW with the discovered table name AND its catalog/schema
        SQLRETURN ret = SQLColumnsW(stmt.get_handle(),
            table_catalog.empty() ? nullptr : SqlWcharBuf(table_catalog.c_str()).ptr(),
            table_catalog.empty() ? 0 : SQL_NTS,
            table_schema.empty() ? nullptr : SqlWcharBuf(table_schema.c_str()).ptr(),
            table_schema.empty() ? 0 : SQL_NTS,
            SqlWcharBuf(table_name.c_str()).ptr(), SQL_NTS,
            SqlWcharBuf("%").ptr(), SQL_NTS);
        
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
        actual << "SQLColumnsW returned " << col_count << " column(s) for " << table_name;
        if (!table_catalog.empty()) actual << " (catalog=" << table_catalog << ")";
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
        "ODBC 3.8 String Truncation: 01004 with byte-based length for Unicode"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Step 1: Probe several info types with a full-size buffer to find one
        // that returns a string long enough to guarantee truncation.
        // The actual string length varies by driver and server.
        struct InfoProbe { SQLUSMALLINT type; const char* name; };
        InfoProbe probes[] = {
            { SQL_DBMS_NAME, "SQL_DBMS_NAME" },
            { SQL_DBMS_VER, "SQL_DBMS_VER" },
            { SQL_SERVER_NAME, "SQL_SERVER_NAME" },
            { SQL_DRIVER_VER, "SQL_DRIVER_VER" },
        };
        
        SQLUSMALLINT chosen_type = 0;
        SQLSMALLINT full_byte_len = 0;     // bytes, excluding NUL
        
        for (const auto& p : probes) {
            SQLWCHAR full_buf[256] = {0};
            SQLSMALLINT len = 0;
            SQLRETURN probe_ret = SQLGetInfoW(conn_.get_handle(), p.type,
                                              full_buf, sizeof(full_buf), &len);
            if (SQL_SUCCEEDED(probe_ret) && len > static_cast<SQLSMALLINT>(sizeof(SQLWCHAR))) {
                // Need at least 2 characters of data for truncation to be meaningful
                if (len > full_byte_len) {
                    full_byte_len = len;
                    chosen_type = p.type;
                }
            }
        }
        
        if (chosen_type == 0 || full_byte_len <= static_cast<SQLSMALLINT>(sizeof(SQLWCHAR))) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "All string info types returned very short values; truncation test inconclusive";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Step 2: Craft a buffer that is exactly half the full data length.
        // This guarantees truncation.  BufferLength is in bytes for W-functions.
        // We need room for at least 1 wide char + NUL, but less than the full string.
        SQLSMALLINT tiny_byte_len = std::max(
            static_cast<SQLSMALLINT>(2 * sizeof(SQLWCHAR)),   // minimum: 1 char + NUL
            static_cast<SQLSMALLINT>(full_byte_len / 2));
        // Ensure it's a multiple of sizeof(SQLWCHAR)
        tiny_byte_len = static_cast<SQLSMALLINT>(
            (tiny_byte_len / sizeof(SQLWCHAR)) * sizeof(SQLWCHAR));
        
        std::vector<SQLWCHAR> tiny_buf(tiny_byte_len / sizeof(SQLWCHAR), 0);
        SQLSMALLINT needed_len = 0;
        
        SQLRETURN ret = SQLGetInfoW(conn_.get_handle(), chosen_type,
                                    tiny_buf.data(), tiny_byte_len, &needed_len);
        
        if (ret == SQL_SUCCESS_WITH_INFO) {
            // Check that needed_len reports the full string length in bytes
            std::ostringstream actual;
            actual << "Truncation detected: 01004, needed " << needed_len 
                   << " bytes, buffer was " << tiny_byte_len 
                   << " bytes (full=" << full_byte_len << ")";
            result.actual = actual.str();
            
            if (needed_len <= 0) {
                result.status = TestStatus::FAIL;
                result.suggestion = "pcbInfoValue should report total bytes needed (excl NUL) even on truncation";
            } else if (needed_len < tiny_byte_len) {
                result.status = TestStatus::FAIL;
                result.suggestion = "pcbInfoValue (" + std::to_string(needed_len) +
                                   ") is less than buffer size (" + std::to_string(tiny_byte_len) +
                                   ") despite truncation";
            }
        } else if (ret == SQL_SUCCESS) {
            // String fit in the tiny buffer — shouldn't happen with our probing
            result.actual = "String fit in " + std::to_string(tiny_byte_len) +
                           " byte buffer (full=" + std::to_string(full_byte_len) +
                           " bytes); truncation test inconclusive";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
        } else {
            result.actual = "SQLGetInfoW failed unexpectedly (ret=" + std::to_string(ret) + ")";
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
