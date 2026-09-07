#include "statement_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
#include <string>

namespace odbc_crusher::tests {

namespace {

// First SQLSTATE on a statement handle, or `fallback` when the driver posted
// nothing. A7 needs it to tell an optional-feature refusal (07009/HY109) from
// a real failure.
//
// Local to this file for now: C5 extracts the six copies of this shape across
// the tree into one helper, and B3 builds classify_failure() on top of it.
std::string first_sqlstate_or(SQLHSTMT handle, const std::string& fallback) {
    try {
        auto err = core::OdbcError::from_handle(SQL_HANDLE_STMT, handle, "");
        if (!err.diagnostics().empty()) return err.diagnostics()[0].sqlstate;
    } catch (...) {
    }
    return fallback;
}

}  // namespace

std::vector<TestResult> StatementTests::run() {
    return {
        test_simple_query(),
        test_prepared_statement(),
        test_parameter_binding(),
        test_result_fetching(),
        test_column_metadata(),
        test_statement_reuse(),
        test_multiple_result_sets(),

        // Phase 12: Column Binding Tests
        test_bind_col_integer(),
        test_bind_col_string(),
        test_fetch_bound_vs_getdata(),
        test_free_stmt_unbind(),

        // Phase 12: Row Count & Parameter Tests
        test_row_count(),
        test_num_params(),
        test_describe_param(),
        test_native_sql()
    };
}

TestResult StatementTests::test_simple_query() {
    return run_test(
        "test_simple_query", "SQLExecDirect",
        "Execute a simple SELECT query",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLExecDirect",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try different query patterns for different databases
            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",  // Firebird
                "SELECT 1",                     // MySQL, SQL Server
                "SELECT 1 FROM DUAL"            // Oracle
            };

            bool success = false;
            std::string successful_query;

            for (const auto& query : test_queries) {
                try {
                    stmt.execute(query);
                    success = true;
                    successful_query = query;
                    break;
                } catch (const core::OdbcError&) {
                    // Try next pattern
                    continue;
                }
            }

            if (success) {
                r.actual = "Successfully executed: " + successful_query;
                r.status = TestStatus::PASS;
            } else {
                r.actual = "Could not execute any simple query pattern";
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
            }
        });
}

TestResult StatementTests::test_prepared_statement() {
    return run_test(
        "test_prepared_statement", "SQLPrepare/SQLExecute",
        "Prepare and execute a statement",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, SQLExecute",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try different query patterns
            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",
                "SELECT 1",
                "SELECT 1 FROM DUAL"
            };

            bool success = false;
            for (const auto& query : test_queries) {
                try {
                    stmt.prepare(query);
                    stmt.execute_prepared();
                    success = true;
                    r.actual = "Successfully prepared and executed query";
                    break;
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not prepare/execute any query pattern";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible query pattern found for this driver";
            }
        });
}

