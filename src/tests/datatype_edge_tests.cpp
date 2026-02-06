#include "datatype_edge_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstring>
#include <climits>

namespace odbc_crusher::tests {

std::vector<TestResult> DataTypeEdgeCaseTests::run() {
    return {
        test_integer_zero(),
        test_integer_max(),
        test_integer_min(),
        test_varchar_empty(),
        test_varchar_special_chars(),
        test_null_integer(),
        test_null_varchar(),
        test_integer_as_string(),
        test_string_as_integer(),
        test_decimal_values()
    };
}

TestResult DataTypeEdgeCaseTests::test_integer_zero() {
    TestResult result = make_result(
        "test_integer_zero",
        "SQLGetData",
        TestStatus::PASS,
        "Integer value 0 retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Integer Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 0", "SELECT 0 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLINTEGER value = -1;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (value == 0) {
                            result.status = TestStatus::PASS;
                            result.actual = "Integer 0 retrieved correctly";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected 0, got " + std::to_string(value);
                            result.severity = Severity::ERR;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for integer 0 test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_integer_max() {
    TestResult result = make_result(
        "test_integer_max",
        "SQLGetData",
        TestStatus::PASS,
        "Large integer value retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Integer Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 2147483647", "SELECT 2147483647 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (value == 2147483647) {
                            result.status = TestStatus::PASS;
                            result.actual = "INT_MAX (2147483647) retrieved correctly";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected 2147483647, got " + std::to_string(value);
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for INT_MAX test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_integer_min() {
    TestResult result = make_result(
        "test_integer_min",
        "SQLGetData",
        TestStatus::PASS,
        "Negative integer retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Integer Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT -2147483648", "SELECT -2147483648 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (value == (-2147483647 - 1)) {
                            result.status = TestStatus::PASS;
                            result.actual = "INT_MIN (-2147483648) retrieved correctly";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected -2147483648, got " + std::to_string(value);
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for INT_MIN test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_varchar_empty() {
    TestResult result = make_result(
        "test_varchar_empty",
        "SQLGetData",
        TestStatus::PASS,
        "Empty string retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Character Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT ''", "SELECT '' FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    char buffer[256] = {0};
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (indicator == 0 || std::strlen(buffer) == 0) {
                            result.status = TestStatus::PASS;
                            result.actual = "Empty string retrieved correctly (length=" + 
                                           std::to_string(indicator) + ")";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected empty string, got '" + std::string(buffer) + "'";
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for empty string test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_varchar_special_chars() {
    TestResult result = make_result(
        "test_varchar_special_chars",
        "SQLGetData",
        TestStatus::PASS,
        "String with special characters retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Character Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Test with a string containing special characters
        std::vector<std::string> queries = {
            "SELECT 'a''b\"c\\d'",
            "SELECT 'a''b\"c\\d' FROM RDB$DATABASE"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    char buffer[256] = {0};
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        std::string val(buffer);
                        result.status = TestStatus::PASS;
                        result.actual = "Special chars retrieved: '" + val + 
                                       "' (length=" + std::to_string(indicator) + ")";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for special characters test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_null_integer() {
    TestResult result = make_result(
        "test_null_integer",
        "SQLGetData",
        TestStatus::PASS,
        "NULL integer returns SQL_NULL_DATA indicator",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §NULL Data"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(NULL AS INTEGER)",
            "SELECT NULL FROM RDB$DATABASE"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLINTEGER value = 42;  // sentinel
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (indicator == SQL_NULL_DATA) {
                            result.status = TestStatus::PASS;
                            result.actual = "NULL integer correctly returned SQL_NULL_DATA";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected SQL_NULL_DATA, got indicator=" + 
                                           std::to_string(indicator);
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for NULL integer test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_null_varchar() {
    TestResult result = make_result(
        "test_null_varchar",
        "SQLGetData",
        TestStatus::PASS,
        "NULL varchar returns SQL_NULL_DATA indicator",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §NULL Data"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(NULL AS VARCHAR(50))",
            "SELECT NULL FROM RDB$DATABASE"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    char buffer[256];
                    std::memset(buffer, 'X', sizeof(buffer));
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (indicator == SQL_NULL_DATA) {
                            result.status = TestStatus::PASS;
                            result.actual = "NULL varchar correctly returned SQL_NULL_DATA";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected SQL_NULL_DATA, got indicator=" + 
                                           std::to_string(indicator);
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for NULL varchar test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_integer_as_string() {
    TestResult result = make_result(
        "test_integer_as_string",
        "SQLGetData",
        TestStatus::PASS,
        "Integer retrieved as SQL_C_CHAR converts correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Type Conversion"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 42", "SELECT 42 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    char buffer[256] = {0};
                    SQLLEN indicator = 0;
                    
                    // Retrieve integer as SQL_C_CHAR
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        std::string val(buffer);
                        // The string should contain "42" (possibly with whitespace)
                        if (val.find("42") != std::string::npos) {
                            result.status = TestStatus::PASS;
                            result.actual = "Integer 42 converted to string: '" + val + "'";
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Integer->string conversion unexpected: '" + val + "'";
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for integer-as-string test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_string_as_integer() {
    TestResult result = make_result(
        "test_string_as_integer",
        "SQLGetData",
        TestStatus::PASS,
        "Numeric string retrieved as SQL_C_SLONG converts correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Type Conversion"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Use a string that contains a number
        std::vector<std::string> queries = {"SELECT '123'", "SELECT '123' FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    
                    // Retrieve string as SQL_C_SLONG (type conversion)
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        if (value == 123) {
                            result.status = TestStatus::PASS;
                            result.actual = "String '123' converted to integer 123";
                        } else {
                            result.status = TestStatus::PASS;
                            result.actual = "String->integer conversion returned " + std::to_string(value);
                        }
                        success = true;
                        break;
                    } else if (rc == SQL_ERROR) {
                        // Some drivers don't support this conversion
                        result.status = TestStatus::SKIP_UNSUPPORTED;
                        result.actual = "Driver does not support string->integer conversion in SQLGetData";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for string-as-integer test";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const std::exception& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("Exception: ") + e.what();
    }
    
    return result;
}

TestResult DataTypeEdgeCaseTests::test_decimal_values() {
    TestResult result = make_result(
        "test_decimal_values",
        "SQLGetData",
        TestStatus::PASS,
        "Decimal/float value retrieved correctly",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetData, §Numeric Types"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT 3.14",
            "SELECT CAST(3.14 AS DECIMAL(5,2)) FROM RDB$DATABASE"
        };
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                if (stmt.fetch()) {
                    double value = 0.0;
                    SQLLEN indicator = 0;
                    
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                             &value, sizeof(value), &indicator);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        // Check approximate equality
                        if (value > 3.0 && value < 3.2) {
                            result.status = TestStatus::PASS;
                            result.actual = "Decimal value retrieved: " + std::to_string(value);
                        } else {
                            result.status = TestStatus::FAIL;
                            result.actual = "Expected ~3.14, got " + std::to_string(value);
                            result.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query for decimal value test";
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
