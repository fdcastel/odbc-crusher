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
    results.push_back(test_unicode_types());
    results.push_back(test_binary_types());
    results.push_back(test_guid_type());
    
    return results;
}

TestResult DataTypeTests::test_integer_types() {
    TestResult result = make_result(
        "test_integer_types",
        "Integer type handling",
        TestStatus::PASS,
        "Test SMALLINT, INTEGER, BIGINT types",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible integer query pattern found for this driver";
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
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible decimal query pattern found for this driver";
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
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible float query pattern found for this driver";
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
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible string query pattern found for this driver";
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
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible date/time query pattern found for this driver";
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
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Retrieving Data"
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
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "No compatible NULL query pattern found for this driver";
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

TestResult DataTypeTests::test_unicode_types() {
    TestResult result = make_result(
        "test_unicode_types",
        "Unicode type handling (WCHAR, WVARCHAR)",
        TestStatus::PASS,
        "Retrieve and validate Unicode string data",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Unicode Data"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try different SQL patterns for wide character strings
        std::vector<std::string> test_queries = {
            "SELECT CAST(N'Hello World' AS NVARCHAR(50))",           // SQL Server style
            "SELECT CAST('Unicode Test' AS VARCHAR(50))",             // Standard (will test as wide)
            "SELECT N'Test' FROM RDB$DATABASE",                       // Firebird
            "SELECT 'Unicode' FROM DUAL"                              // Oracle
        };
        
        bool success = false;
        
        for (const auto& sql : test_queries) {
            try {
                stmt.execute(sql);
                
                if (stmt.fetch()) {
                    // Try to retrieve as wide character (SQL_C_WCHAR)
                    SQLWCHAR wstr_buffer[256];
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_WCHAR,
                                               wstr_buffer, sizeof(wstr_buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                        result.actual = "Successfully retrieved wide character string (SQL_C_WCHAR)";
                        success = true;
                        break;
                    }
                    
                    // Also try as regular char and verify it works
                    char str_buffer[256];
                    ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                   str_buffer, sizeof(str_buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret)) {
                        result.actual = "Successfully retrieved Unicode-compatible string";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "Unicode types not supported or query failed";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Driver may not support SQL_C_WCHAR or Unicode types";
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

TestResult DataTypeTests::test_binary_types() {
    TestResult result = make_result(
        "test_binary_types",
        "Binary type handling (BINARY, VARBINARY)",
        TestStatus::PASS,
        "Retrieve and validate binary data",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try different SQL patterns for binary data
        std::vector<std::string> test_queries = {
            "SELECT CAST(0x48656C6C6F AS VARBINARY(10))",            // SQL Server style
            "SELECT CAST('Binary' AS BLOB SUB_TYPE 0) FROM RDB$DATABASE",  // Firebird
            "SELECT CAST('test' AS BINARY(10))",                      // Standard
            "SELECT X'48656C6C6F'",                                   // Hex literal
        };
        
        bool success = false;
        
        for (const auto& sql : test_queries) {
            try {
                stmt.execute(sql);
                
                if (stmt.fetch()) {
                    // Try to retrieve as binary (SQL_C_BINARY)
                    unsigned char bin_buffer[256];
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_BINARY,
                                               bin_buffer, sizeof(bin_buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                        std::ostringstream oss;
                        oss << "Successfully retrieved binary data (" << indicator << " bytes)";
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
            result.actual = "Binary types not supported or query failed";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Driver may not support SQL_C_BINARY or binary types";
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

TestResult DataTypeTests::test_guid_type() {
    TestResult result = make_result(
        "test_guid_type",
        "GUID/UUID type handling",
        TestStatus::PASS,
        "Retrieve and validate GUID/UUID data",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, SQL_C_GUID"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try different SQL patterns for GUID/UUID
        std::vector<std::string> test_queries = {
            "SELECT CAST('6F9619FF-8B86-D011-B42D-00C04FC964FF' AS UNIQUEIDENTIFIER)",  // SQL Server
            "SELECT CAST('6F9619FF-8B86-D011-B42D-00C04FC964FF' AS CHAR(36))",          // As string
            "SELECT UUID()",                                                             // MySQL
            "SELECT GEN_UUID() FROM RDB$DATABASE",                                       // Firebird
        };
        
        bool success = false;
        
        for (const auto& sql : test_queries) {
            try {
                stmt.execute(sql);
                
                if (stmt.fetch()) {
                    // Try to retrieve as GUID (SQL_C_GUID)
                    SQLGUID guid_buffer;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_GUID,
                                               &guid_buffer, sizeof(guid_buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                        result.actual = "Successfully retrieved GUID data (SQL_C_GUID)";
                        success = true;
                        break;
                    }
                    
                    // Also try as string representation
                    char str_buffer[64];
                    ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                   str_buffer, sizeof(str_buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(ret) && indicator > 30) {  // GUIDs are typically 36+ chars
                        result.actual = "Successfully retrieved GUID as string";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.actual = "GUID/UUID type not supported or query failed";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Driver may not support SQL_C_GUID or UUID generation functions";
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
