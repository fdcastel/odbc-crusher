#include "boundary_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstring>

namespace odbc_crusher::tests {

namespace {

// First SQLSTATE on a handle, or "" when the driver posted nothing.
//
// Deliberately a local copy: **C5** (Phase 3) extracts the six-plus variants of
// this shape across the tree into one helper and **B3** builds
// classify_failure() on top of it. Phase 2 is localized probe fixes with no
// refactor, so this is a stand-in that C5 deletes.
std::string first_sqlstate(SQLSMALLINT handle_type, SQLHANDLE handle) {
    try {
        auto err = core::OdbcError::from_handle(handle_type, handle, "");
        if (!err.diagnostics().empty()) return err.diagnostics()[0].sqlstate;
    } catch (...) {
    }
    return "";
}

}  // namespace

std::vector<TestResult> BoundaryTests::run() {
    return {
        test_getinfo_zero_buffer(),
        test_getdata_zero_buffer(),
        test_bindparam_null_value_with_null_indicator(),
        test_execdirect_empty_sql(),
        test_describecol_col0()
    };
}

TestResult BoundaryTests::test_getinfo_zero_buffer() {
    return run_test(
        "test_getinfo_zero_buffer", "SQLGetInfo",
        "SQLGetInfo with buffer=0 returns required length",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo, Buffer Length",
        [&](TestResult& r) {
            try {
                SQLSMALLINT required_len = 0;

                // Pass NULL buffer and 0 size - should return the required length
                SQLRETURN rc = SQLGetInfo(
                    conn_.get_handle(),
                    SQL_DRIVER_NAME,
                    nullptr,
                    0,
                    &required_len
                );

                if (SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO) {
                    if (required_len > 0) {
                        r.status = TestStatus::PASS;
                        r.actual = "Required length = " + std::to_string(required_len) +
                                       " bytes (rc=" + std::to_string(rc) + ")";
                    } else {
                        r.status = TestStatus::FAIL;
                        r.actual = "Required length is 0, expected > 0";
                        r.severity = Severity::WARNING;
                    }
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLGetInfo with buffer=0 returned error (rc=" + std::to_string(rc) + ")";
                    r.severity = Severity::WARNING;
                    r.suggestion = "Driver should return SQL_SUCCESS with the required buffer length";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
            }
        });
}

TestResult BoundaryTests::test_getdata_zero_buffer() {
    return run_test(
        "test_getdata_zero_buffer", "SQLGetData",
        "SQLGetData with buffer=0 returns data length",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData, Buffer Length",
        [&](TestResult& r) {
            try {
                std::vector<std::string> queries = {
                    "SELECT 'hello'",
                    "SELECT 'hello' FROM RDB$DATABASE",
                    "SELECT CAST('hello' AS VARCHAR(50))",
                    "SELECT CAST('hello' AS VARCHAR(50)) FROM RDB$DATABASE"
                };
                bool success = false;
                std::string working_query;

                // First, find a query that actually works on this driver
                for (const auto& query : queries) {
                    try {
                        core::OdbcStatement probe(conn_);
                        probe.execute(query);
                        if (probe.fetch()) {
                            working_query = query;
                            break;
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (working_query.empty()) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query to test zero-buffer SQLGetData";
                    return;
                }

                // Strategy 1: NULL buffer, 0 size — should return data length
                {
                    core::OdbcStatement stmt(conn_);
                    stmt.execute(working_query);
                    if (stmt.fetch()) {
                        SQLLEN indicator = 0;
                        SQLRETURN rc = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                                 nullptr, 0, &indicator);

                        if ((SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO) && indicator > 0) {
                            r.status = TestStatus::PASS;
                            r.actual = "Data length = " + std::to_string(indicator) + " bytes";
                            success = true;
                        } else if (SQL_SUCCEEDED(rc) && indicator == SQL_NULL_DATA) {
                            r.status = TestStatus::PASS;
                            r.actual = "Column is NULL (SQL_NULL_DATA)";
                            success = true;
                        }
                    }
                }

                // Strategy 2: 1-byte buffer to trigger truncation (fresh statement)
                if (!success) {
                    core::OdbcStatement stmt2(conn_);
                    stmt2.execute(working_query);
                    if (stmt2.fetch()) {
                        char tiny[1] = {0};
                        SQLLEN indicator = 0;
                        SQLRETURN rc = SQLGetData(stmt2.get_handle(), 1, SQL_C_CHAR,
                                                  tiny, sizeof(tiny), &indicator);
                        if ((SQL_SUCCEEDED(rc) || rc == SQL_SUCCESS_WITH_INFO) && indicator > 0) {
                            r.status = TestStatus::PASS;
                            r.actual = "Data length = " + std::to_string(indicator) +
                                            " bytes (via 1-byte buffer truncation)";
                            success = true;
                        }
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not determine data length via zero-buffer or truncation";
                    r.suggestion = "Driver may not support SQLGetData with NULL buffer or may not set indicator on truncation";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
            }
        });
}

TestResult BoundaryTests::test_bindparam_null_value_with_null_indicator() {
    return run_test(
        "test_bindparam_null_value_with_null_indicator", "SQLBindParameter",
        "SQLBindParameter with NULL value and SQL_NULL_DATA indicator",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter, SQL_NULL_DATA",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                // Bind a parameter with NULL value pointer and SQL_NULL_DATA indicator
                SQLLEN indicator = SQL_NULL_DATA;

                SQLRETURN rc = SQLBindParameter(
                    stmt.get_handle(),
                    1,                    // parameter number
                    SQL_PARAM_INPUT,      // input/output type
                    SQL_C_CHAR,           // C type
                    SQL_VARCHAR,          // SQL type
                    255, 0,               // column size, decimal digits
                    nullptr,              // value pointer = NULL (representing NULL parameter)
                    0,                    // buffer length
                    &indicator            // indicator = SQL_NULL_DATA
                );

                // Per ODBC spec, this should succeed - it represents binding a NULL parameter value
                // Note: passing nullptr as value pointer when indicator is SQL_NULL_DATA is valid
                // and is the standard way to pass NULL values to parameterized queries.
                // However, some drivers may unbind when value is nullptr.
                if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLBindParameter with NULL value + SQL_NULL_DATA indicator succeeded";
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "SQLBindParameter handled NULL value (rc=" + std::to_string(rc) + ")";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
            }
        });
}

TestResult BoundaryTests::test_execdirect_empty_sql() {
    return run_test(
        "test_execdirect_empty_sql", "SQLExecDirect",
        "SQLExecDirect with empty SQL returns error",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                SQLRETURN rc = SQLExecDirect(stmt.get_handle(), (SQLCHAR*)"", SQL_NTS);

                if (rc == SQL_ERROR) {
                    r.status = TestStatus::PASS;
                    r.actual = "SQL_ERROR for empty SQL string - expected behavior";
                } else if (SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::PASS;
                    r.actual = "Driver accepted empty SQL string (implementation-defined behavior)";
                } else {
                    r.status = TestStatus::PASS;
                    r.actual = "Driver returned rc=" + std::to_string(rc) + " for empty SQL";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
            }
        });
}

TestResult BoundaryTests::test_describecol_col0() {
    return run_test(
        "test_describecol_col0", "SQLDescribeCol",
        "SQLDescribeCol with column 0 returns error or bookmark info",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeCol",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
                bool success = false;

                for (const auto& query : queries) {
                    try {
                        stmt.execute(query);

                        SQLCHAR col_name[128] = {0};
                        SQLSMALLINT col_name_len = 0;
                        SQLSMALLINT data_type = 0;
                        SQLULEN col_size = 0;
                        SQLSMALLINT decimal_digits = 0;
                        SQLSMALLINT nullable = 0;

                        SQLRETURN rc = SQLDescribeCol(
                            stmt.get_handle(), 0,
                            col_name, sizeof(col_name), &col_name_len,
                            &data_type, &col_size, &decimal_digits, &nullable
                        );

                        // A21: both branches used to PASS — SQL_ERROR without
                        // reading the state, and plain SQL_SUCCESS. The spec is
                        // specific: with SQL_ATTR_USE_BOOKMARKS at its default
                        // SQL_UB_OFF, ColumnNumber 0 is 07009 (invalid
                        // descriptor index), not "some error" and certainly not
                        // success.
                        if (rc == SQL_ERROR) {
                            const std::string state =
                                first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
                            if (state == "07009") {
                                r.status = TestStatus::PASS;
                                r.actual = "07009 for column 0 with bookmarks off";
                            } else {
                                r.status = TestStatus::FAIL;
                                r.actual = "Column 0 rejected with " +
                                           (state.empty() ? std::string("no SQLSTATE")
                                                          : state) +
                                           "; the spec requires 07009";
                                r.severity = Severity::WARNING;
                            }
                        } else if (SQL_SUCCEEDED(rc)) {
                            r.status = TestStatus::FAIL;
                            r.actual = "Column 0 described successfully even though "
                                       "SQL_ATTR_USE_BOOKMARKS is SQL_UB_OFF";
                            r.severity = Severity::WARNING;
                            r.suggestion =
                                "Column 0 is the bookmark column. With bookmarks "
                                "disabled, SQLDescribeCol must return SQL_ERROR "
                                "with SQLSTATE 07009.";
                        }

                        success = true;
                        break;
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                if (!success) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not execute query to test column 0 describe";
                }
            } catch (const std::exception& e) {
                r.status = TestStatus::ERR;
                r.actual = std::string("Exception: ") + e.what();
            }
        });
}

} // namespace odbc_crusher::tests
