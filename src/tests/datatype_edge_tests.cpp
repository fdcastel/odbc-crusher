#include "datatype_edge_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <climits>
#include <cstring>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <algorithm>   // A26: std::min was used without it

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
        test_decimal_values(),
        test_varchar_raw_byte_integrity(),
        test_null_vs_empty_distinction_varchar(),
        test_null_vs_zero_distinction_integer(),
        test_null_in_numeric_struct()
    };
}

TestResult DataTypeEdgeCaseTests::test_integer_zero() {
    return run_test(
        "test_integer_zero", "SQLGetData",
        "Integer value 0 retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Integer Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 0", "SELECT 0 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    SQLINTEGER value = -1;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        if (value == 0) {
                            r.status = TestStatus::PASS;
                            r.actual = "Integer 0 retrieved correctly";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected 0, got " + std::to_string(value);
                            r.severity = Severity::ERR;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for integer 0 test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_integer_max() {
    return run_test(
        "test_integer_max", "SQLGetData",
        "Large integer value retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Integer Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 2147483647", "SELECT 2147483647 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        if (value == 2147483647) {
                            r.status = TestStatus::PASS;
                            r.actual = "INT_MAX (2147483647) retrieved correctly";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected 2147483647, got " + std::to_string(value);
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for INT_MAX test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_integer_min() {
    return run_test(
        "test_integer_min", "SQLGetData",
        "Negative integer retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Integer Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT -2147483648", "SELECT -2147483648 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        if (value == (-2147483647 - 1)) {
                            r.status = TestStatus::PASS;
                            r.actual = "INT_MIN (-2147483648) retrieved correctly";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected -2147483648, got " + std::to_string(value);
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for INT_MIN test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_varchar_empty() {
    return run_test(
        "test_varchar_empty", "SQLGetData",
        "Empty string retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Character Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT ''", "SELECT '' FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    char buffer[256] = {0};
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        // A21: this used to accept SQL_NULL_DATA as an
                        // empty string, because `std::strlen(buffer)==0`
                        // is true for the untouched zero-initialised
                        // buffer a NULL fetch leaves behind. That is the
                        // exact NULL-vs-empty conflation the sibling
                        // test_null_vs_empty_distinction_varchar FAILs.
                        if (indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::FAIL;
                            r.actual = "SELECT '' reported SQL_NULL_DATA; "
                                       "an empty string is not NULL";
                            r.severity = Severity::ERR;
                            r.suggestion =
                                "The empty string and NULL are distinct values. "
                                "A driver conflating them corrupts every "
                                "nullable character column.";
                        } else if (indicator == 0 && buffer[0] == '\0') {
                            r.status = TestStatus::PASS;
                            r.actual = "Empty string retrieved correctly (length=0)";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected empty string, got '" +
                                       std::string(buffer) + "' (indicator=" +
                                       std::to_string(indicator) + ")";
                            r.severity = Severity::ERR;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for empty string test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_varchar_special_chars() {
    return run_test(
        "test_varchar_special_chars", "SQLGetData",
        "String with special characters retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Character Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // A21: the literal used to be 'a''b\"c\d'. The trailing
            // backslash is dialect-dependent — MySQL and ClickHouse treat
            // it as an escape introducer unless NO_BACKSLASH_ESCAPES is
            // set, so a correct driver there returns a different string
            // and the probe was asserting a MySQL bug into existence. The
            // two remaining specials, a doubled single quote and a double
            // quote, are portable across every engine in the matrix.
            const std::string expected = "a'b" + std::string(1, '\"') + "c";

            std::vector<std::string> queries = {
                "SELECT 'a''b\"c'",
                "SELECT 'a''b\"c' FROM RDB$DATABASE"
            };
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    char buffer[256] = {0};
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        // A21: the value was retrieved and never
                        // compared, so the probe passed whatever came
                        // back — including nothing at all.
                        const auto got = bounded_string(buffer, sizeof(buffer),
                                                        indicator);
                        if (got.value == expected) {
                            r.status = TestStatus::PASS;
                            r.actual = "Special chars round-tripped: '" +
                                       got.value + "'";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected '" + expected + "', got '" +
                                       got.value + "' (indicator=" +
                                       std::to_string(indicator) + ")";
                            r.severity = Severity::ERR;
                            r.suggestion =
                                "A doubled single quote is the SQL standard "
                                "escape and a double quote is an ordinary "
                                "character inside a string literal; both must "
                                "survive a round trip unchanged.";
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for special characters test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_null_integer() {
    return run_test(
        "test_null_integer", "SQLGetData",
        "NULL integer returns SQL_NULL_DATA indicator",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, NULL Data",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {
                "SELECT CAST(NULL AS INTEGER)",
                "SELECT NULL FROM RDB$DATABASE"
            };
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    SQLINTEGER value = 42;  // sentinel
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        if (indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::PASS;
                            r.actual = "NULL integer correctly returned SQL_NULL_DATA";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected SQL_NULL_DATA, got indicator=" +
                                           std::to_string(indicator);
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for NULL integer test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_null_varchar() {
    return run_test(
        "test_null_varchar", "SQLGetData",
        "NULL varchar returns SQL_NULL_DATA indicator",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, NULL Data",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {
                "SELECT CAST(NULL AS VARCHAR(50))",
                "SELECT NULL FROM RDB$DATABASE"
            };
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    char buffer[256];
                    std::memset(buffer, 'X', sizeof(buffer));
                    SQLLEN indicator = 0;
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                             buffer, sizeof(buffer), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        if (indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::PASS;
                            r.actual = "NULL varchar correctly returned SQL_NULL_DATA";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected SQL_NULL_DATA, got indicator=" +
                                           std::to_string(indicator);
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for NULL varchar test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_integer_as_string() {
    return run_test(
        "test_integer_as_string", "SQLGetData",
        "Integer retrieved as SQL_C_CHAR converts correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Type Conversion",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 42", "SELECT 42 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
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
                            r.status = TestStatus::PASS;
                            r.actual = "Integer 42 converted to string: '" + val + "'";
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Integer->string conversion unexpected: '" + val + "'";
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for integer-as-string test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_string_as_integer() {
    return run_test(
        "test_string_as_integer", "SQLGetData",
        "Numeric string retrieved as SQL_C_SLONG converts correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Type Conversion",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Use a string that contains a number
            std::vector<std::string> queries = {"SELECT '123'", "SELECT '123' FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;

                    // Retrieve string as SQL_C_SLONG (type conversion)
                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        // A21: both branches used to set PASS, so a
                        // driver returning 0 for '123' — the classic
                        // "atoi gave up" result — passed. SQL_CHAR to
                        // SQL_C_SLONG is a required Core conversion.
                        if (value == 123 && indicator != SQL_NULL_DATA) {
                            r.status = TestStatus::PASS;
                            r.actual = "String '123' converted to integer 123";
                        } else if (indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::FAIL;
                            r.actual = "String->integer conversion reported "
                                       "SQL_NULL_DATA for the literal '123'";
                            r.severity = Severity::ERR;
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "String->integer conversion returned " +
                                       std::to_string(value) + " (expected 123)";
                            r.severity = Severity::ERR;
                            r.suggestion =
                                "SQL_CHAR to SQL_C_SLONG is a required Core "
                                "conversion; a driver that cannot perform it "
                                "must return SQL_ERROR with 07006, not a "
                                "wrong value.";
                        }
                        success = true;
                        break;
                    } else if (rc == SQL_ERROR) {
                        // B3: SQL_CHAR to SQL_C_SLONG is a *required Core*
                        // conversion, so "some drivers don't support this"
                        // is only true when the driver says so. Anything
                        // other than IM001/HYC00/HY092/HY106 is a Core
                        // failure and must not be excused as a SKIP.
                        report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                       "SQLGetData(SQL_C_SLONG) on a character column");
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for string-as-integer test";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_decimal_values() {
    return run_test(
        "test_decimal_values", "SQLGetData",
        "Decimal/float value retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Numeric Types",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {
                "SELECT 3.14",
                "SELECT CAST(3.14 AS DECIMAL(5,2)) FROM RDB$DATABASE"
            };
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                if (stmt.fetch()) {
                    double value = 0.0;
                    SQLLEN indicator = 0;

                    SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_DOUBLE,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc)) {
                        // Check approximate equality
                        if (value > 3.0 && value < 3.2) {
                            r.status = TestStatus::PASS;
                            r.actual = "Decimal value retrieved: " + std::to_string(value);
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Expected ~3.14, got " + std::to_string(value);
                            r.severity = Severity::WARNING;
                        }
                        success = true;
                        break;
                    }
                }                } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for decimal value test";
            }
        });
}

// ── §1.6: VARCHAR raw-byte integrity probe ─────────────────────────────────
//
// IMPROVEMENT_PLAN.md §1.6. Some engines (Firebird, DB2 LOB) store VARCHAR
// with an inline length-prefix in the same buffer as the characters; a
// driver that miscomputes the data offset can write characters over the
// prefix. The string then looks fine via SQLGetData(SQL_C_CHAR) (the
// driver reads the same wrong offset back) but the raw bytes via
// SQL_C_BINARY reveal the corruption.
//
// This is purely informational because the bug shape is engine-specific
// and the "first byte must not be 'A'" assertion only holds for engines
// with inline prefixes. The test runs against any driver that supports
// `SELECT CAST('ABCDEFGH' AS VARCHAR(32))` and dumps the first 16 raw
// bytes (hex) into the test result so a driver developer can interpret.
TestResult DataTypeEdgeCaseTests::test_varchar_raw_byte_integrity() {
    return run_test(
        "test_varchar_raw_byte_integrity", "SQLGetData(SQL_C_BINARY)",
        "Read CAST('ABCDEFGH' AS VARCHAR(32)) as raw binary; report first 16 bytes",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData (SQL_C_BINARY)",
        [&](TestResult& r) {
            const std::vector<std::string> queries = {
                "SELECT CAST('ABCDEFGH' AS VARCHAR(32))",
                "SELECT CAST('ABCDEFGH' AS VARCHAR(32)) FROM RDB$DATABASE",
                "SELECT CAST('ABCDEFGH' AS VARCHAR(32)) FROM DUAL",
            };

            core::OdbcStatement stmt(conn_);
            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No CAST(... AS VARCHAR(32)) query succeeded";
                // C2: name each variant and its SQLSTATE.
                r.diagnostic = attempt.format_failures();
                return;
            }

            if (!stmt.fetch()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Query executed but no row fetched";
                return;
            }

            unsigned char raw[64] = {0};
            SQLLEN ind = 0;
            SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_BINARY,
                                      raw, sizeof(raw), &ind);
            if (!SQL_SUCCEEDED(rc)) {
                // B3
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetData(SQL_C_BINARY) on a VARCHAR column");
                return;
            }

            size_t to_dump = ind > 0 ? std::min<size_t>(static_cast<size_t>(ind), 16) : 0;
            std::ostringstream actual;
            actual << "indicator=" << ind << " first_bytes_hex=";
            for (size_t i = 0; i < to_dump; ++i) {
                if (i > 0) actual << ' ';
                actual << std::setfill('0') << std::setw(2) << std::hex
                       << static_cast<int>(raw[i]);
            }
            actual << std::dec << std::setfill(' ');
            actual << " (informational; engines with inline length prefixes "
                      "should NOT show ASCII 'A'=0x41 in the first prefix bytes)";
            r.actual = actual.str();
        });
}

// ── NULL-vs-non-NULL contrast probes (PORT plan §4.2) ──────────────────────
//
// The existing test_null_{integer,varchar} probes verify that a NULL value
// returns SQL_NULL_DATA, but drivers that conflate empty/zero with NULL pass
// those individually. The contrast is what catches the bug: round-trip both
// NULL and a non-NULL sentinel through the same column, and assert the two
// indicators differ.
//
// All three probes use INFRA-1 RoundTripTableGuard for cleanup.

namespace {

// Walks rows in PK order, reads VAL with the given C type, fills `inds`
// with the indicator from each row. Returns true on a clean fetch chain.
template <typename Buf>
bool collect_indicators(core::OdbcConnection& conn,
                        const std::string& table_name,
                        SQLSMALLINT c_type,
                        Buf& bufA, SQLLEN& indA,
                        Buf& bufB, SQLLEN& indB) {
    core::OdbcStatement sel(conn);
    sel.execute("SELECT VAL FROM " + table_name + " ORDER BY ID");
    SQLRETURN rc = SQLFetch(sel.get_handle());
    if (!SQL_SUCCEEDED(rc)) return false;
    rc = SQLGetData(sel.get_handle(), 1, c_type,
                    &bufA, sizeof(bufA), &indA);
    if (!SQL_SUCCEEDED(rc)) return false;
    rc = SQLFetch(sel.get_handle());
    if (!SQL_SUCCEEDED(rc)) return false;
    rc = SQLGetData(sel.get_handle(), 1, c_type,
                    &bufB, sizeof(bufB), &indB);
    return SQL_SUCCEEDED(rc);
}

} // namespace

TestResult DataTypeEdgeCaseTests::test_null_vs_empty_distinction_varchar() {
    return run_test(
        "test_null_vs_empty_distinction_varchar", "SQLGetData",
        "Empty string ('') and NULL VARCHAR produce different indicators "
        "(0 vs SQL_NULL_DATA)",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData: SQL_NULL_DATA distinguishes NULL from empty data",
        [&](TestResult& r) {
            const std::string table = "ODBC_TEST_NULL_VARCHAR";
            RoundTripTableGuard tbl(conn_, table, "VARCHAR(8)");
            if (!tbl.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create round-trip table: " + tbl.last_error();
                return;
            }

            try {
                core::OdbcStatement ins1(conn_);
                ins1.execute("INSERT INTO " + table + " (ID, VAL) VALUES (1, '')");
                core::OdbcStatement ins2(conn_);
                ins2.execute("INSERT INTO " + table + " (ID, VAL) VALUES (2, NULL)");
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = std::string("INSERT failed: ") + e.what();
                return;
            }

            char bufEmpty[16];
            char bufNull[16];
            std::memset(bufEmpty, 'X', sizeof(bufEmpty));
            std::memset(bufNull,  'X', sizeof(bufNull));
            SQLLEN indEmpty = 999;
            SQLLEN indNull  = 999;
            if (!collect_indicators(conn_, table, SQL_C_CHAR,
                                    bufEmpty, indEmpty,
                                    bufNull,  indNull)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Failed to fetch both rows from " + table;
                return;
            }

            std::ostringstream summary;
            summary << "row1 ('') indicator=" << indEmpty
                    << "; row2 (NULL) indicator=" << indNull;
            r.actual = summary.str();

            if (indEmpty == SQL_NULL_DATA && indNull == SQL_NULL_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver appears to treat empty string as NULL "
                               "(Oracle-style conflation). Empty VARCHAR must "
                               "return indicator=0, not SQL_NULL_DATA.";
                return;
            }
            if (indNull != SQL_NULL_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "NULL VARCHAR must produce SQL_NULL_DATA "
                               "(spec: ODBC 3.8 SQLGetData).";
                return;
            }
            if (indEmpty == SQL_NULL_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Empty VARCHAR must NOT produce SQL_NULL_DATA — "
                               "use indicator=0 for an empty string.";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_null_vs_zero_distinction_integer() {
    return run_test(
        "test_null_vs_zero_distinction_integer", "SQLGetData",
        "Integer 0 and NULL produce different indicators "
        "(sizeof(SQLINTEGER) vs SQL_NULL_DATA), buffer not zeroed for NULL",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData: indicator distinguishes NULL from value=0",
        [&](TestResult& r) {
            const std::string table = "ODBC_TEST_NULL_INT";
            RoundTripTableGuard tbl(conn_, table, "INTEGER");
            if (!tbl.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create round-trip table: " + tbl.last_error();
                return;
            }

            try {
                core::OdbcStatement ins1(conn_);
                ins1.execute("INSERT INTO " + table + " (ID, VAL) VALUES (1, 0)");
                core::OdbcStatement ins2(conn_);
                ins2.execute("INSERT INTO " + table + " (ID, VAL) VALUES (2, NULL)");
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = std::string("INSERT failed: ") + e.what();
                return;
            }

            // Sentinel value for the NULL row: a driver that zeroes the buffer
            // instead of setting SQL_NULL_DATA leaves us unable to distinguish
            // VAL=0 from NULL.
            // A26: 0xDEADBEEF exceeds INT32_MAX, so assigning it to a signed
            // SQLINTEGER was implementation-defined before C++20. The sentinel
            // only has to be a value the driver would never produce for this
            // column; the two's-complement bit pattern of 0xDEADBEEF is that,
            // written in a way the standard defines.
            constexpr SQLINTEGER kSentinel = static_cast<SQLINTEGER>(0xDEADBEEFu);
            SQLINTEGER bufZero = kSentinel;
            SQLINTEGER bufNull = kSentinel;
            SQLLEN indZero = 999, indNull = 999;
            if (!collect_indicators(conn_, table, SQL_C_SLONG,
                                    bufZero, indZero,
                                    bufNull, indNull)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Failed to fetch both rows from " + table;
                return;
            }

            std::ostringstream summary;
            summary << "row1 (0) indicator=" << indZero << " buf=" << bufZero
                    << "; row2 (NULL) indicator=" << indNull
                    << " buf=0x" << std::hex << bufNull << std::dec;
            r.actual = summary.str();

            if (indNull != SQL_NULL_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "NULL INTEGER must produce SQL_NULL_DATA. A "
                               "zeroed buffer with indicator != SQL_NULL_DATA "
                               "is indistinguishable from a real value of 0.";
                return;
            }
            if (indZero != static_cast<SQLLEN>(sizeof(SQLINTEGER)) || bufZero != 0) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Stored 0 must round-trip as buf=0 with "
                               "indicator=sizeof(SQLINTEGER), distinct from "
                               "the SQL_NULL_DATA path.";
            }
        });
}

TestResult DataTypeEdgeCaseTests::test_null_in_numeric_struct() {
    return run_test(
        "test_null_in_numeric_struct", "SQLGetData(SQL_C_NUMERIC)",
        "NULL DECIMAL produces SQL_NULL_DATA when read as SQL_C_NUMERIC",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData: SQL_NULL_DATA on the indicator for NULL numeric",
        [&](TestResult& r) {
            const std::string table = "ODBC_TEST_NULL_NUM";
            RoundTripTableGuard tbl(conn_, table, "DECIMAL(10, 2)");
            if (!tbl.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create round-trip table: " + tbl.last_error();
                return;
            }

            try {
                core::OdbcStatement ins(conn_);
                ins.execute("INSERT INTO " + table + " (ID, VAL) VALUES (1, NULL)");
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = std::string("INSERT failed: ") + e.what();
                return;
            }

            core::OdbcStatement sel(conn_);
            sel.execute("SELECT VAL FROM " + table);
            SQLRETURN rc = SQLFetch(sel.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLFetch failed";
                return;
            }

            // Configure ARD descriptor; some drivers reject SQL_C_NUMERIC
            // without it. The struct content for a NULL is unconstrained
            // by spec — only the indicator must be SQL_NULL_DATA.
            //
            // C11: this was the same four calls written out here with **every
            // return code discarded**, so a driver that refused
            // SQL_DESC_PRECISION was indistinguishable from one that accepted
            // it — and D29 found exactly that driver. The shared helper
            // checks each step, so a refusal is reported as a refusal rather
            // than blamed on the SQLGetData that follows.
            if (!set_numeric_descriptor(sel.get_handle(), 1, 10, 2)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Driver would not accept the SQL_C_NUMERIC ARD "
                           "descriptor (type/precision/scale), so a NULL "
                           "NUMERIC cannot be read back meaningfully";
                r.suggestion = "SQL_DESC_PRECISION and SQL_DESC_SCALE must be "
                               "settable on the ARD before SQLGetData with "
                               "SQL_C_NUMERIC";
                return;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0xAA, sizeof(ns));  // sentinel
            SQLLEN ind = 999;
            rc = SQLGetData(sel.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
            if (!SQL_SUCCEEDED(rc)) {
                // B3: SQL_C_NUMERIC is a required Core target type.
                report_failure(r, SQL_HANDLE_STMT, sel.get_handle(),
                               "SQLGetData(SQL_C_NUMERIC)");
                return;
            }

            std::ostringstream summary;
            summary << "indicator=" << ind
                    << " (expected SQL_NULL_DATA = " << SQL_NULL_DATA << ")";
            r.actual = summary.str();
            if (ind != SQL_NULL_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "NULL numeric must produce SQL_NULL_DATA; the "
                               "SQL_NUMERIC_STRUCT contents are unspecified for "
                               "NULL but the indicator is the contract.";
            }
        });
}

} // namespace odbc_crusher::tests
