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

            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                const std::string& query = attempt.query;
                success = true;
                successful_query = query;
                break;            } while (false);

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
            auto attempt = prepare_first_working(stmt, test_queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                stmt.execute_prepared();
                success = true;
                r.actual = "Successfully prepared and executed query";
                break;            } while (false);

            if (!success) {
                // B1: was SKIP_INCONCLUSIVE, which made this probe unable to
                // fail. SQLPrepare + SQLExecute is Core, and the three
                // variants cover the dialects this tool targets - a driver
                // that runs none of them cannot run a prepared statement at
                // all.
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "No variant of SELECT 1 could be prepared and "
                           "executed";
                r.suggestion =
                    "SQLPrepare and SQLExecute are Core. The diagnostic lists "
                    "what each dialect variant returned.";
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

            SQLINTEGER param_value = 42;

            // C2/A1. Whether a dialect variant works is not decided by the
            // prepare alone: the bind or the execute can be the step this
            // engine does not support. try_first_working() runs all three and
            // rejects the variant only if one of them throws — which is
            // exactly the rule A1 asks for, "advance only when the call
            // failed", and what the old loop got right by accident and the
            // first migration attempt got wrong by stopping at the prepare.
            auto attempt = try_first_working(test_queries, [&](const std::string& q) {
                stmt.prepare(q);
                SQLRETURN bind_rc = SQLBindParameter(
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
                core::check_odbc_result(bind_rc, SQL_HANDLE_STMT, stmt.get_handle(),
                                        "SQLBindParameter");
                stmt.execute_prepared();
            });

            if (!attempt) {
                r.actual = "No parameterised query pattern this driver accepts";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "None of the tested dialect variants prepared, bound "
                               "and executed.";
                // C2: name each variant and what it failed with.
                r.diagnostic = attempt.format_failures();
                return;
            }

            // A1: it prepared, bound and executed. From here the probe is
            // conclusive — a missing row or a wrong value is the driver's
            // answer, not a reason to try the next dialect and report
            // SKIP_INCONCLUSIVE.
            if (!stmt.fetch()) {
                r.status = TestStatus::FAIL;
                r.actual = attempt.query + " executed but returned no row";
                r.severity = Severity::ERR;
                return;
            }

            SQLINTEGER result_value = 0;
            SQLLEN indicator = 0;
            SQLRETURN ret = SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                       &result_value, sizeof(result_value), &indicator);
            if (SQL_SUCCEEDED(ret) && result_value == 42 &&
                indicator != SQL_NULL_DATA) {
                r.actual = "Parameter binding successful, retrieved value: 42";
            } else if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData failed with " +
                           first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(),
                                          "no SQLSTATE");
                r.severity = Severity::ERR;
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "Bound parameter 42 came back as " +
                           std::to_string(result_value) + " (indicator=" +
                           std::to_string(indicator) + ")";
                r.severity = Severity::CRITICAL;
                r.suggestion = "A value bound as an input parameter must survive the "
                               "round trip unchanged. This is the shape of the "
                               "Firebird #161 silent-corruption bug.";
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
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                if (stmt.fetch()) {
                    r.actual = "Successfully fetched result row";
                    success = true;
                    break;
                }            } while (false);

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
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

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
                }            } while (false);

            if (!success) {
                // B1: SQLNumResultCols and SQLDescribeCol are Core. An
                // application cannot read a result set without them, so
                // failing both is a finding, not an inconclusive result.
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "SQLNumResultCols/SQLDescribeCol produced no column "
                           "metadata for any query variant";
                r.suggestion =
                    "Both are Core. Without them an application cannot "
                    "discover the shape of a result set.";
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
                // B1: SQLCloseCursor followed by a re-execute on the same
                // handle is Core, and it is the shape every bulk consumer
                // uses. A driver that cannot do it forces a fresh handle per
                // statement, which is a real and reportable limitation.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Could not execute, close the cursor, and re-execute "
                           "on the same statement handle";
                r.suggestion =
                    "SQLCloseCursor is Core. If the driver needs a fresh "
                    "handle between executes, say so with a SQLSTATE rather "
                    "than failing the second execute.";
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
            auto attempt = execute_first_working(stmt, test_queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.
                stmt.fetch();

                // Check for more results
                SQLRETURN ret = SQLMoreResults(stmt.get_handle());

                // SQL_NO_DATA means no more result sets (expected)
                // SQL_SUCCESS means there are more results
                // B1: this accepted SQL_NO_DATA *and* SQL_SUCCESS as a pass,
                // so it asserted only that the function was callable. The
                // query is a single SELECT with one result set, so SQL_NO_DATA
                // is the only correct answer - SQL_SUCCESS claims a second
                // result set that does not exist, and an application looping
                // on SQLMoreResults would then read a stale or empty one.
                if (ret == SQL_NO_DATA) {
                    r.actual = "SQLMoreResults returned SQL_NO_DATA after the "
                               "single result set";
                    success = true;
                    break;
                }
                if (ret == SQL_SUCCESS) {
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::ERR;
                    r.actual = "SQLMoreResults returned SQL_SUCCESS after a "
                               "single-result-set query, claiming another "
                               "result set exists";
                    r.suggestion =
                        "A query with one result set must yield SQL_NO_DATA. "
                        "An application driving a loop on SQLMoreResults will "
                        "otherwise read a result set that is not there.";
                    success = true;
                    break;
                }
                // Anything else: let B3 read the SQLSTATE and decide whether
                // this driver is declining an optional function or failing.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLMoreResults");
                success = true;
                break;
            } while (false);

            if (!success) {
                // B1: reaching here means no query variant executed, which is
                // a Core failure and not a reason to excuse SQLMoreResults.
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "No query variant executed, so SQLMoreResults could "
                           "not be reached";
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
            // A26: the bound buffers are declared before the statement so
            // that they outlive it. They used to live inside the loop body,
            // so a `break` left the statement handle holding a binding to a
            // dead stack slot until the OdbcStatement destructor ran — and
            // destruction order made hoisting them below `stmt` no better.
            SQLINTEGER value = 0;
            SQLLEN indicator = 0;
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
                }            } while (false);

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
            // A26: the bound buffers are declared before the statement so
            // that they outlive it. They used to live inside the loop body,
            // so a `break` left the statement handle holding a binding to a
            // dead stack slot until the OdbcStatement destructor ran — and
            // destruction order made hoisting them below `stmt` no better.
            SQLCHAR value[256] = {0};
            SQLLEN indicator = 0;
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 'hello'", "SELECT 'hello' FROM RDB$DATABASE"};
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

                SQLRETURN rc = SQLBindCol(stmt.get_handle(), 1, SQL_C_CHAR,
                                         value, sizeof(value), &indicator);

                if (SQL_SUCCEEDED(rc) && stmt.fetch()) {
                    // D68: `indicator` is what SQLBindCol was given; use it.
                    std::string fetched =
                        bounded_string(reinterpret_cast<char*>(value),
                                       sizeof(value), indicator).value;
                    r.status = TestStatus::PASS;
                    r.actual = "Bound string column, fetched '" + fetched + "'";
                    success = true;
                    break;
                }            } while (false);

            if (!success && r.status == TestStatus::PASS) {
                // B1: SQLBindCol with SQL_C_CHAR is Core and is how most
                // applications read a result set. Failing it is a finding.
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = "Could not bind a character column with SQLBindCol "
                           "and fetch it";
                r.suggestion =
                    "SQLBindCol is Core. An application that binds columns "
                    "rather than calling SQLGetData per column cannot read "
                    "anything from this driver.";
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
            // A26: the bound buffers are declared before the statement so
            // that they outlive it. They used to live inside the loop body,
            // so a `break` left the statement handle holding a binding to a
            // dead stack slot until the OdbcStatement destructor ran — and
            // destruction order made hoisting them below `stmt` no better.
            SQLINTEGER bound_value = 0;
            SQLLEN indicator = 0;
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 99", "SELECT 99 FROM RDB$DATABASE"};
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

                // A7: the SQLBindCol return code used to be discarded.
                // SQLBindCol is Core, so a failure here is the driver's,
                // and continuing would have compared an unwritten buffer.
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
                }            } while (false);

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

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                SQLLEN row_count = -1;
                SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);

                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLRowCount returned " + std::to_string(row_count);
                    success = true;
                    break;
                }            } while (false);

            if (!success && r.status == TestStatus::PASS) {
                // B1: SQLRowCount is Core. The *value* is driver-defined for
                // a SELECT - which is why the passing branch above only
                // records it - but the call itself must succeed.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLRowCount failed after a successful SELECT";
                r.suggestion =
                    "SQLRowCount is Core and must succeed after an executed "
                    "statement, even where the count it reports for a SELECT "
                    "is left to the driver.";
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

            auto attempt = prepare_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                SQLSMALLINT num_params = -1;
                SQLRETURN rc = SQLNumParams(stmt.get_handle(), &num_params);

                if (SQL_SUCCEEDED(rc)) {
                    if (num_params == 1) {
                        r.status = TestStatus::PASS;
                        r.actual = "SQLNumParams correctly returned 1 for single-parameter query";
                    } else {
                        // B1: this was a second PASS, so the probe reported
                        // success whatever number came back - which is the
                        // one thing it exists to check. The statement has
                        // exactly one parameter marker.
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::ERR;
                        r.actual = "SQLNumParams returned " +
                                   std::to_string(num_params) +
                                   " for a statement with one parameter marker";
                        r.suggestion =
                            "SQLNumParams must report the number of parameter "
                            "markers in the prepared statement. A wrong count "
                            "misleads any application that binds by position.";
                    }
                    success = true;
                    break;
                }            } while (false);

            if (!success && r.status == TestStatus::PASS) {
                // B1: SQLNumParams is Core.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLNumParams failed on a prepared statement";
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

            auto attempt = prepare_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

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
                }            } while (false);

            if (!success && r.status == TestStatus::PASS) {
                // B1: SQLDescribeParam really is optional, so a skip can be
                // right - but it has to be justified by the driver saying so,
                // not by the call having failed for any reason at all. B3
                // reads the SQLSTATE and decides.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLDescribeParam");
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
                // D68: SQLNativeSql reports its length in `output_len`.
                // Reading to a terminator, the probe reported
                // `'SELECT 1' -> 'SELECT 1X'` against a driver whose
                // translation was correct.
                std::string native_sql =
                    bounded_string(reinterpret_cast<char*>(output),
                                   sizeof(output), output_len).value;
                r.status = TestStatus::PASS;
                r.actual = "SQLNativeSql: '" + std::string(reinterpret_cast<char*>(input)) +
                               "' -> '" + native_sql + "'";
            } else {
                // B1: the suggestion said "SQLNativeSql is a Core conformance
                // function" and the status said "not supported". Both cannot
                // be right. B3 reads the SQLSTATE: a driver that answers
                // HYC00 or IM001 is declining and still skips, and anything
                // else is the Core failure the suggestion describes.
                report_failure(r, SQL_HANDLE_DBC, conn_.get_handle(),
                               "SQLNativeSql");
            }
        });
}

} // namespace odbc_crusher::tests