TestResult StatementTests::test_parameter_binding() {
    return run_test(
        "test_parameter_binding", "SQLBindParameter",
        "Bind parameters to a prepared statement",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLBindParameter",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try parametrized queries
            // NOTE: "SELECT CAST(? AS INTEGER)" is preferred over "SELECT ?" because
            // some drivers (e.g. DuckDB) crash on SQLDescribeParam when the parameter
            // type cannot be inferred from a bare "SELECT ?".
            std::vector<std::string> test_queries = {
                "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",  // Firebird
                "SELECT CAST(? AS INTEGER)",                     // DuckDB, PostgreSQL, MySQL, SQL Server
                "SELECT CAST(? AS INTEGER) FROM DUAL",           // Oracle
                "SELECT ?"                                        // Fallback
            };

            bool success = false;
            SQLINTEGER param_value = 42;

            for (const auto& query : test_queries) {
                try {
                    stmt.prepare(query);

                    SQLRETURN ret = SQLBindParameter(
                        stmt.get_handle(),
                        1,                      // Parameter number
                        SQL_PARAM_INPUT,        // Input parameter
                        SQL_C_SLONG,            // C type
                        SQL_INTEGER,            // SQL type
                        0,                      // Column size
                        0,                      // Decimal digits
                        &param_value,           // Parameter value
                        0,                      // Buffer length
                        nullptr                 // StrLen_or_IndPtr
                    );

                    if (SQL_SUCCEEDED(ret)) {
                        stmt.execute_prepared();

                        // Try to fetch result
                        if (stmt.fetch()) {
                            SQLINTEGER result_value = 0;
                            SQLLEN indicator = 0;

                            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                            &result_value, sizeof(result_value), &indicator);

                            if (SQL_SUCCEEDED(ret) && result_value == 42) {
                                r.actual = "Parameter binding successful, retrieved value: 42";
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
                r.actual = "Parameter binding not tested (driver may not support)";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible parameterized query pattern found for this driver";
            }
        });
}

TestResult StatementTests::test_result_fetching() {
    return run_test(
        "test_result_fetching", "SQLFetch",
        "Fetch results from a query",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLFetch",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",
                "SELECT 1",
                "SELECT 1 FROM DUAL"
            };

            bool success = false;
            for (const auto& query : test_queries) {
                try {
                    stmt.execute(query);

                    if (stmt.fetch()) {
                        r.actual = "Successfully fetched result row";
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not fetch results";
                r.status = TestStatus::FAIL;
            }
        });
}

TestResult StatementTests::test_column_metadata() {
    return run_test(
        "test_column_metadata", "SQLNumResultCols/SQLDescribeCol",
        "Get column metadata from result set",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLNumResultCols, SQLDescribeCol",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",
                "SELECT 1",
                "SELECT 1 FROM DUAL"
            };

            bool success = false;
            for (const auto& query : test_queries) {
                try {
                    stmt.execute(query);

                    SQLSMALLINT num_cols = 0;
                    SQLRETURN ret = SQLNumResultCols(stmt.get_handle(), &num_cols);

                    if (SQL_SUCCEEDED(ret) && num_cols > 0) {
                        // Get column info
                        SQLCHAR col_name[256];
                        SQLSMALLINT name_len = 0;
                        SQLSMALLINT data_type = 0;
                        SQLULEN column_size = 0;
                        SQLSMALLINT decimal_digits = 0;
                        SQLSMALLINT nullable = 0;

                        ret = SQLDescribeCol(stmt.get_handle(), 1, col_name, sizeof(col_name),
                                            &name_len, &data_type, &column_size, &decimal_digits, &nullable);

                        if (SQL_SUCCEEDED(ret)) {
                            std::ostringstream oss;
                            oss << "Found " << num_cols << " column(s), type: " << data_type;
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
                r.actual = "Could not retrieve column metadata";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No compatible query pattern produced result column metadata";
            }
        });
}

TestResult StatementTests::test_statement_reuse() {
    return run_test(
        "test_statement_reuse", "SQLCloseCursor/Reexecute",
        "Reuse a statement handle multiple times",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCloseCursor",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",
                "SELECT 1"
            };

            bool success = false;
            for (const auto& query : test_queries) {
                try {
                    // Execute first time
                    stmt.execute(query);
                    stmt.fetch();

                    // Close cursor
                    stmt.close_cursor();

                    // Execute second time with same statement
                    stmt.execute(query);
                    stmt.fetch();

                    r.actual = "Statement reused successfully";
                    success = true;
                    break;
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "Could not reuse statement";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "Statement reuse test could not complete with available query patterns";
            }
        });
}

TestResult StatementTests::test_multiple_result_sets() {
    return run_test(
        "test_multiple_result_sets", "SQLMoreResults",
        "Check if driver supports multiple result sets",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLMoreResults",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Most simple queries don't have multiple result sets
            // This test just checks if SQLMoreResults is callable
            std::vector<std::string> test_queries = {
                "SELECT 1 FROM RDB$DATABASE",
                "SELECT 1"
            };

            bool success = false;
            for (const auto& query : test_queries) {
                try {
                    stmt.execute(query);
                    stmt.fetch();

                    // Check for more results
                    SQLRETURN ret = SQLMoreResults(stmt.get_handle());

                    // SQL_NO_DATA means no more result sets (expected)
                    // SQL_SUCCESS means there are more results
                    if (ret == SQL_NO_DATA || ret == SQL_SUCCESS) {
                        r.actual = "SQLMoreResults callable (returned " +
                                       std::string(ret == SQL_NO_DATA ? "SQL_NO_DATA" : "SQL_SUCCESS") + ")";
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.actual = "SQLMoreResults not tested";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "Could not execute a query to test SQLMoreResults";
            }
        });
}

// ============================================================
// Phase 12: Column Binding Tests
// ============================================================

TestResult StatementTests::test_bind_col_integer() {
    return run_test(
        "test_bind_col_integer", "SQLBindCol",
        "Bind an integer column and fetch via bound buffer",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLBindCol",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 42", "SELECT 42 FROM RDB$DATABASE"};
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.execute(query);

                    SQLINTEGER value = 0;
                    SQLLEN indicator = 0;

                    SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG,
                                             &value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc) && stmt.fetch()) {
                        // A6: this was `||`. The right operand is true for
                        // every non-NULL fetch, so the probe passed on any
                        // value at all and its FAIL branch was unreachable.
                        if (value == 42 && indicator != SQL_NULL_DATA) {
                            r.status = TestStatus::PASS;
                            r.actual = "Bound integer column, fetched value=" + std::to_string(value);
                            success = true;
                        } else if (indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::FAIL;
                            r.actual = "Bound column reported SQL_NULL_DATA for a "
                                       "non-NULL literal";
                            r.severity = Severity::ERR;
                        } else {
                            r.status = TestStatus::FAIL;
                            r.actual = "Fetched unexpected value=" + std::to_string(value) +
                                       " (expected 42)";
                            r.severity = Severity::ERR;
                        }
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not bind/fetch integer column";
            }
        });
}

