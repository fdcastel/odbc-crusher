#include "connection_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> ConnectionTests::run() {
    return {
        test_connection_info(),
        test_connection_string_format(),
        test_multiple_statements(),
        test_connection_attributes(),
        test_connection_timeout(),
        test_connection_pooling(),
    };
}

TestResult ConnectionTests::test_connection_info() {
    return run_test(
        "test_connection_info", "SQLGetInfo",
        "Can retrieve connection information",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetInfo",
        [&](TestResult& r) {
            SQLCHAR dbname[256] = {0};
            SQLSMALLINT dbname_len = 0;

            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DATABASE_NAME,
                                       dbname, sizeof(dbname), &dbname_len);

            if (SQL_SUCCEEDED(ret)) {
                std::string db_name(reinterpret_cast<char*>(dbname), dbname_len);
                r.actual = "Database name: " + db_name;
            } else {
                r.actual = "Could not retrieve database name";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLGetInfo succeeded but database name unavailable";
            }
        });
}

TestResult ConnectionTests::test_connection_string_format() {
    return run_test(
        "test_connection_string_format", "SQLGetInfo(SQL_DRIVER_NAME)",
        "Connection is active and driver name is retrievable",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetInfo",
        [&](TestResult& r) {
            SQLCHAR driver_name[256] = {0};
            SQLSMALLINT driver_name_len = 0;

            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_DRIVER_NAME,
                                       driver_name, sizeof(driver_name), &driver_name_len);
            core::check_odbc_result(ret, SQL_HANDLE_DBC, conn_.get_handle(),
                                    "SQLGetInfo(SQL_DRIVER_NAME)");

            std::string name(reinterpret_cast<char*>(driver_name), driver_name_len);
            r.actual = "Driver: " + name;
        },
        TestStatus::FAIL);
}

TestResult ConnectionTests::test_multiple_statements() {
    return run_test(
        "test_multiple_statements", "SQLAllocHandle(STMT)",
        "Can allocate multiple statement handles on one connection",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLAllocHandle",
        [&](TestResult& r) {
            core::OdbcStatement stmt1(conn_);
            core::OdbcStatement stmt2(conn_);
            core::OdbcStatement stmt3(conn_);
            r.actual = "Successfully allocated 3 statement handles";
        },
        TestStatus::FAIL);
}

TestResult ConnectionTests::test_connection_attributes() {
    return run_test(
        "test_connection_attributes", "SQLGetConnectAttr",
        "Can get/set connection attributes",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetConnectAttr",
        [&](TestResult& r) {
            SQLUINTEGER autocommit = 0;
            SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                              &autocommit, 0, nullptr);
            if (SQL_SUCCEEDED(ret)) {
                std::ostringstream oss;
                oss << "Autocommit: " << (autocommit == SQL_AUTOCOMMIT_ON ? "ON" : "OFF");
                r.actual = oss.str();
            } else {
                r.actual = "Could not retrieve autocommit status";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLGetConnectAttr call did not succeed for SQL_ATTR_AUTOCOMMIT";
            }
        });
}

TestResult ConnectionTests::test_connection_timeout() {
    return run_test(
        "test_connection_timeout", "SQLGetConnectAttr(SQL_ATTR_CONNECTION_TIMEOUT)",
        "Can query connection timeout setting",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetConnectAttr",
        [&](TestResult& r) {
            SQLUINTEGER timeout = 0;
            SQLRETURN ret = SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_CONNECTION_TIMEOUT,
                                              &timeout, 0, nullptr);
            if (SQL_SUCCEEDED(ret)) {
                std::ostringstream oss;
                oss << "Connection timeout: " << timeout << " seconds";
                r.actual = oss.str();
            } else {
                r.actual = "Connection timeout attribute not supported";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "SQL_ATTR_CONNECTION_TIMEOUT is an optional connection attribute";
            }
        });
}

TestResult ConnectionTests::test_connection_pooling() {
    return run_test(
        "test_connection_pooling", "SQLGetEnvAttr/SQLSetEnvAttr(SQL_ATTR_CONNECTION_POOLING)",
        "Can query/set connection pooling mode",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLGetEnvAttr",
        [&](TestResult& r) {
            // Note: SQL_ATTR_CONNECTION_POOLING must be set BEFORE allocating environment
            // For this test, we just query the current setting
            SQLUINTEGER pooling_mode = 0;
            SQLINTEGER indicator = 0;
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
                r.actual = oss.str();
            } else {
                // Many drivers don't support connection pooling query
                r.actual = "Connection pooling not supported by driver";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Connection pooling is an optional ODBC feature";
            }
        });
}

} // namespace odbc_crusher::tests
