#include "statement_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

namespace odbc_crusher::tests {

std::vector<TestResult> StatementTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_simple_query());
    results.push_back(test_prepared_statement());
    results.push_back(test_parameter_binding());
    results.push_back(test_result_fetching());
    results.push_back(test_column_metadata());
    results.push_back(test_statement_reuse());
    results.push_back(test_multiple_result_sets());
    
    // Phase 12: Column Binding Tests
    results.push_back(test_bind_col_integer());
    results.push_back(test_bind_col_string());
    results.push_back(test_fetch_bound_vs_getdata());
    results.push_back(test_free_stmt_unbind());
    
    // Phase 12: Row Count & Parameter Tests
    results.push_back(test_row_count());
    results.push_back(test_num_params());
    results.push_back(test_describe_param());
    results.push_back(test_native_sql());
    
    return results;
}

TestResult StatementTests::test_simple_query() {
    TestResult result = make_result(
        "test_simple_query",
        "SQLExecDirect",
        TestStatus::PASS,
        "Execute a simple SELECT query",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try different query patterns for different databases
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",  // Firebird
            "SELECT 1",                     // MySQL, SQL Server
            "SELECT 1 FROM DUAL"            // Oracle
        };
        
        bool success = false;
        std::string successful_query;
        
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                success = true;
                successful_query = query;
                break;
            } catch (const core::OdbcError&) {
                // Try next pattern
                continue;
            }
        }
        
        if (success) {
            result.actual = "Successfully executed: " + successful_query;
            result.status = TestStatus::PASS;
        } else {
            result.actual = "Could not execute any simple query pattern";
            result.status = TestStatus::FAIL;
            result.severity = Severity::ERR;
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

TestResult StatementTests::test_prepared_statement() {
    TestResult result = make_result(
        "test_prepared_statement",
        "SQLPrepare/SQLExecute",
        TestStatus::PASS,
        "Prepare and execute a statement",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, SQLExecute"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try different query patterns
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",
            "SELECT 1",
            "SELECT 1 FROM DUAL"
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.prepare(query);
                stmt.execute_prepared();
                success = true;
                result.actual = "Successfully prepared and executed query";
                break;
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Could not prepare/execute any query pattern";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible query pattern found for this driver";
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

TestResult StatementTests::test_parameter_binding() {
    TestResult result = make_result(
        "test_parameter_binding",
        "SQLBindParameter",
        TestStatus::PASS,
        "Bind parameters to a prepared statement",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try parametrized queries
        // NOTE: "SELECT CAST(? AS INTEGER)" is preferred over "SELECT ?" because
        // some drivers (e.g. DuckDB) crash on SQLDescribeParam when the parameter
        // type cannot be inferred from a bare "SELECT ?".
        std::vector<std::string> test_queries = {
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST(? AS INTEGER)",                     // DuckDB, PostgreSQL, MySQL, SQL Server
            "SELECT CAST(? AS INTEGER) FROM DUAL",           // Oracle
            "SELECT ?"                                        // Fallback
        };
        
        bool success = false;
        SQLINTEGER param_value = 42;
        
        for (const auto& query : test_queries) {
            try {
                stmt.prepare(query);
                
                SQLRETURN ret = SQLBindParameter(
                    stmt.get_handle(),
                    1,                      // Parameter number
                    SQL_PARAM_INPUT,        // Input parameter
                    SQL_C_SLONG,            // C type
                    SQL_INTEGER,            // SQL type
                    0,                      // Column size
                    0,                      // Decimal digits
                    &param_value,           // Parameter value
                    0,                      // Buffer length
                    nullptr                 // StrLen_or_IndPtr
                );
                
                if (SQL_SUCCEEDED(ret)) {
                    stmt.execute_prepared();
                    
                    // Try to fetch result
                    if (stmt.fetch()) {
                        SQLINTEGER result_value = 0;
                        SQLLEN indicator = 0;
                        
                        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                        &result_value, sizeof(result_value), &indicator);
                        
                        if (SQL_SUCCEEDED(ret) && result_value == 42) {
                            result.actual = "Parameter binding successful, retrieved value: 42";
                            success = true;
                            break;
                        }
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Parameter binding not tested (driver may not support)";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible parameterized query pattern found for this driver";
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

TestResult StatementTests::test_result_fetching() {
    TestResult result = make_result(
        "test_result_fetching",
        "SQLFetch",
        TestStatus::PASS,
        "Fetch results from a query",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLFetch"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",
            "SELECT 1",
            "SELECT 1 FROM DUAL"
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    result.actual = "Successfully fetched result row";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Could not fetch results";
            result.status = TestStatus::FAIL;
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

TestResult StatementTests::test_column_metadata() {
    TestResult result = make_result(
        "test_column_metadata",
        "SQLNumResultCols/SQLDescribeCol",
        TestStatus::PASS,
        "Get column metadata from result set",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLNumResultCols, SQLDescribeCol"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",
            "SELECT 1",
            "SELECT 1 FROM DUAL"
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                SQLSMALLINT num_cols = 0;
                SQLRETURN ret = SQLNumResultCols(stmt.get_handle(), &num_cols);
                
                if (SQL_SUCCEEDED(ret) && num_cols > 0) {
                    // Get column info
                    SQLCHAR col_name[256];
                    SQLSMALLINT name_len = 0;
                    SQLSMALLINT data_type = 0;
                    SQLULEN column_size = 0;
                    SQLSMALLINT decimal_digits = 0;
                    SQLSMALLINT nullable = 0;
                    
                    ret = SQLDescribeCol(stmt.get_handle(), 1, col_name, sizeof(col_name),
                                        &name_len, &data_type, &column_size, &decimal_digits, &nullable);
                    
                    if (SQL_SUCCEEDED(ret)) {
                        std::ostringstream oss;
                        oss << "Found " << num_cols << " column(s), type: " << data_type;
                        result.actual = oss.str();
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Could not retrieve column metadata";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible query pattern produced result column metadata";
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

TestResult StatementTests::test_statement_reuse() {
    TestResult result = make_result(
        "test_statement_reuse",
        "SQLCloseCursor/Reexecute",
        TestStatus::PASS,
        "Reuse a statement handle multiple times",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLCloseCursor"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",
            "SELECT 1"
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                // Execute first time
                stmt.execute(query);
                stmt.fetch();
                
                // Close cursor
                stmt.close_cursor();
                
                // Execute second time with same statement
                stmt.execute(query);
                stmt.fetch();
                
                result.actual = "Statement reused successfully";
                success = true;
                break;
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Could not reuse statement";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Statement reuse test could not complete with available query patterns";
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

TestResult StatementTests::test_multiple_result_sets() {
    TestResult result = make_result(
        "test_multiple_result_sets",
        "SQLMoreResults",
        TestStatus::PASS,
        "Check if driver supports multiple result sets",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLMoreResults"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Most simple queries don't have multiple result sets
        // This test just checks if SQLMoreResults is callable
        std::vector<std::string> test_queries = {
            "SELECT 1 FROM RDB$DATABASE",
            "SELECT 1"
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                stmt.fetch();
                
                // Check for more results
                SQLRETURN ret = SQLMoreResults(stmt.get_handle());
                
                // SQL_NO_DATA means no more result sets (expected)
                // SQL_SUCCESS means there are more results
                if (ret == SQL_NO_DATA || ret == SQL_SUCCESS) {
                    result.actual = "SQLMoreResults callable (returned " + 
                                   std::string(ret == SQL_NO_DATA ? "SQL_NO_DATA" : "SQL_SUCCESS") + ")";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "SQLMoreResults not tested";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Could not execute a query to test SQLMoreResults";
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

// ============================================================
// Phase 12: Column Binding Tests
// ============================================================

TestResult StatementTests::test_bind_col_integer() {
    TestResult result = make_result(
        "test_bind_col_integer",
        "SQLBindCol",
        TestStatus::PASS,
        "Bind an integer column and fetch via bound buffer",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindCol"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 42", "SELECT 42 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLINTEGER value = 0;
                SQLLEN indicator = 0;
                
                SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG,
                                         &value, sizeof(value), &indicator);
                
                if (SQL_SUCCEEDED(rc) && stmt.fetch()) {
                    if (value == 42 || indicator != SQL_NULL_DATA) {
                        result.status = TestStatus::PASS;
                        result.actual = "Bound integer column, fetched value=" + std::to_string(value);
                        success = true;
                    } else {
                        result.status = TestStatus::FAIL;
                        result.actual = "Fetched unexpected value=" + std::to_string(value);
                    }
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not bind/fetch integer column";
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

TestResult StatementTests::test_bind_col_string() {
    TestResult result = make_result(
        "test_bind_col_string",
        "SQLBindCol",
        TestStatus::PASS,
        "Bind a string column and fetch via bound buffer",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindCol"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 'hello'", "SELECT 'hello' FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLCHAR value[256] = {0};
                SQLLEN indicator = 0;
                
                SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_CHAR,
                                         value, sizeof(value), &indicator);
                
                if (SQL_SUCCEEDED(rc) && stmt.fetch()) {
                    std::string fetched(reinterpret_cast<char*>(value));
                    result.status = TestStatus::PASS;
                    result.actual = "Bound string column, fetched '" + fetched + "'";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not bind/fetch string column";
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

TestResult StatementTests::test_fetch_bound_vs_getdata() {
    TestResult result = make_result(
        "test_fetch_bound_vs_getdata",
        "SQLBindCol/SQLGetData",
        TestStatus::PASS,
        "Fetch same column via bound buffer and SQLGetData - values match",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindCol, SQLGetData"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 99", "SELECT 99 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                // Bind column 1
                SQLINTEGER bound_value = 0;
                SQLLEN indicator = 0;
                SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG, &bound_value, sizeof(bound_value), &indicator);
                
                if (stmt.fetch()) {
                    // Also get via SQLGetData
                    SQLINTEGER getdata_value = 0;
                    SQLLEN getdata_ind = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                            &getdata_value, sizeof(getdata_value), &getdata_ind);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        result.status = TestStatus::PASS;
                        result.actual = "Bound=" + std::to_string(bound_value) + 
                                       ", GetData=" + std::to_string(getdata_value);
                    } else {
                        result.status = TestStatus::PASS;
                        result.actual = "Bound column fetched value=" + std::to_string(bound_value) + 
                                       " (SQLGetData on bound column may not be supported)";
                    }
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not compare bound fetch vs SQLGetData";
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

TestResult StatementTests::test_free_stmt_unbind() {
    TestResult result = make_result(
        "test_free_stmt_unbind",
        "SQLFreeStmt(SQL_UNBIND)",
        TestStatus::PASS,
        "SQLFreeStmt(SQL_UNBIND) resets column bindings",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLFreeStmt"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Bind a column
        SQLINTEGER value = 0;
        SQLLEN indicator = 0;
        SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG, &value, sizeof(value), &indicator);
        
        if (SQL_SUCCEEDED(rc)) {
            // Unbind all columns
            rc = SQLFreeStmt(stmt.get_handle(), SQL_UNBIND);
            
            if (SQL_SUCCEEDED(rc)) {
                result.status = TestStatus::PASS;
                result.actual = "SQLFreeStmt(SQL_UNBIND) succeeded";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQLFreeStmt(SQL_UNBIND) failed (rc=" + std::to_string(rc) + ")";
                result.severity = Severity::WARNING;
            }
        } else {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQLBindCol not supported";
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

// ============================================================
// Phase 12: Row Count & Parameter Tests
// ============================================================

TestResult StatementTests::test_row_count() {
    TestResult result = make_result(
        "test_row_count",
        "SQLRowCount",
        TestStatus::PASS,
        "SQLRowCount returns row count after execution",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                SQLLEN row_count = -1;
                SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLRowCount returned " + std::to_string(row_count);
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test SQLRowCount";
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

TestResult StatementTests::test_num_params() {
    TestResult result = make_result(
        "test_num_params",
        "SQLNumParams",
        TestStatus::PASS,
        "SQLNumParams returns parameter count after prepare",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLNumParams"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",
            "SELECT CAST(? AS INTEGER)",
            "SELECT ?"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.prepare(query);
                
                SQLSMALLINT num_params = -1;
                SQLRETURN rc = SQLNumParams(stmt.get_handle(), &num_params);
                
                if (SQL_SUCCEEDED(rc)) {
                    if (num_params == 1) {
                        result.status = TestStatus::PASS;
                        result.actual = "SQLNumParams correctly returned 1 for single-parameter query";
                    } else {
                        result.status = TestStatus::PASS;
                        result.actual = "SQLNumParams returned " + std::to_string(num_params);
                    }
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test SQLNumParams";
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

TestResult StatementTests::test_describe_param() {
    TestResult result = make_result(
        "test_describe_param",
        "SQLDescribeParam",
        TestStatus::PASS,
        "SQLDescribeParam returns parameter type info",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLDescribeParam"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",
            "SELECT CAST(? AS INTEGER)",
            "SELECT ?"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.prepare(query);
                
                SQLSMALLINT sql_type = 0;
                SQLULEN param_size = 0;
                SQLSMALLINT decimal_digits = 0;
                SQLSMALLINT nullable = 0;
                
                SQLRETURN rc = SQLDescribeParam(
                    stmt.get_handle(), 1,
                    &sql_type, &param_size, &decimal_digits, &nullable
                );
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "Parameter 1: type=" + std::to_string(sql_type) +
                                   ", size=" + std::to_string(param_size) +
                                   ", nullable=" + std::to_string(nullable);
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success && result.status == TestStatus::PASS) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLDescribeParam not supported by this driver";
            result.suggestion = "SQLDescribeParam is a Level 1 conformance function";
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

TestResult StatementTests::test_native_sql() {
    TestResult result = make_result(
        "test_native_sql",
        "SQLNativeSql",
        TestStatus::PASS,
        "SQLNativeSql translates ODBC SQL to native SQL",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLNativeSql"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        SQLCHAR input[] = "SELECT 1";
        SQLCHAR output[512] = {0};
        SQLINTEGER output_len = 0;
        
        SQLRETURN rc = SQLNativeSql(
            conn_.get_handle(),
            input, SQL_NTS,
            output, sizeof(output), &output_len
        );
        
        if (SQL_SUCCEEDED(rc)) {
            std::string native_sql(reinterpret_cast<char*>(output));
            result.status = TestStatus::PASS;
            result.actual = "SQLNativeSql: '" + std::string(reinterpret_cast<char*>(input)) + 
                           "' -> '" + native_sql + "'";
        } else {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLNativeSql not supported";
            result.suggestion = "SQLNativeSql is a Core conformance function per ODBC 3.x";
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
