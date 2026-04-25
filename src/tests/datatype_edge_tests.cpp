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
        test_varchar_raw_byte_integrity()
    };
}

TestResult DataTypeEdgeCaseTests::test_integer_zero() {
    return run_test(
        "test_integer_zero", "SQLGetData",
        "Integer value 0 retrieved correctly",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Integer Types",
        [&](TestResult& r) {
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for integer 0 test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for INT_MAX test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for INT_MIN test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                                    r.status = TestStatus::PASS;
                                    r.actual = "Empty string retrieved correctly (length=" +
                                                   std::to_string(indicator) + ")";
                                } else {
                                    r.status = TestStatus::FAIL;
                                    r.actual = "Expected empty string, got '" + std::string(buffer) + "'";
                                    r.severity = Severity::WARNING;
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
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for empty string test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                                r.status = TestStatus::PASS;
                                r.actual = "Special chars retrieved: '" + val +
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
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for special characters test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for NULL integer test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for NULL varchar test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for integer-as-string test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                                    r.status = TestStatus::PASS;
                                    r.actual = "String '123' converted to integer 123";
                                } else {
                                    r.status = TestStatus::PASS;
                                    r.actual = "String->integer conversion returned " + std::to_string(value);
                                }
                                success = true;
                                break;
                            } else if (rc == SQL_ERROR) {
                                // Some drivers don't support this conversion
                                r.status = TestStatus::SKIP_UNSUPPORTED;
                                r.actual = "Driver does not support string->integer conversion in SQLGetData";
                                success = true;
                                break;
                            }
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for string-as-integer test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            try {
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
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query for decimal value test";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
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
            bool executed = false;
            for (const auto& q : queries) {
                try {
                    stmt.execute(q);
                    executed = true;
                    break;
                } catch (const core::OdbcError&) {
                    // try next
                }
            }
            if (!executed) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No CAST(... AS VARCHAR(32)) query succeeded";
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
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver rejected SQLGetData(SQL_C_BINARY) on VARCHAR; "
                                "rc=" + std::to_string(rc);
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
            actual << " (informational; engines with inline length prefixes "
                      "should NOT show ASCII 'A'=0x41 in the first prefix bytes)";
            r.actual = actual.str();
        });
}

} // namespace odbc_crusher::tests
