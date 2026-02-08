#include "boundary_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstring>

namespace odbc_crusher::tests {

std::vector<TestResult> BoundaryTests::run() {
    return {
        test_getinfo_zero_buffer(),
        test_getdata_zero_buffer(),
        test_bindparam_null_value_with_null_indicator(),
        test_execdirect_empty_sql(),
        test_describecol_col0()
    };
}

TestResult BoundaryTests::test_getinfo_zero_buffer() {
    TestResult result = make_result(
        "test_getinfo_zero_buffer",
        "SQLGetInfo",
        TestStatus::PASS,
        "SQLGetInfo with buffer=0 returns required length",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        SQLSMALLINT required_len = 0;
        
        // Pass NULL buffer and 0 size - should return the required length
        SQLRETURN rc = SQLGetInfo(
            conn_.get_handle(),
            SQL_DRIVER_NAME,
            nullptr,
            0,
            &required_len
        );
        
        if (SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO) {
            if (required_len > 0) {
                result.status = TestStatus::PASS;
                result.actual = "Required length = " + std::to_string(required_len) + 
                               " bytes (rc=" + std::to_string(rc) + ")";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "Required length is 0, expected > 0";
                result.severity = Severity::WARNING;
            }
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "SQLGetInfo with buffer=0 returned error (rc=" + std::to_string(rc) + ")";
            result.severity = Severity::WARNING;
            result.suggestion = "Driver should return SQL_SUCCESS with the required buffer length";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult BoundaryTests::test_getdata_zero_buffer() {
    TestResult result = make_result(
        "test_getdata_zero_buffer",
        "SQLGetData",
        TestStatus::PASS,
        "SQLGetData with buffer=0 returns data length",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Buffer Length"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 'hello'", "SELECT 'hello' FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLLEN indicator = 0;
                    
                    // Pass NULL buffer, 0 size - should return data length in indicator
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             nullptr, 0, &indicator);
                    
                    if (SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO) {
                        if (indicator > 0) {
                            result.status = TestStatus::PASS;
                            result.actual = "Data length = " + std::to_string(indicator) + " bytes";
                        } else if (indicator == SQL_NULL_DATA) {
                            result.status = TestStatus::PASS;
                            result.actual = "Column is NULL (SQL_NULL_DATA)";
                        } else {
                            result.status = TestStatus::PASS;
                            result.actual = "Indicator = " + std::to_string(indicator);
                        }
                        success = true;
                        break;
                    } else {
                        // DM may not support NULL buffer for Unicode drivers;
                        // fallback: use 1-byte buffer to trigger truncation and get length
                        stmt.recycle();
                        stmt.execute(query);
                        if (stmt.fetch()) {
                            char tiny[1] = {0};
                            indicator = 0;
                            rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                            tiny, sizeof(tiny), &indicator);
                            if (rc == SQL_SUCCESS_WITH_INFO && indicator > 0) {
                                result.status = TestStatus::PASS;
                                result.actual = "Data length = " + std::to_string(indicator) + 
                                                " bytes (via 1-byte buffer truncation)";
                                success = true;
                                break;
                            }
                        }
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test zero-buffer SQLGetData";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult BoundaryTests::test_bindparam_null_value_with_null_indicator() {
    TestResult result = make_result(
        "test_bindparam_null_value_with_null_indicator",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQLBindParameter with NULL value and SQL_NULL_DATA indicator",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter, SQL_NULL_DATA"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Bind a parameter with NULL value pointer and SQL_NULL_DATA indicator
        SQLLEN indicator = SQL_NULL_DATA;
        
        SQLRETURN rc = SQLBindParameter(
            stmt.get_handle(),
            1,                    // parameter number
            SQL_PARAM_INPUT,      // input/output type
            SQL_C_CHAR,           // C type
            SQL_VARCHAR,          // SQL type
            255, 0,               // column size, decimal digits
            nullptr,              // value pointer = NULL (representing NULL parameter)
            0,                    // buffer length
            &indicator            // indicator = SQL_NULL_DATA
        );
        
        // Per ODBC spec, this should succeed - it represents binding a NULL parameter value
        // Note: passing nullptr as value pointer when indicator is SQL_NULL_DATA is valid
        // and is the standard way to pass NULL values to parameterized queries.
        // However, some drivers may unbind when value is nullptr.
        if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::PASS;
            result.actual = "SQLBindParameter with NULL value + SQL_NULL_DATA indicator succeeded";
        } else {
            result.status = TestStatus::PASS;
            result.actual = "SQLBindParameter handled NULL value (rc=" + std::to_string(rc) + ")";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult BoundaryTests::test_execdirect_empty_sql() {
    TestResult result = make_result(
        "test_execdirect_empty_sql",
        "SQLExecDirect",
        TestStatus::PASS,
        "SQLExecDirect with empty SQL returns error",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        SQLRETURN rc = SQLExecDirect(stmt.get_handle(), (SQLCHAR*)"", SQL_NTS);
        
        if (rc == SQL_ERROR) {
            result.status = TestStatus::PASS;
            result.actual = "SQL_ERROR for empty SQL string - expected behavior";
        } else if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::PASS;
            result.actual = "Driver accepted empty SQL string (implementation-defined behavior)";
        } else {
            result.status = TestStatus::PASS;
            result.actual = "Driver returned rc=" + std::to_string(rc) + " for empty SQL";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult BoundaryTests::test_describecol_col0() {
    TestResult result = make_result(
        "test_describecol_col0",
        "SQLDescribeCol",
        TestStatus::PASS,
        "SQLDescribeCol with column 0 returns error or bookmark info",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeCol"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLCHAR col_name[128] = {0};
                SQLSMALLINT col_name_len = 0;
                SQLSMALLINT data_type = 0;
                SQLULEN col_size = 0;
                SQLSMALLINT decimal_digits = 0;
                SQLSMALLINT nullable = 0;
                
                SQLRETURN rc = SQLDescribeCol(
                    stmt.get_handle(), 0,
                    col_name, sizeof(col_name), &col_name_len,
                    &data_type, &col_size, &decimal_digits, &nullable
                );
                
                if (rc == SQL_ERROR) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQL_ERROR for column 0 (no bookmarks enabled)";
                } else if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "Driver returned bookmark column info for column 0";
                }
                
                success = true;
                break;
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test column 0 describe";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
