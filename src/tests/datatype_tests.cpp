#include "datatype_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cctype>

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
        test_guid_type(),
        // P9 (IMPROVEMENT_PLAN_V2)
        test_guid_parameter_binding()
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

// ── P9 (IMPROVEMENT_PLAN_V2) — SQL_C_GUID as a *parameter* ─────────────
//
// `SQL_C_GUID` appeared once in the whole suite, in `test_guid_type` above, on
// the **output** path via `SQLGetData`. The defect PR #296 fixed is entirely on
// the input path: `SQLBindParameter(SQL_C_GUID, SQL_GUID, …, ptr, 16, &len)`
// returned `SQL_SUCCESS` and the server never saw the sixteen UUID bytes. A
// binary target got the ASCII of the first sixteen characters of the canonical
// string; a text target under `CHARSET=UTF8` got UTF-16 on the wire, because
// the driver mapped UTF8 text to a wide C type and picked the wide converter.
//
// Real applications do exactly this: DuckDB's ODBC scanner binds its `::UUID`
// values this way, and its Firebird round-trip test was parked on the bug.
//
// The probe is written round-trip rather than against a literal, so it needs no
// engine-specific UUID function to *check* the answer — only one to make the
// server render what it received. Both shapes come from issue #295, and both
// are tried, because the two failure modes are different: a binary parameter
// slot and a text one exercise different converters.
TestResult DataTypeTests::test_guid_parameter_binding() {
    return run_test(
        "test_guid_parameter_binding",
        "SQLBindParameter(SQL_C_GUID)",
        "A 16-byte SQL_C_GUID bound as a parameter reaches the server as that "
        "UUID, not as the text of its canonical form",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter — SQL_C_GUID input conversion",
        [&](TestResult& r) {
            // A0EEBC99-9C0B-4EF8-BB6D-6BB9BD380A11, the example from the issue.
            // Data1/2/3 are little-endian fields in SQLGUID and big-endian on
            // the wire, which is the conversion under test; Data4 is bytes.
            SQLGUID guid{};
            guid.Data1 = 0xA0EEBC99u;
            guid.Data2 = 0x9C0B;
            guid.Data3 = 0x4EF8;
            const unsigned char d4[8] = {0xBB, 0x6D, 0x6B, 0xB9,
                                         0xBD, 0x38, 0x0A, 0x11};
            std::memcpy(guid.Data4, d4, sizeof(d4));
            static const char* const kCanonical =
                "A0EEBC99-9C0B-4EF8-BB6D-6BB9BD380A11";

            // Statements that hand the bound UUID back as text. Each engine
            // spells this its own way and most have nothing like it; a driver
            // with no UUID rendering simply skips, which is a fact about the
            // engine rather than a verdict on the driver.
            struct Shape { const char* sql; const char* what; };
            static const Shape shapes[] = {
                // Firebird: the parameter is described as CHAR(16) OCTETS,
                // which is reproducer A from the issue.
                {"SELECT UUID_TO_CHAR(?) FROM RDB$DATABASE", "binary parameter slot"},
                // Firebird: described as CHAR(36) in the connection charset,
                // reproducer B - the one that produced UTF-16 on the wire.
                {"SELECT UUID_TO_CHAR(CHAR_TO_UUID(?)) FROM RDB$DATABASE", "text parameter slot"},
                // SQL Server and friends: CAST to the engine's own UUID type.
                {"SELECT CAST(? AS UNIQUEIDENTIFIER)", "uniqueidentifier cast"},
            };

            std::string tried;
            std::string mismatches;
            int measured = 0;

            for (const auto& shape : shapes) {
                core::OdbcStatement stmt(conn_);
                if (!SQL_SUCCEEDED(SQLPrepare(
                        stmt.get_handle(),
                        reinterpret_cast<SQLCHAR*>(const_cast<char*>(shape.sql)),
                        SQL_NTS))) {
                    continue;   // this engine does not have that function
                }

                SQLLEN ind = sizeof(SQLGUID);
                SQLRETURN rc = SQLBindParameter(
                    stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_GUID, SQL_GUID,
                    36, 0, &guid, sizeof(guid), &ind);
                if (!SQL_SUCCEEDED(rc)) {
                    if (!tried.empty()) tried += "; ";
                    tried += std::string(shape.what) + ": SQLBindParameter(SQL_C_GUID) "
                             "refused with " +
                             first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                    continue;
                }

                rc = SQLExecute(stmt.get_handle());
                if (!SQL_SUCCEEDED(rc)) {
                    // A driver that refuses the conversion outright is at least
                    // telling the truth; one that corrupts it silently is the
                    // subject here. Recorded, not graded.
                    if (!tried.empty()) tried += "; ";
                    tried += std::string(shape.what) + ": execute failed with " +
                             first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "no SQLSTATE");
                    continue;
                }
                // Raw SQLFetch, not OdbcStatement::fetch(): that throws, and a
                // driver declining this conversion is a fact to record rather
                // than an exception to unwind the probe with.
                SQLRETURN fetch_rc = SQLFetch(stmt.get_handle());
                if (!SQL_SUCCEEDED(fetch_rc)) {
                    if (!tried.empty()) tried += "; ";
                    tried += std::string(shape.what) + ": fetch returned " +
                             std::to_string(fetch_rc) + " (" +
                             first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(),
                                            "no SQLSTATE") + ")";
                    continue;
                }

                std::string got = get_string(stmt.get_handle(), 1);
                std::transform(got.begin(), got.end(), got.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::toupper(c));
                               });
                while (!got.empty() && got.back() == ' ') got.pop_back();

                // Only a UUID-shaped answer is evidence about the conversion.
                // An engine with no UUID rendering may echo the expression back
                // as text - the mock returns the literal "UUID_TO_CHAR(?)" -
                // and reading that as a corrupted GUID would be a false
                // positive of exactly the kind R3 was. 8-4-4-4-12 of hex is the
                // shape; note the *wrong* answers this probe exists to catch are
                // themselves canonical-shaped, so the guard does not hide them.
                auto looks_like_uuid = [](const std::string& v) {
                    if (v.size() != 36) return false;
                    for (size_t i = 0; i < v.size(); ++i) {
                        const bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
                        if (dash) { if (v[i] != '-') return false; }
                        else if (!std::isxdigit(static_cast<unsigned char>(v[i]))) return false;
                    }
                    return true;
                };
                if (!looks_like_uuid(got)) {
                    if (!tried.empty()) tried += "; ";
                    tried += std::string(shape.what) +
                             ": the engine returned '" + got +
                             "', which is not a UUID rendering, so the "
                             "conversion cannot be judged from it";
                    continue;
                }

                ++measured;
                if (got != kCanonical) {
                    if (!mismatches.empty()) mismatches += "; ";
                    mismatches += std::string(shape.what) + ": got '" + got +
                                  "', expected '" + kCanonical + "'";
                }
            }

            std::ostringstream oss;
            oss << measured << " parameter shape(s) round-tripped a GUID";
            if (!tried.empty()) oss << "; not measurable here: " << tried;

            if (measured == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                oss << " — this engine has no statement that renders a bound "
                       "UUID back as text, so the input path cannot be checked";
                r.actual = oss.str();
                return;
            }
            if (!mismatches.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                oss << "; " << mismatches;
                r.suggestion =
                    "A SQL_C_GUID parameter must reach the server as the sixteen "
                    "bytes of the UUID, in canonical order. Two ways to get this "
                    "wrong, both silent: stringifying the GUID into a 16-byte "
                    "binary slot, which stores the ASCII of the first sixteen "
                    "characters; and picking a wide converter for a text slot "
                    "the driver maps to a wide C type, which puts UTF-16 on the "
                    "wire. Both return SQL_SUCCESS.";
            }
            r.actual = oss.str();
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
