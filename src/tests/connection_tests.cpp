#include "connection_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> ConnectionTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_connection_info());
    results.push_back(test_connection_string_format());
    results.push_back(test_multiple_statements());
    results.push_back(test_connection_attributes());
    results.push_back(test_connection_timeout());
    results.push_back(test_connection_pooling());
    
    return results;
}

TestResult ConnectionTests::test_connection_info() {
    TestResult result = make_result(
        "test_connection_info",
        "SQLGetInfo",
        TestStatus::PASS,
        "Can retrieve connection information",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Try to get database name
        SQLCHAR dbname[256] = {0};
        SQLSMALLINT dbname_len = 0;
        
        SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DATABASE_NAME, 
                                   dbname, sizeof(dbname), &dbname_len);
        
        if (SQL_SUCCEEDED(ret)) {
            std::string db_name(reinterpret_cast<char*>(dbname), dbname_len);
            result.actual = "Database name: " + db_name;
            result.status = TestStatus::PASS;
        } else {
            result.actual = "Could not retrieve database name";
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

TestResult ConnectionTests::test_connection_string_format() {
    TestResult result = make_result(
        "test_connection_string_format",
        "SQLGetInfo(SQL_DRIVER_NAME)",
        TestStatus::PASS,
        "Connection is active and driver name is retrievable",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        SQLCHAR driver_name[256] = {0};
        SQLSMALLINT driver_name_len = 0;
        
        SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DRIVER_NAME,
                                   driver_name, sizeof(driver_name), &driver_name_len);
        
        core::check_odbc_result(ret, SQL_HANDLE_DBC, conn_.get_handle(), "SQLGetInfo(SQL_DRIVER_NAME)");
        
        std::string name(reinterpret_cast<char*>(driver_name), driver_name_len);
        result.actual = "Driver: " + name;
        result.status = TestStatus::PASS;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::FAIL;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        result.severity = Severity::ERR;
    }
    
    return result;
}

TestResult ConnectionTests::test_multiple_statements() {
    TestResult result = make_result(
        "test_multiple_statements",
        "SQLAllocHandle(STMT)",
        TestStatus::PASS,
        "Can allocate multiple statement handles on one connection",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Allocate multiple statements
        core::OdbcStatement stmt1(conn_);
        core::OdbcStatement stmt2(conn_);
        core::OdbcStatement stmt3(conn_);
        
        result.actual = "Successfully allocated 3 statement handles";
        result.status = TestStatus::PASS;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::FAIL;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        result.severity = Severity::ERR;
    }
    
    return result;
}

TestResult ConnectionTests::test_connection_attributes() {
    TestResult result = make_result(
        "test_connection_attributes",
        "SQLGetConnectAttr",
        TestStatus::PASS,
        "Can get/set connection attributes",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Try to get autocommit mode
        SQLUINTEGER autocommit = 0;
        SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                          &autocommit, 0, nullptr);
        
        if (SQL_SUCCEEDED(ret)) {
            std::ostringstream oss;
            oss << "Autocommit: " << (autocommit == SQL_AUTOCOMMIT_ON ? "ON" : "OFF");
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "Could not retrieve autocommit status";
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

TestResult ConnectionTests::test_connection_timeout() {
    TestResult result = make_result(
        "test_connection_timeout",
        "SQLGetConnectAttr(SQL_ATTR_CONNECTION_TIMEOUT)",
        TestStatus::PASS,
        "Can query connection timeout setting",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        SQLUINTEGER timeout = 0;
        SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_CONNECTION_TIMEOUT,
                                          &timeout, 0, nullptr);
        
        if (SQL_SUCCEEDED(ret)) {
            std::ostringstream oss;
            oss << "Connection timeout: " << timeout << " seconds";
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "Connection timeout attribute not supported";
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

TestResult ConnectionTests::test_connection_pooling() {
    TestResult result = make_result(
        "test_connection_pooling",
        "SQLGetEnvAttr/SQLSetEnvAttr(SQL_ATTR_CONNECTION_POOLING)",
        TestStatus::PASS,
        "Can query/set connection pooling mode",
        "",
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Get current connection pooling setting (environment attribute)
        SQLUINTEGER pooling_mode = 0;
        SQLINTEGER indicator = 0;
        
        // Note: SQL_ATTR_CONNECTION_POOLING must be set BEFORE allocating environment
        // For this test, we just query the current setting
        SQLHENV henv = conn_.get_environment().get();
        SQLRETURN ret = SQLGetEnvAttr(henv, SQL_ATTR_CONNECTION_POOLING,
                                      &pooling_mode, sizeof(pooling_mode), &indicator);
        
        if (SQL_SUCCEEDED(ret)) {
            std::ostringstream oss;
            oss << "Connection pooling mode: ";
            switch (pooling_mode) {
                case SQL_CP_OFF:
                    oss << "OFF (SQL_CP_OFF)";
                    break;
                case SQL_CP_ONE_PER_DRIVER:
                    oss << "ONE_PER_DRIVER (SQL_CP_ONE_PER_DRIVER)";
                    break;
                case SQL_CP_ONE_PER_HENV:
                    oss << "ONE_PER_HENV (SQL_CP_ONE_PER_HENV)";
                    break;
                default:
                    oss << "Unknown (" << pooling_mode << ")";
                    break;
            }
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            // Many drivers don't support connection pooling query
            result.actual = "Connection pooling not supported by driver";
            result.status = TestStatus::SKIP;
            result.suggestion = "This is normal - connection pooling is optional ODBC feature";
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
