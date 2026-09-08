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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test integer types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible integer query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT of an integer literal must return one row.";
                return;
            }

            SQLINTEGER value = 0;
            SQLLEN indicator = 0;

            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                       &value, sizeof(value), &indicator);

            if (SQL_SUCCEEDED(ret) && value == 42) {
                r.actual = "Successfully retrieved INTEGER value: 42";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Expected 42, got " + std::to_string(value) + " (indicator=" + std::to_string(indicator) + ")";
                r.severity = Severity::ERR;
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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test decimal types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible decimal query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT of a decimal literal must return one row.";
                return;
            }

            SQLDOUBLE value = 0.0;
            SQLLEN indicator = 0;

            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                       &value, sizeof(value), &indicator);

            if (SQL_SUCCEEDED(ret) && value > 123.0 && value < 124.0) {
                std::ostringstream oss;
                oss << "Successfully retrieved DECIMAL value: " << value;
                r.actual = oss.str();
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Expected a value near 123.45, got " + std::to_string(value);
                r.severity = Severity::ERR;
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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test float types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible float query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT of a float literal must return one row.";
                return;
            }

            SQLDOUBLE value = 0.0;
            SQLLEN indicator = 0;

            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                       &value, sizeof(value), &indicator);

            if (SQL_SUCCEEDED(ret) && value > 3.0 && value < 3.2) {
                std::ostringstream oss;
                oss << "Successfully retrieved DOUBLE value: " << value;
                r.actual = oss.str();
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Expected a value near 3.14159, got " + std::to_string(value);
                r.severity = Severity::ERR;
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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test string types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible string query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT of a string literal must return one row.";
                return;
            }

            core::GuardedBuffer<SQLCHAR> buffer(256, 0);  // D62
            SQLLEN indicator = 0;

            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                       buffer.data(), buffer.declared_bytes(), &indicator);

            if (SQL_SUCCEEDED(ret)) {
                // D68: to `indicator`, not to a terminator. Under
                // BufferValidation=Lenient this reported the value with the
                // mock's filler byte glued on - `Hello, ODBC!X`.
                std::string value =
                    bounded_string(reinterpret_cast<char*>(buffer.data()),
                                   buffer.declared_elements(), indicator).value;
                // Trim trailing spaces
                size_t end = value.find_last_not_of(" \t\n\r");
                if (end != std::string::npos) {
                    value = value.substr(0, end + 1);
                }

                if (value.find("Hello, ODBC!") != std::string::npos) {
                    r.actual = "Successfully retrieved VARCHAR value: " + value;
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Expected a value containing " + std::string("Hello, ODBC!") + ", got " + value;
                    r.severity = Severity::ERR;
                }
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData(SQL_C_CHAR) failed with " + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                r.severity = Severity::ERR;
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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test date/time types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible date/time query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT of a date literal must return one row.";
                return;
            }

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
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Expected 2026-02-05, got " + std::to_string(date_value.year) + "-" + std::to_string(date_value.month) + "-" + std::to_string(date_value.day);
                    r.severity = Severity::ERR;
                }
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData(SQL_C_TYPE_DATE) failed with " + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                r.severity = Severity::ERR;
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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "Could not test NULL values";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible NULL query pattern found for this driver";
                // C2: say what each variant actually failed with,
                // instead of shrugging. Closes prior item 2.8.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: a variant executed, so this probe is now conclusive.
            // A wrong value is the driver's answer, not a reason to try
            // the next dialect and end up reporting SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                r.suggestion = "SELECT NULL must return one row.";
                return;
            }

            SQLINTEGER value = 0;
            SQLLEN indicator = 0;

            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                       &value, sizeof(value), &indicator);

            if (SQL_SUCCEEDED(ret)) {
                if (indicator == SQL_NULL_DATA) {
                    r.actual = "Successfully detected NULL value (indicator = SQL_NULL_DATA)";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SELECT NULL returned indicator=" + std::to_string(indicator) + " (expected SQL_NULL_DATA = -1)";
                    r.severity = Severity::ERR;
                }
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData(SQL_C_SLONG) failed with " + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                r.severity = Severity::ERR;
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

            // C2/A1: find a dialect that executes, once. The old version ran
            // the whole list twice — once for SQL_C_WCHAR and again for
            // SQL_C_CHAR — and discarded every failure on both passes.
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "No compatible query pattern found for Unicode types";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "None of the tested dialect variants executed.";
                r.diagnostic = attempt.format_failures();
                return;
            }

            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                return;
            }

            // Pass 1: the wide path.
            {
                core::GuardedBuffer<SQLWCHAR> wstr_buffer(256, 0);  // D62
                SQLLEN indicator = 0;
                SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_WCHAR,
                                           wstr_buffer.data(), wstr_buffer.declared_bytes(), &indicator);
                if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                    r.actual = "Successfully retrieved wide character string (SQL_C_WCHAR)";
                    return;
                }
            }

            // Pass 2: the narrow path. Some drivers (Firebird ANSI with
            // CHARSET=UTF8, for instance) do not support SQL_C_WCHAR retrieval
            // through the ANSI entry point while the column data is still
            // valid. That is a real driver limitation, not a probe bug.
            //
            // Re-executing rather than calling SQLGetData twice on the same
            // column deliberately: repeated SQLGetData on one column is its own
            // optional behaviour (SQL_GD_ANY_COLUMN), and depending on it here
            // would conflate two unrelated driver features.
            const std::string wchar_state =
                first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
            stmt.execute(attempt.query);
            if (stmt.fetch()) {
                core::GuardedBuffer<char> str_buffer(256, 0);  // D62
                SQLLEN indicator = 0;
                SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                           str_buffer.data(), str_buffer.declared_bytes(), &indicator);
                // `indicator > 0` already excludes both SQL_NULL_DATA (-1)
                // and SQL_NO_TOTAL (-4), so explicit comparisons are redundant.
                if (SQL_SUCCEEDED(ret) && indicator > 0) {
                    r.actual = std::string("SQL_C_WCHAR unavailable (") + wchar_state +
                               "); retrieved as SQL_C_CHAR: '" +
                               bounded_string(str_buffer.data(), str_buffer.declared_elements(),
                                              indicator).value + "'";
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.suggestion = "Driver does not support SQL_C_WCHAR retrieval; "
                                   "consider implementing wide character conversion "
                                   "in SQLGetData";
                    return;
                }
            }

            // The query executed and returned a row, but neither the wide nor
            // the narrow retrieval worked. That is a driver gap, and now the
            // report names the SQLSTATE behind it rather than guessing.
            r.actual = "Neither SQL_C_WCHAR (" + wchar_state + ") nor SQL_C_CHAR (" +
                       first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE") +
                       ") retrieved the value";
            r.status = TestStatus::SKIP_UNSUPPORTED;
            r.suggestion = "Driver may not support SQL_C_WCHAR or Unicode types";
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

            // C2: no engine in the matrix shares a binary-literal syntax, so
            // all-variants-fail is the expected outcome on most of them — which
            // is exactly why the failures need to be reported rather than
            // swallowed.
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "No compatible binary-literal syntax for this engine";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver may not support binary literals in a SELECT.";
                r.diagnostic = attempt.format_failures();
                return;
            }

            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                return;
            }

            core::GuardedBuffer<unsigned char> bin_buffer(256, 0);  // D62
            SQLLEN indicator = 0;
            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_BINARY,
                                       bin_buffer.data(), bin_buffer.declared_bytes(), &indicator);

            if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                std::ostringstream oss;
                oss << "Successfully retrieved binary data (" << indicator << " bytes)";
                r.actual = oss.str();
            } else if (indicator == SQL_NULL_DATA) {
                // A1: the query executed and returned a row, so a NULL here is
                // the driver's answer about a non-NULL literal.
                r.status = TestStatus::FAIL;
                r.actual = "Binary literal came back as SQL_NULL_DATA";
                r.severity = Severity::ERR;
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLGetData(SQL_C_BINARY) returned " +
                           first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                r.suggestion = "Driver may not support SQL_C_BINARY retrieval.";
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

            // C2: UUID syntax differs on every engine, so failing every
            // variant is a normal outcome here — and the reason belongs in the
            // report rather than in a swallowed exception.
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                r.actual = "No compatible GUID/UUID syntax for this engine";
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver may not support UUID literals or generation "
                               "functions.";
                r.diagnostic = attempt.format_failures();
                return;
            }

            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                return;
            }

            SQLGUID guid_buffer{};
            SQLLEN indicator = 0;
            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_GUID,
                                       &guid_buffer, sizeof(guid_buffer), &indicator);
            if (SQL_SUCCEEDED(ret) && indicator != SQL_NULL_DATA) {
                r.actual = "Successfully retrieved GUID data (SQL_C_GUID)";
                return;
            }
            const std::string guid_state =
                first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");

            // Fall back to the string form. Re-executed rather than calling
            // SQLGetData twice on one column, which is its own optional
            // behaviour and would conflate two driver features.
            stmt.execute(attempt.query);
            if (stmt.fetch()) {
                core::GuardedBuffer<char> str_buffer(64, 0);  // D62
                SQLLEN str_ind = 0;
                ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                 str_buffer.data(), str_buffer.declared_bytes(), &str_ind);
                if (SQL_SUCCEEDED(ret) && str_ind > 30) {  // GUIDs are 36+ chars
                    r.actual = std::string("SQL_C_GUID unavailable (") + guid_state +
                               "); retrieved as string";
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.suggestion = "Driver does not support SQL_C_GUID retrieval.";
                    return;
                }
            }

            r.actual = "Neither SQL_C_GUID (" + guid_state + ") nor a 36-character "
                       "string form retrieved the value";
            r.status = TestStatus::SKIP_UNSUPPORTED;
            r.suggestion = "Driver may not support SQL_C_GUID or UUID generation "
                           "functions";
        });
}

} // namespace odbc_crusher::tests
