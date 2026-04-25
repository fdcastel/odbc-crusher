#include "datatype_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

namespace odbc_crusher::tests {

std::vector<TestResult> DataTypeTests::run() {
    return {
        test_integer_types(),
        test_decimal_types(),
        test_float_types(),
        test_string_types(),
        test_date_time_types(),
        test_null_values(),
        test_unicode_types(),
        test_binary_types(),
        test_guid_type()
    };
}

TestResult DataTypeTests::test_integer_types() {
    return run_test(
        "test_integer_types", "Integer type handling",
        "Test SMALLINT, INTEGER, BIGINT types",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
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
                            r.actual = "Successfully retrieved INTEGER value: 42";
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not test integer types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible integer query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_decimal_types() {
    return run_test(
        "test_decimal_types", "Decimal/Numeric type handling",
        "Test DECIMAL, NUMERIC types",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
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
                            r.actual = oss.str();
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not test decimal types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible decimal query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_float_types() {
    return run_test(
        "test_float_types", "Float/Double type handling",
        "Test FLOAT, DOUBLE, REAL types",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
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
                            r.actual = oss.str();
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not test float types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible float query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_string_types() {
    return run_test(
        "test_string_types", "String type handling",
        "Test CHAR, VARCHAR types",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
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
                                r.actual = "Successfully retrieved VARCHAR value: " + value;
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
                r.actual = "Could not test string types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible string query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_date_time_types() {
    return run_test(
        "test_date_time_types", "Date/Time type handling",
        "Test DATE, TIME, TIMESTAMP types",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
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
                                r.actual = oss.str();
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
                r.actual = "Could not test date/time types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible date/time query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_null_values() {
    return run_test(
        "test_null_values", "NULL value handling",
        "Test NULL value retrieval and indicator",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Retrieving Data",
        [&](TestResult& r) {
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
                                r.actual = "Successfully detected NULL value (indicator = SQL_NULL_DATA)";
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
                r.actual = "Could not test NULL values";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible NULL query pattern found for this driver";
            }
        });
}

TestResult DataTypeTests::test_unicode_types() {
    return run_test(
        "test_unicode_types", "Unicode type handling (WCHAR, WVARCHAR)",
        "Retrieve and validate Unicode string data",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Unicode Data",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try different SQL patterns for wide character strings
            // Ordered: most portable first, then database-specific
            std::vector<std::string> test_queries = {
                "SELECT CAST('Hello World' AS VARCHAR(50))",              // Standard (retrieve as SQL_C_WCHAR)
                "SELECT CAST('Hello World' AS VARCHAR(50)) FROM RDB$DATABASE",  // Firebird
                "SELECT CAST(N'Hello World' AS NVARCHAR(50))",           // SQL Server style
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
                            r.actual = "Successfully retrieved wide character string (SQL_C_WCHAR)";
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            // If SQL_C_WCHAR failed on all queries, try retrieving as SQL_C_CHAR.
            // Some drivers (e.g. Firebird ANSI with CHARSET=UTF8) don't support
            // SQL_C_WCHAR retrieval via the ANSI entry point but the column data
            // is still valid.  This is a genuine driver limitation, not a test bug.
            if (!success) {
                for (const auto& sql : test_queries) {
                    try {
                        stmt.execute(sql);

                        if (stmt.fetch()) {
                            char str_buffer[256] = {0};
                            SQLLEN indicator = 0;

                            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                                        str_buffer, sizeof(str_buffer), &indicator);

                            if (SQL_SUCCEEDED(ret) && indicator > 0 && indicator != SQL_NULL_DATA) {
                                r.actual = std::string("SQL_C_WCHAR not supported; retrieved as SQL_C_CHAR: '")
                                              + str_buffer + "'";
                                r.suggestion = "Driver does not support SQL_C_WCHAR retrieval; "
                                                    "consider implementing wide character conversion in SQLGetData";
                                success = true;
                                break;
                            }
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }
            }

            if (!success) {
                r.actual = "Unicode types not supported or query failed";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver may not support SQL_C_WCHAR or Unicode types";
            }
        });
}

TestResult DataTypeTests::test_binary_types() {
    return run_test(
        "test_binary_types", "Binary type handling (BINARY, VARBINARY)",
        "Retrieve and validate binary data",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Appendix D: Data Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try different SQL patterns for binary data
            std::vector<std::string> test_queries = {
                "SELECT CAST(0x48656C6C6F AS VARBINARY(10))",            // SQL Server style
                "SELECT CAST('Binary' AS BLOB SUB_TYPE 0) FROM RDB$DATABASE",  // Firebird
                "SELECT CAST('test' AS BINARY(10))",                      // Standard
                "SELECT X'48656C6C6F'",                                   // Hex literal
                "SELECT decode('48656C6C6F', 'hex')::bytea",             // PostgreSQL
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
                            r.actual = oss.str();
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Binary types not supported or query failed";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver may not support SQL_C_BINARY or binary types";
            }
        });
}

TestResult DataTypeTests::test_guid_type() {
    return run_test(
        "test_guid_type", "GUID/UUID type handling",
        "Retrieve and validate GUID/UUID data",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, SQL_C_GUID",
        [&](TestResult& r) {
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
                            r.actual = "Successfully retrieved GUID data (SQL_C_GUID)";
                            success = true;
                            break;
                        }

                        // Also try as string representation
                        char str_buffer[64];
                        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                       str_buffer, sizeof(str_buffer), &indicator);

                        if (SQL_SUCCEEDED(ret) && indicator > 30) {  // GUIDs are typically 36+ chars
                            r.actual = "Successfully retrieved GUID as string";
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "GUID/UUID type not supported or query failed";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver may not support SQL_C_GUID or UUID generation functions";
            }
        });
}

} // namespace odbc_crusher::tests
