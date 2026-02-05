#include "datatype_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

namespace odbc_crusher::tests {

std::vector<TestResult> DataTypeTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_integer_types());
    results.push_back(test_decimal_types());
    results.push_back(test_float_types());
    results.push_back(test_string_types());
    results.push_back(test_date_time_types());
    results.push_back(test_null_values());
    
    return results;
}

TestResult DataTypeTests::test_integer_types() {
    TestResult result = make_result(
        "test_integer_types",
        "Integer type handling",
        TestStatus::PASS,
        "Test SMALLINT, INTEGER, BIGINT types",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Test different integer queries
        std::vector<std::string> test_queries = {
            "SELECT CAST(42 AS INTEGER) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST(42 AS SIGNED)",                      // MySQL
            "SELECT 42"                                        // Generic
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                               &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && value == 42) {
                        result.actual = "Successfully retrieved INTEGER value: 42";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Could not test integer types";
            result.status = TestStatus::SKIP;
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

TestResult DataTypeTests::test_decimal_types() {
    TestResult result = make_result(
        "test_decimal_types",
        "Decimal/Numeric type handling",
        TestStatus::PASS,
        "Test DECIMAL, NUMERIC types",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT CAST(123.45 AS DECIMAL(10,2)) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST(123.45 AS DECIMAL(10,2))",                     // MySQL
            "SELECT 123.45"                                              // Generic
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQLDOUBLE value = 0.0;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                               &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && value > 123.0 && value < 124.0) {
                        std::ostringstream oss;
                        oss << "Successfully retrieved DECIMAL value: " << value;
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
            result.actual = "Could not test decimal types";
            result.status = TestStatus::SKIP;
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

TestResult DataTypeTests::test_float_types() {
    TestResult result = make_result(
        "test_float_types",
        "Float/Double type handling",
        TestStatus::PASS,
        "Test FLOAT, DOUBLE, REAL types",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT CAST(3.14159 AS DOUBLE PRECISION) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST(3.14159 AS DOUBLE)",                               // MySQL
            "SELECT 3.14159"                                                 // Generic
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQLDOUBLE value = 0.0;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                               &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && value > 3.0 && value < 3.2) {
                        std::ostringstream oss;
                        oss << "Successfully retrieved DOUBLE value: " << value;
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
            result.actual = "Could not test float types";
            result.status = TestStatus::SKIP;
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

TestResult DataTypeTests::test_string_types() {
    TestResult result = make_result(
        "test_string_types",
        "String type handling",
        TestStatus::PASS,
        "Test CHAR, VARCHAR types",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT CAST('Hello, ODBC!' AS VARCHAR(50)) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST('Hello, ODBC!' AS CHAR(50))",                        // MySQL
            "SELECT 'Hello, ODBC!'"                                           // Generic
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQLCHAR buffer[256] = {0};
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                               buffer, sizeof(buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret)) {
                        std::string value(reinterpret_cast<char*>(buffer));
                        // Trim trailing spaces
                        size_t end = value.find_last_not_of(" \t\n\r");
                        if (end != std::string::npos) {
                            value = value.substr(0, end + 1);
                        }
                        
                        if (value.find("Hello, ODBC!") != std::string::npos) {
                            result.actual = "Successfully retrieved VARCHAR value: " + value;
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
            result.actual = "Could not test string types";
            result.status = TestStatus::SKIP;
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

TestResult DataTypeTests::test_date_time_types() {
    TestResult result = make_result(
        "test_date_time_types",
        "Date/Time type handling",
        TestStatus::PASS,
        "Test DATE, TIME, TIMESTAMP types",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT CAST('2026-02-05' AS DATE) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST('2026-02-05' AS DATE)",                     // MySQL
            "SELECT DATE '2026-02-05'"                               // SQL-92
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQL_DATE_STRUCT date_value;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_TYPE_DATE,
                                               &date_value, sizeof(date_value), &indicator);
                    
                    if (SQL_SUCCEEDED(ret)) {
                        if (date_value.year == 2026 && date_value.month == 2 && date_value.day == 5) {
                            std::ostringstream oss;
                            oss << "Successfully retrieved DATE: " 
                                << date_value.year << "-" 
                                << (int)date_value.month << "-" 
                                << (int)date_value.day;
                            result.actual = oss.str();
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
            result.actual = "Could not test date/time types";
            result.status = TestStatus::SKIP;
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

TestResult DataTypeTests::test_null_values() {
    TestResult result = make_result(
        "test_null_values",
        "NULL value handling",
        TestStatus::PASS,
        "Test NULL value retrieval and indicator",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> test_queries = {
            "SELECT CAST(NULL AS INTEGER) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST(NULL AS SIGNED)",                      // MySQL
            "SELECT NULL"                                        // Generic
        };
        
        bool success = false;
        for (const auto& query : test_queries) {
            try {
                stmt.execute(query);
                
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                               &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(ret)) {
                        if (indicator == SQL_NULL_DATA) {
                            result.actual = "Successfully detected NULL value (indicator = SQL_NULL_DATA)";
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
            result.actual = "Could not test NULL values";
            result.status = TestStatus::SKIP;
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
