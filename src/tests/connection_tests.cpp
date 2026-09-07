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
                // A3: dbname_len is the total available length, and
                // SQL_SUCCEEDED accepts the 01004 that comes with truncation.
                // Firebird's SQL_DATABASE_NAME is a filesystem path, so >255
                // is realistic rather than theoretical.
                const auto db = bounded_string(reinterpret_cast<const char*>(dbname),
                                               sizeof(dbname), dbname_len);
                r.actual = "Database name: " + db.value;
                if (db.truncated) {
                    r.actual += " (truncated at " + std::to_string(sizeof(dbname) - 1) +
                                " bytes; driver reported " +
                                std::to_string(dbname_len) + ")";
                }
            } else {
                // B1: was SKIP_INCONCLUSIVE, and the suggestion even said
                // "SQLGetInfo succeeded" on the branch where it had not.
                // SQLGetInfo is Core and SQL_DATABASE_NAME is one of the
                // info types every driver must answer; a driver that cannot
                // is a finding. (An *empty* name is fine and lands in the
                // branch above - this is the call failing outright.)
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetInfo(SQL_DATABASE_NAME) returned " +
                           std::to_string(ret) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") + "]";
                r.suggestion =
                    "SQL_DATABASE_NAME is a Core information type. Returning "
                    "an empty string is allowed; failing the call is not.";
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

            // A3: see test_connection_info above.
            const auto name = bounded_string(reinterpret_cast<const char*>(driver_name),
                                             sizeof(driver_name), driver_name_len);
            r.actual = "Driver: " + name.value;
            if (name.truncated) {
                r.actual += " (truncated; driver reported " +
                            std::to_string(driver_name_len) + " bytes)";
            }
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
                // B1: SQLGetConnectAttr is Core and SQL_ATTR_AUTOCOMMIT is
                // not optional - every connection has one, and the whole
                // transaction category depends on being able to read it.
                // A14 is what happens downstream when this call fails and
                // nobody notices.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT) returned " +
                           std::to_string(ret) + " [" +
                           first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle(),
                                          "no diagnostic") + "]";
                r.suggestion =
                    "SQL_ATTR_AUTOCOMMIT is a Core connection attribute and "
                    "must be readable. An application that cannot read it "
                    "cannot restore it, and will leave the connection in "
                    "whatever mode the last operation chose.";
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
                // B1/B2: any timeout is legal, including 0 for "no timeout",
                // so there is nothing here to grade - the probe records what
                // the driver is configured with. Scoring it as PASS gave a
                // free point to every driver that answers the attribute.
                r.status = TestStatus::INFORMATIONAL;
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
                // B1/B2: every pooling mode is legal, and pooling is the
                // driver manager's setting rather than the driver's, so
                // there is nothing about the driver to grade here.
                r.status = TestStatus::INFORMATIONAL;
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