TestResult StatementTests::test_bind_col_string() {
    return run_test(
        "test_bind_col_string", "SQLBindCol",
        "Bind a string column and fetch via bound buffer",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLBindCol",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 'hello'", "SELECT 'hello' FROM RDB$DATABASE"};
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.execute(query);

                    SQLCHAR value[256] = {0};
                    SQLLEN indicator = 0;

                    SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_CHAR,
                                             value, sizeof(value), &indicator);

                    if (SQL_SUCCEEDED(rc) && stmt.fetch()) {
                        std::string fetched(reinterpret_cast<char*>(value));
                        r.status = TestStatus::PASS;
                        r.actual = "Bound string column, fetched '" + fetched + "'";
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not bind/fetch string column";
            }
        });
}

TestResult StatementTests::test_fetch_bound_vs_getdata() {
    return run_test(
        "test_fetch_bound_vs_getdata", "SQLBindCol/SQLGetData",
        "Fetch same column via bound buffer and SQLGetData - values match",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindCol, SQLGetData",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 99", "SELECT 99 FROM RDB$DATABASE"};
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.execute(query);

                    // A7: the SQLBindCol return code used to be discarded.
                    // SQLBindCol is Core, so a failure here is the driver's,
                    // and continuing would have compared an unwritten buffer.
                    SQLINTEGER bound_value = 0;
                    SQLLEN indicator = 0;
                    SQLRETURN bind_rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG,
                                                   &bound_value, sizeof(bound_value),
                                                   &indicator);
                    if (!SQL_SUCCEEDED(bind_rc)) {
                        r.status = TestStatus::FAIL;
                        r.actual = "SQLBindCol failed (" +
                                   first_sqlstate_or(stmt.get_handle(), "no SQLSTATE") + ")";
                        r.severity = Severity::ERR;
                        r.suggestion = "SQLBindCol is a Core function; binding "
                                       "SQL_C_SLONG to an integer column must succeed.";
                        success = true;
                        break;
                    }

                    if (stmt.fetch()) {
                        // Also get via SQLGetData
                        SQLINTEGER getdata_value = 0;
                        SQLLEN getdata_ind = 0;
                        SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                                &getdata_value, sizeof(getdata_value), &getdata_ind);

                        // A7: this probe is named for comparing the two values
                        // and never compared them — both branches set PASS.
                        if (SQL_SUCCEEDED(rc)) {
                            if (bound_value == getdata_value &&
                                indicator == getdata_ind) {
                                r.status = TestStatus::PASS;
                                r.actual = "Bound=" + std::to_string(bound_value) +
                                           ", GetData=" + std::to_string(getdata_value) +
                                           " (match)";
                            } else {
                                r.status = TestStatus::FAIL;
                                r.actual = "Bound=" + std::to_string(bound_value) +
                                           " (ind=" + std::to_string(indicator) + ")" +
                                           " but GetData=" + std::to_string(getdata_value) +
                                           " (ind=" + std::to_string(getdata_ind) + ")";
                                r.severity = Severity::CRITICAL;
                                r.suggestion =
                                    "The same column read two ways in the same row "
                                    "must yield the same value. A mismatch means one "
                                    "of the two delivery paths is corrupting data.";
                            }
                        } else {
                            // SQLGetData on a *bound* column is optional: a
                            // driver may decline it unless SQL_GETDATA_EXTENSIONS
                            // advertises SQL_GD_BOUND. Only that specific
                            // combination is a SKIP; anything else is a failure.
                            const std::string state =
                                first_sqlstate_or(stmt.get_handle(), "");
                            SQLUINTEGER gd_ext = 0;
                            SQLGetInfo(conn_.get_handle(), SQL_GETDATA_EXTENSIONS,
                                       &gd_ext, sizeof(gd_ext), nullptr);
                            const bool advertises_gd_bound =
                                (gd_ext & SQL_GD_BOUND) != 0;
                            if ((state == "07009" || state == "HY109") &&
                                !advertises_gd_bound) {
                                r.status = TestStatus::SKIP_UNSUPPORTED;
                                r.actual = "SQLGetData on a bound column returned " +
                                           state + "; SQL_GETDATA_EXTENSIONS does not "
                                           "advertise SQL_GD_BOUND";
                            } else {
                                r.status = TestStatus::FAIL;
                                r.actual = "SQLGetData on a bound column failed (" +
                                           (state.empty() ? "no SQLSTATE" : state) + ")";
                                r.severity = Severity::ERR;
                                r.suggestion =
                                    "A driver that advertises SQL_GD_BOUND must allow "
                                    "SQLGetData on a bound column; otherwise it must "
                                    "report 07009 or HY109.";
                            }
                        }
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not compare bound fetch vs SQLGetData";
            }
        });
}

TestResult StatementTests::test_free_stmt_unbind() {
    return run_test(
        "test_free_stmt_unbind", "SQLFreeStmt(SQL_UNBIND)",
        "SQLFreeStmt(SQL_UNBIND) resets column bindings",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLFreeStmt",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Bind a column
            SQLINTEGER value = 0;
            SQLLEN indicator = 0;
            SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG, &value, sizeof(value), &indicator);

            if (SQL_SUCCEEDED(rc)) {
                // Unbind all columns
                rc = SQLFreeStmt(stmt.get_handle(), SQL_UNBIND);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLFreeStmt(SQL_UNBIND) succeeded";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFreeStmt(SQL_UNBIND) failed (rc=" + std::to_string(rc) + ")";
                    r.severity = Severity::WARNING;
                }
            } else {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLBindCol not supported";
            }
        });
}

// ============================================================
// Phase 12: Row Count & Parameter Tests
// ============================================================

TestResult StatementTests::test_row_count() {
    return run_test(
        "test_row_count", "SQLRowCount",
        "SQLRowCount returns row count after execution",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLRowCount",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.execute(query);

                    SQLLEN row_count = -1;
                    SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);

                    if (SQL_SUCCEEDED(rc)) {
                        r.status = TestStatus::PASS;
                        r.actual = "SQLRowCount returned " + std::to_string(row_count);
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQLRowCount";
            }
        });
}

TestResult StatementTests::test_num_params() {
    return run_test(
        "test_num_params", "SQLNumParams",
        "SQLNumParams returns parameter count after prepare",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLNumParams",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {
                "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",
                "SELECT CAST(? AS INTEGER)",
                "SELECT ?"
            };
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.prepare(query);

                    SQLSMALLINT num_params = -1;
                    SQLRETURN rc = SQLNumParams(stmt.get_handle(), &num_params);

                    if (SQL_SUCCEEDED(rc)) {
                        if (num_params == 1) {
                            r.status = TestStatus::PASS;
                            r.actual = "SQLNumParams correctly returned 1 for single-parameter query";
                        } else {
                            r.status = TestStatus::PASS;
                            r.actual = "SQLNumParams returned " + std::to_string(num_params);
                        }
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQLNumParams";
            }
        });
}

TestResult StatementTests::test_describe_param() {
    return run_test(
        "test_describe_param", "SQLDescribeParam",
        "SQLDescribeParam returns parameter type info",
        Severity::INFO, ConformanceLevel::LEVEL_1, "ODBC 3.8 SQLDescribeParam",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {
                "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE",
                "SELECT CAST(? AS INTEGER)",
                "SELECT ?"
            };
            bool success = false;

            for (const auto& query : queries) {
                try {
                    stmt.prepare(query);

                    SQLSMALLINT sql_type = 0;
                    SQLULEN param_size = 0;
                    SQLSMALLINT decimal_digits = 0;
                    SQLSMALLINT nullable = 0;

                    SQLRETURN rc = SQLDescribeParam(
                        stmt.get_handle(), 1,
                        &sql_type, &param_size, &decimal_digits, &nullable
                    );

                    if (SQL_SUCCEEDED(rc)) {
                        r.status = TestStatus::PASS;
                        r.actual = "Parameter 1: type=" + std::to_string(sql_type) +
                                       ", size=" + std::to_string(param_size) +
                                       ", nullable=" + std::to_string(nullable);
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success && r.status == TestStatus::PASS) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLDescribeParam not supported by this driver";
                r.suggestion = "SQLDescribeParam is a Level 1 conformance function";
            }
        });
}

TestResult StatementTests::test_native_sql() {
    return run_test(
        "test_native_sql", "SQLNativeSql",
        "SQLNativeSql translates ODBC SQL to native SQL",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLNativeSql",
        [&](TestResult& r) {
            SQLCHAR input[] = "SELECT 1";
            SQLCHAR output[512] = {0};
            SQLINTEGER output_len = 0;

            SQLRETURN rc = SQLNativeSql(
                conn_.get_handle(),
                input, SQL_NTS,
                output, sizeof(output), &output_len
            );

            if (SQL_SUCCEEDED(rc)) {
                std::string native_sql(reinterpret_cast<char*>(output));
                r.status = TestStatus::PASS;
                r.actual = "SQLNativeSql: '" + std::string(reinterpret_cast<char*>(input)) +
                               "' -> '" + native_sql + "'";
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLNativeSql not supported";
                r.suggestion = "SQLNativeSql is a Core conformance function per ODBC 3.x";
            }
        });
}

} // namespace odbc_crusher::tests
