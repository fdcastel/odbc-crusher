#include "param_binding_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> ParameterBindingTests::run() {
    std::vector<TestResult> results;

    results.push_back(test_bindparam_wchar_input());
    results.push_back(test_bindparam_null_indicator());
    results.push_back(test_param_bound_value_reread_on_execute());
    results.push_back(test_bindparam_tinyint_to_varchar_roundtrip());
    results.push_back(test_bindparam_short_to_varchar_roundtrip());
    results.push_back(test_bindparam_int_to_varchar_roundtrip());
    results.push_back(test_bindparam_bigint_to_varchar_roundtrip());
    results.push_back(test_bindparam_float_to_varchar_roundtrip());
    results.push_back(test_bindparam_double_to_varchar_roundtrip());
    results.push_back(test_bindparam_double_to_varchar_fractional_roundtrip());
    results.push_back(test_bindparam_int_to_char_roundtrip());
    results.push_back(test_bindparam_int_to_wvarchar_roundtrip());
    // A20: the cells prior work left out.
    results.push_back(test_bindparam_utinyint_to_varchar_roundtrip());
    results.push_back(test_bindparam_ushort_to_varchar_roundtrip());
    results.push_back(test_bindparam_ulong_to_varchar_roundtrip());
    results.push_back(test_bindparam_ubigint_to_varchar_roundtrip());
    results.push_back(test_bindparam_bigint_to_char_roundtrip());
    results.push_back(test_bindparam_double_to_char_roundtrip());
    results.push_back(test_bindparam_bigint_to_wvarchar_roundtrip());
    results.push_back(test_sqldescribeparam_varchar());
    results.push_back(test_sqldescribeparam_integer());
    results.push_back(test_sqldescribeparam_decimal());
    results.push_back(test_sqlrowcount_after_insert());
    results.push_back(test_sqlrowcount_after_update());
    results.push_back(test_sqlrowcount_after_delete());
    results.push_back(test_sqlrowcount_after_execute_procedure());
    results.push_back(test_param_rebind_per_row_row_count());
    results.push_back(test_param_bind_once_execute_many_row_count());
    results.push_back(test_param_bind_once_execute_many_endtran());
    results.push_back(test_param_reexecute_requires_close());
    results.push_back(test_param_batch_then_single_row_tail());

    return results;
}

// ── Round-trip test table lifecycle ─────────────────────────────────────────
//
// Keep this table distinct from ODBC_TEST_ARRAY so the two categories don't
// interfere. Follows the same CREATE-first / DROP-and-retry pattern as
// ArrayParamTests to survive Firebird's "DDL failure invalidates the txn" rule
// (see PROJECT_PLAN.md lesson 15).

bool ParameterBindingTests::create_roundtrip_table(
    const std::string& table_name,
    const std::string& val_ddl)
{
    // C4 - see TransactionTests::create_test_table. This copy was 63 lines
    // and, unlike the other two, had no reuse probe at all: a run without
    // CREATE TABLE privilege could not use these probes even when the table
    // was already there. Adopting the guard adds that.
    //
    // Keyed by name because this file uses several tables (ODBC_TEST_ROUNDTRIP
    // and the per-type tables the round-trip matrix builds), and a probe may
    // hold one open while creating another.
    tables_.erase(table_name);
    auto [it, inserted] = tables_.try_emplace(
        table_name, conn_, table_name, val_ddl);
    (void)inserted;
    if (!it->second.ok()) {
        last_ddl_error_ = it->second.last_error();
        tables_.erase(it);
        return false;
    }
    return true;
}

void ParameterBindingTests::drop_roundtrip_table(const std::string& table_name) {
    tables_.erase(table_name);   // C4
}

TestResult ParameterBindingTests::test_bindparam_wchar_input() {
    return run_test(
        "test_bindparam_wchar_input", "SQLBindParameter",
        "SQLBindParameter with SQL_C_WCHAR input type accepts Unicode data",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_C_WCHAR for Unicode parameter data",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        
        // Prepare a parameterized query — try multiple patterns
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        // D78: W first, then ANSI. The fallback is for a driver exporting
        // both widths with broken W conversion; against a Unicode-only
        // driver the manager converts the ANSI attempt back into the same
        // W entry point, so it cannot help. One copy, not six.
        SQLRETURN ret = prepare_w_then_ansi(stmt, queries);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare parameterized query";
            return;
        }
        
        // Bind a Unicode string parameter
        auto param_wbuf = to_sqlwchar("TestCustomer"); SQLWCHAR* param_value = param_wbuf.data();
        SQLLEN param_len = SQL_NTS;
        
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
            50, 0,
            param_value, (param_wbuf.size() * sizeof(SQLWCHAR)), &param_len);
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(ret)) {
            actual << "SQLBindParameter with SQL_C_WCHAR succeeded";
            
            // Try to execute
            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
            if (SQL_SUCCEEDED(exec_ret)) {
                actual << "; execute succeeded";
            } else {
                actual << "; execute returned " << exec_ret;
            }
        } else {
            // B1: "may not support" was a guess. SQL_C_WCHAR is a Core C type
            // and every Unicode driver binds it; B3 reads the SQLSTATE, so a
            // driver that answers HYC00 still skips and anything else is the
            // failure it is.
            actual << "SQLBindParameter with SQL_C_WCHAR returned " << ret;
            r.actual = actual.str();
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLBindParameter(SQL_C_WCHAR)");
            return;
        }
        r.actual = actual.str();
        });
}

TestResult ParameterBindingTests::test_bindparam_null_indicator() {
    return run_test(
        "test_bindparam_null_indicator", "SQLBindParameter",
        "SQLBindParameter with SQL_NULL_DATA indicator passes NULL to driver",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_NULL_DATA in StrLen_or_IndPtr for NULL",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        // D78: W first, then ANSI. The fallback is for a driver exporting
        // both widths with broken W conversion; against a Unicode-only
        // driver the manager converts the ANSI attempt back into the same
        // W entry point, so it cannot help. One copy, not six.
        SQLRETURN ret = prepare_w_then_ansi(stmt, queries);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare query for NULL parameter test";
            return;
        }
        
        // Bind with SQL_NULL_DATA indicator
        SQLLEN null_ind = SQL_NULL_DATA;
        
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            50, 0,
            nullptr, 0, &null_ind);
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(ret)) {
            actual << "SQLBindParameter with NULL indicator succeeded";
            
            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
            actual << "; execute returned " << exec_ret;
        } else {
            actual << "SQLBindParameter with NULL indicator returned " << ret;
            r.status = TestStatus::FAIL;
            r.suggestion = "Drivers must accept SQL_NULL_DATA as parameter indicator";
        }
        r.actual = actual.str();
        });
}

TestResult ParameterBindingTests::test_param_bound_value_reread_on_execute() {
    return run_test(
        // B8: named for a rebind it does not do. The body binds once,
        // mutates the bound variable, and executes again with no intervening
        // SQLBindParameter - which is the *bind-once* contract, and is what
        // test_param_bind_once_execute_many_endtran covers at scale. Renamed
        // to what it tests; the parameter-rebinding shape has its own probe
        // in test_param_rebind_per_row_row_count.
        "test_param_bound_value_reread_on_execute", "SQLBindParameter",
        "Bind, execute, rebind with new value, execute again",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: the driver re-reads the bound buffer at "
        "each execute",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS INTEGER)",
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE"
        };
        // D78: W first, then ANSI. The fallback is for a driver exporting
        // both widths with broken W conversion; against a Unicode-only
        // driver the manager converts the ANSI attempt back into the same
        // W entry point, so it cannot help. One copy, not six.
        SQLRETURN ret = prepare_w_then_ansi(stmt, queries);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare query for rebind test";
            return;
        }
        
        // First bind and execute
        SQLINTEGER param_val = 1;
        SQLLEN ind = 0;
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, &param_val, 0, &ind);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not bind first parameter";
            return;
        }
        
        SQLRETURN exec1 = SQLExecute(stmt.get_handle());
        
        // Close cursor if needed
        SQLCloseCursor(stmt.get_handle());
        
        // Rebind with different value and execute again
        param_val = 2;
        SQLRETURN exec2 = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "First execute: " << exec1 << "; Rebind + second execute: " << exec2;
        r.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.suggestion = "Neither execution succeeded; driver may not support parameterized queries";
        } else if (SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            r.status = TestStatus::FAIL;
            r.suggestion = "Second execute after rebind should succeed if first did";
        }
        });
}

// ── §1.1 numeric-C → character-SQL round-trip helpers ────────────────────
//
// These two templated helpers carry the boilerplate that every cell of the
// matrix needs: CREATE TABLE, PREPARE INSERT, BIND ID + VAL, loop EXECUTE,
// COMMIT, verify_rows_persisted. The integer variant compares values
// exactly via `std::to_string`; the float variant parses each row as a
// double and compares numerically (drivers format `1.0f` as "1", "1.0",
// "1.000000", "1e0", etc — exact-string compare would generate noise).

namespace {

// CHAR columns pad with spaces to the declared width; right-trim before
// comparing the round-trip value to `std::to_string(i)`.
std::string rtrim_spaces(const std::string& s) {
    auto end = s.find_last_not_of(' ');
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

}  // namespace

template <typename CType>
TestResult ParameterBindingTests::run_int_to_string_roundtrip(
    const std::string& test_name,
    SQLSMALLINT c_type_id,
    const std::string& c_type_name,
    SQLSMALLINT sql_type_id,
    const std::string& sql_type_name,
    const std::string& table_name,
    const std::string& column_ddl,
    SQLULEN col_size,
    bool right_trim_for_compare)
{
    return run_test(
        test_name,
        "SQLBindParameter",
        "INSERT 10 rows binding " + c_type_name + " into " + sql_type_name +
        " column; read back ORDER BY id; values match std::to_string(i) for i=1..10",
        Severity::CRITICAL,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: numeric C → character SQL conversion, Appendix D",
        [&](TestResult& result) {
            if (!create_roundtrip_table(table_name, column_ddl)) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE " + table_name +
                                " (ID INTEGER, VAL " + column_ddl + ")";
                result.diagnostic = last_ddl_error_;
                result.suggestion =
                    "Driver may not support `" + column_ddl + "` columns (some "
                    "engines spell WVARCHAR as NVARCHAR or NATIONAL VARCHAR). Skip, "
                    "don't fail — the test cannot exercise this cell on this driver.";
                return;
            }

            constexpr int kRowCount = 10;
            bool insert_phase_ok = true;
            int insert_errors = 0;
            std::string first_insert_error;
            const std::string insert_sql = "INSERT INTO " + table_name +
                                           " (ID, VAL) VALUES (?, ?)";

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)insert_sql.c_str(),
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare INSERT returned " + std::to_string(rc);
                    drop_roundtrip_table(table_name);
                    return;
                }

                SQLINTEGER id_param = 0;
                CType val_param = 0;
                SQLLEN id_ind = 0;
                SQLLEN val_ind = 0;

                rc = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                      SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                      &id_param, 0, &id_ind);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLBindParameter(id) returned " + std::to_string(rc);
                    drop_roundtrip_table(table_name);
                    return;
                }

                // The core bind under test: <CType> → <sql_type>. The driver must
                // convert the integer to a numeric string and store it in the
                // character column.
                rc = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                      c_type_id, sql_type_id, col_size, 0,
                                      &val_param, 0, &val_ind);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "SQLBindParameter(val, " + c_type_name + "→" +
                                    sql_type_name + ") returned " + std::to_string(rc);
                    result.suggestion =
                        "Driver rejected " + c_type_name + "→" + sql_type_name +
                        " conversion at bind time. Skip, don't fail.";
                    drop_roundtrip_table(table_name);
                    return;
                }

                for (int i = 1; i <= kRowCount; ++i) {
                    id_param = i;
                    val_param = static_cast<CType>(i);
                    SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                    if (!SQL_SUCCEEDED(exec_rc)) {
                        insert_phase_ok = false;
                        insert_errors++;
                        if (first_insert_error.empty()) {
                            first_insert_error = "SQLExecute row " + std::to_string(i) +
                                                 " returned " + std::to_string(exec_rc);
                        }
                    }
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = std::string("INSERT phase threw: ") + e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table(table_name);
                return;
            }

            if (!insert_phase_ok && insert_errors == kRowCount) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "All " + std::to_string(kRowCount) +
                                " SQLExecute calls failed: " + first_insert_error;
                drop_roundtrip_table(table_name);
                return;
            }

            // A22: keep the return code. A failed COMMIT used to be discarded,
            // and the missing rows were then blamed on the bind path.
            const CommitOutcome commit = commit_now();

            RowVerification v = verify_rows_persisted(
                table_name, "ID", "VAL", kRowCount);

            if (!v.ok) {
                result.status = TestStatus::FAIL;
                result.actual = "verify_rows_persisted failed: " + v.diagnostic +
                                " (count=" + std::to_string(v.actual_count) +
                                ", " + commit.summary + ")";
                result.suggestion = commit
                    ? "Rows did not persist after SQL_SUCCESS INSERTs — this is the "
                      "Firebird #161 silent-corruption shape. Check the driver's "
                      "numeric-C → character-SQL conversion on the bind path."
                    : "The COMMIT failed, so the rows are missing because the "
                      "transaction never committed — not because the driver lost "
                      "them on the bind path. Fix the commit failure first; this "
                      "probe cannot say anything about binding until it succeeds.";
            } else {
                std::string mismatches;
                for (int i = 0; i < kRowCount; ++i) {
                    std::string expected = std::to_string(i + 1);
                    // A19: a NULL is now distinguishable from '' and is a mismatch in
                    // its own right — the probe inserted a value, so a column that
                    // reads back NULL is the silent-drop shape this probe hunts.
                    if (!v.actual_values[i]) {
                        if (!mismatches.empty()) mismatches += ", ";
                        mismatches += "row " + std::to_string(i + 1) + ": expected '" +
                                      expected + "' got NULL";
                        continue;
                    }
                    const std::string& raw = *v.actual_values[i];
                    std::string actual = right_trim_for_compare ? rtrim_spaces(raw)
                                                                : raw;
                    if (actual != expected) {
                        if (!mismatches.empty()) mismatches += ", ";
                        mismatches += "row " + std::to_string(i + 1) + ": expected '" +
                                      expected + "' got '" + raw + "'";
                    }
                }
                if (mismatches.empty()) {
                    if (insert_phase_ok) {
                        result.actual = "All " + std::to_string(kRowCount) +
                                        " rows round-tripped correctly with " +
                                        c_type_name + " → " + sql_type_name;
                    } else {
                        result.status = TestStatus::FAIL;
                        result.severity = Severity::WARNING;
                        result.actual = "Round-trip succeeded but " +
                                        std::to_string(insert_errors) +
                                        " INSERTs reported errors (first: " +
                                        first_insert_error + ")";
                        result.suggestion =
                            "Driver returned errors during execute but data still landed. "
                            "Indicator handling or post-execute state may be inconsistent.";
                    }
                } else {
                    result.status = TestStatus::FAIL;
                    result.actual = "Round-trip value mismatch: " + mismatches;
                    result.suggestion =
                        "Driver converted " + c_type_name + " to " + sql_type_name +
                        " incorrectly — numeric-C → character-SQL conversion is broken.";
                }
            }

            drop_roundtrip_table(table_name);
        });
}

template <typename CType>
TestResult ParameterBindingTests::run_float_to_string_roundtrip(
    const std::string& test_name,
    SQLSMALLINT c_type_id,
    const std::string& c_type_name,
    SQLSMALLINT sql_type_id,
    const std::string& sql_type_name,
    const std::string& table_name,
    const std::string& column_ddl,
    SQLULEN col_size,
    double value_offset)
{
    return run_test(
        test_name,
        "SQLBindParameter",
        "INSERT 10 rows binding " + c_type_name + " into " + sql_type_name +
        " column; read back as text and parse to number; |back - i| < epsilon",
        Severity::CRITICAL,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: numeric C → character SQL conversion, Appendix D",
        [&](TestResult& result) {
            if (!create_roundtrip_table(table_name, column_ddl)) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE " + table_name;
                result.diagnostic = last_ddl_error_;
                return;
            }

            constexpr int kRowCount = 10;
            int insert_errors = 0;
            std::string first_insert_error;
            const std::string insert_sql = "INSERT INTO " + table_name +
                                           " (ID, VAL) VALUES (?, ?)";

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)insert_sql.c_str(),
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare INSERT returned " + std::to_string(rc);
                    drop_roundtrip_table(table_name);
                    return;
                }

                SQLINTEGER id_param = 0;
                CType val_param = 0;
                SQLLEN id_ind = 0;
                SQLLEN val_ind = 0;

                rc = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                      SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                      &id_param, 0, &id_ind);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLBindParameter(id) returned " + std::to_string(rc);
                    drop_roundtrip_table(table_name);
                    return;
                }

                rc = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                      c_type_id, sql_type_id, col_size, 0,
                                      &val_param, 0, &val_ind);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "SQLBindParameter(val, " + c_type_name + "→" +
                                    sql_type_name + ") returned " + std::to_string(rc);
                    result.suggestion =
                        "Driver rejected " + c_type_name + "→" + sql_type_name +
                        " conversion.";
                    drop_roundtrip_table(table_name);
                    return;
                }

                for (int i = 1; i <= kRowCount; ++i) {
                    id_param = i;
                    val_param = static_cast<CType>(static_cast<double>(i) + value_offset);
                    SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                    if (!SQL_SUCCEEDED(exec_rc)) {
                        insert_errors++;
                        if (first_insert_error.empty()) {
                            first_insert_error = "SQLExecute row " + std::to_string(i) +
                                                 " returned " + std::to_string(exec_rc);
                        }
                    }
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = std::string("INSERT phase threw: ") + e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table(table_name);
                return;
            }

            if (insert_errors == kRowCount) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "All " + std::to_string(kRowCount) +
                                " SQLExecute calls failed: " + first_insert_error;
                drop_roundtrip_table(table_name);
                return;
            }

            // A22: keep the return code. A failed COMMIT used to be discarded,
            // and the missing rows were then blamed on the bind path.
            const CommitOutcome commit = commit_now();

            RowVerification v = verify_rows_persisted(
                table_name, "ID", "VAL", kRowCount);

            if (!v.ok) {
                result.status = TestStatus::FAIL;
                result.actual = "verify_rows_persisted failed: " + v.diagnostic +
                                " (" + commit.summary + ")";
                result.suggestion = commit
                    ? "Rows did not persist — Firebird #161 silent-corruption shape. "
                      "Check driver's " + c_type_name + " → " + sql_type_name + " path."
                    : "The COMMIT failed, so the rows are missing because the "
                      "transaction never committed — not because the driver lost "
                      "them on the bind path. Fix the commit failure first; this "
                      "probe cannot say anything about binding until it succeeds.";
            } else {
                std::string mismatches;
                const double kEpsilon = 1e-3;
                for (int i = 0; i < kRowCount; ++i) {
                    double expected = static_cast<double>(i + 1) + value_offset;
                    // A19: NULL is no longer read as the empty string, which strtod
                    // parsed as a failure anyway — but now it is *reported* as NULL
                    // rather than as "got ''".
                    if (!v.actual_values[i]) {
                        if (!mismatches.empty()) mismatches += ", ";
                        mismatches += "row " + std::to_string(i + 1) +
                                      ": got NULL, expected ~" +
                                      std::to_string(expected);
                        continue;
                    }
                    const std::string& s = *v.actual_values[i];
                    char* end = nullptr;
                    double parsed = std::strtod(s.c_str(), &end);
                    bool parsed_ok = (end != s.c_str());
                    if (!parsed_ok || std::fabs(parsed - expected) > kEpsilon) {
                        if (!mismatches.empty()) mismatches += ", ";
                        mismatches += "row " + std::to_string(i + 1) + ": got '" + s +
                                      "', expected ~" + std::to_string(expected);
                    }
                }
                if (mismatches.empty()) {
                    result.actual = "All " + std::to_string(kRowCount) +
                                    " rows round-tripped numerically with " +
                                    c_type_name + " (formatting driver-defined)";
                } else {
                    result.status = TestStatus::FAIL;
                    result.actual = "Numeric round-trip mismatch: " + mismatches;
                    result.suggestion =
                        "Driver converted " + c_type_name + " to " + sql_type_name +
                        " incorrectly.";
                }
            }

            drop_roundtrip_table(table_name);
        });
}

TestResult ParameterBindingTests::test_bindparam_tinyint_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLSCHAR>(
        "test_bindparam_tinyint_to_varchar_roundtrip",
        SQL_C_STINYINT, "SQL_C_STINYINT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_short_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLSMALLINT>(
        "test_bindparam_short_to_varchar_roundtrip",
        SQL_C_SSHORT, "SQL_C_SSHORT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_int_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLINTEGER>(
        "test_bindparam_int_to_varchar_roundtrip",
        SQL_C_SLONG, "SQL_C_SLONG",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_bigint_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLBIGINT>(
        "test_bindparam_bigint_to_varchar_roundtrip",
        SQL_C_SBIGINT, "SQL_C_SBIGINT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_float_to_varchar_roundtrip() {
    return run_float_to_string_roundtrip<SQLREAL>(
        "test_bindparam_float_to_varchar_roundtrip",
        SQL_C_FLOAT, "SQL_C_FLOAT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(40)", 40);
}

TestResult ParameterBindingTests::test_bindparam_double_to_varchar_roundtrip() {
    return run_float_to_string_roundtrip<SQLDOUBLE>(
        "test_bindparam_double_to_varchar_roundtrip",
        SQL_C_DOUBLE, "SQL_C_DOUBLE",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(40)", 40);
}

// Fractional variant — inserts 1.5, 2.5, …, 10.5 instead of whole numbers
// so trunc-style driver bugs (DECIMAL→INTEGER coercion, lossy DOUBLE→string
// formatting) actually trip verify_rows_persisted. The whole-number variant
// above can't catch them: 1.0 trunc'd to 1.0 is still 1.0. The §5.2
// SilentCorruption=TruncateNumeric mode is exercised end-to-end by an e2e
// scenario keyed on this test's name.
TestResult ParameterBindingTests::test_bindparam_double_to_varchar_fractional_roundtrip() {
    return run_float_to_string_roundtrip<SQLDOUBLE>(
        "test_bindparam_double_to_varchar_fractional_roundtrip",
        SQL_C_DOUBLE, "SQL_C_DOUBLE",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP_FRAC", "VARCHAR(40)", 40,
        /*value_offset=*/0.5);
}

TestResult ParameterBindingTests::test_bindparam_int_to_char_roundtrip() {
    // CHAR pads with trailing spaces to declared width — right-trim before
    // comparing to `std::to_string(i)`. Uses a dedicated table so the test
    // doesn't interfere with the VARCHAR cells.
    return run_int_to_string_roundtrip<SQLINTEGER>(
        "test_bindparam_int_to_char_roundtrip",
        SQL_C_SLONG, "SQL_C_SLONG",
        SQL_CHAR, "SQL_CHAR",
        "ODBC_TEST_ROUNDTRIP_CHAR", "CHAR(20)", 20, true);
}

TestResult ParameterBindingTests::test_bindparam_int_to_wvarchar_roundtrip() {
    // NVARCHAR maps to SQL_WVARCHAR in the mock-driver DDL parser. Engines
    // that don't support NVARCHAR will SKIP_INCONCLUSIVE on CREATE TABLE.
    return run_int_to_string_roundtrip<SQLINTEGER>(
        "test_bindparam_int_to_wvarchar_roundtrip",
        SQL_C_SLONG, "SQL_C_SLONG",
        SQL_WVARCHAR, "SQL_WVARCHAR",
        "ODBC_TEST_ROUNDTRIP_WCHAR", "NVARCHAR(20)", 20, false);
}

// ── A20: the rest of the numeric-C -> character-SQL matrix ────────────────
//
// Unsigned C types first. SQL_C_UTINYINT through SQL_C_UBIGINT are separate
// conversions in the spec's table, and a driver that quietly routes them
// through the signed path gets small values right and large ones wrong - so
// these use the same 1..10 values as their signed twins, which proves the
// conversion is wired up at all, and leave the boundary values to the
// boundary category.

TestResult ParameterBindingTests::test_bindparam_utinyint_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLCHAR>(
        "test_bindparam_utinyint_to_varchar_roundtrip",
        SQL_C_UTINYINT, "SQL_C_UTINYINT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_ushort_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLUSMALLINT>(
        "test_bindparam_ushort_to_varchar_roundtrip",
        SQL_C_USHORT, "SQL_C_USHORT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_ulong_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLUINTEGER>(
        "test_bindparam_ulong_to_varchar_roundtrip",
        SQL_C_ULONG, "SQL_C_ULONG",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

TestResult ParameterBindingTests::test_bindparam_ubigint_to_varchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLUBIGINT>(
        "test_bindparam_ubigint_to_varchar_roundtrip",
        SQL_C_UBIGINT, "SQL_C_UBIGINT",
        SQL_VARCHAR, "SQL_VARCHAR",
        "ODBC_TEST_ROUNDTRIP", "VARCHAR(32)", 32, false);
}

// The CHAR and WVARCHAR axes had only SQL_C_SLONG, so a driver could get
// the 32-bit case right and the 64-bit or floating case wrong on those
// column types without anything noticing.

TestResult ParameterBindingTests::test_bindparam_bigint_to_char_roundtrip() {
    return run_int_to_string_roundtrip<SQLBIGINT>(
        "test_bindparam_bigint_to_char_roundtrip",
        SQL_C_SBIGINT, "SQL_C_SBIGINT",
        SQL_CHAR, "SQL_CHAR",
        "ODBC_TEST_ROUNDTRIP_CHAR", "CHAR(20)", 20, true);
}

TestResult ParameterBindingTests::test_bindparam_double_to_char_roundtrip() {
    return run_float_to_string_roundtrip<SQLDOUBLE>(
        "test_bindparam_double_to_char_roundtrip",
        SQL_C_DOUBLE, "SQL_C_DOUBLE",
        SQL_CHAR, "SQL_CHAR",
        "ODBC_TEST_ROUNDTRIP_CHAR", "CHAR(40)", 40,
        /*value_offset=*/0.5);
}

TestResult ParameterBindingTests::test_bindparam_bigint_to_wvarchar_roundtrip() {
    return run_int_to_string_roundtrip<SQLBIGINT>(
        "test_bindparam_bigint_to_wvarchar_roundtrip",
        SQL_C_SBIGINT, "SQL_C_SBIGINT",
        SQL_WVARCHAR, "SQL_WVARCHAR",
        "ODBC_TEST_ROUNDTRIP_WCHAR", "NVARCHAR(20)", 20, false);
}

// ── §1.7: SQLDescribeParam reliability probe (VARCHAR shape) ───────────────
//
// IMPROVEMENT_PLAN.md §1.7. Some drivers (e.g., Firebird ≤3.5.0) return
// SQL_ERROR from SQLDescribeParam. Scanner-style consumers
// (`odbc-scanner::Params::CollectTypes`) need to know per-driver whether
// they can rely on this function at all, or have to fall back to static
// type knowledge.
TestResult ParameterBindingTests::test_sqldescribeparam_varchar() {
    return run_test(
        "test_sqldescribeparam_varchar",
        "SQLDescribeParam",
        "After PREPARE on `INSERT INTO t (varchar_col) VALUES (?)`, "
        "SQLDescribeParam returns the column type and column_size",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeParam",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE for SQLDescribeParam probe";
                result.diagnostic = last_ddl_error_;
                return;
            }

            SQLSMALLINT param_type = 0;
            SQLULEN col_size = 0;
            SQLSMALLINT scale = 0;
            SQLSMALLINT nullable = 0;
            SQLRETURN describe_rc = SQL_ERROR;
            // B3: the statement dies with the try block, so the SQLSTATE has to
            // be read while its handle is still alive.
            FailureClassification describe_failure;

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN prepare_rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(prepare_rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(prepare_rc);
                    drop_roundtrip_table();
                    return;
                }

                // Probe parameter 2 (the VARCHAR column).
                describe_rc = SQLDescribeParam(stmt.get_handle(), 2,
                                               &param_type, &col_size, &scale, &nullable);
                if (!SQL_SUCCEEDED(describe_rc)) {
                    describe_failure =
                        classify_failure(SQL_HANDLE_STMT, stmt.get_handle());
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            if (!SQL_SUCCEEDED(describe_rc)) {
                // B3: optional, so SKIP is usually right - but only when the
                // driver says so, which is what the SQLSTATE decides.
                result.status = describe_failure.status;
                result.actual = "SQLDescribeParam reported " +
                    (describe_failure.sqlstate.empty() ? std::string("no SQLSTATE")
                                                       : describe_failure.sqlstate);
                result.diagnostic = describe_failure.message;
                if (result.status == TestStatus::FAIL &&
                    result.severity > Severity::ERR) {
                    result.severity = Severity::ERR;
                }
                result.suggestion =
                    "Driver does not implement SQLDescribeParam (Firebird ≤3.5.0 "
                    "returns SQL_ERROR here). Scanner-style consumers must fall back "
                    "to static type knowledge — record this driver as "
                    "describe-param-unreliable.";
            } else {
                std::ostringstream actual;
                actual << "param_type=" << param_type
                       << " (expected SQL_VARCHAR=" << SQL_VARCHAR
                       << " or SQL_WVARCHAR=" << SQL_WVARCHAR << ")"
                       << " column_size=" << col_size
                       << " scale=" << scale
                       << " nullable=" << nullable;
                result.actual = actual.str();

                // B1: the probe stopped here, printing expected beside actual
                // and never comparing them - so it passed whatever came back,
                // including a numeric type for a character column.
                const bool is_char_type =
                    param_type == SQL_VARCHAR  || param_type == SQL_WVARCHAR ||
                    param_type == SQL_CHAR     || param_type == SQL_WCHAR    ||
                    param_type == SQL_LONGVARCHAR ||
                    param_type == SQL_WLONGVARCHAR;
                if (!is_char_type) {
                    result.status = TestStatus::FAIL;
                    result.severity = Severity::ERR;
                    result.suggestion =
                        "The parameter is bound to a VARCHAR column, so "
                        "SQLDescribeParam must report a character type. An "
                        "application that sizes its buffer from this answer "
                        "will get it wrong.";
                } else if (col_size == 0) {
                    result.status = TestStatus::FAIL;
                    result.severity = Severity::WARNING;
                    result.suggestion =
                        "column_size must be the character length of the "
                        "parameter. Zero tells an application to allocate "
                        "nothing.";
                }
            }

            drop_roundtrip_table();
        });
}

// §1.7 INTEGER cell — same shape as the VARCHAR probe but inspects
// parameter 1 (the INTEGER column) instead of parameter 2.
TestResult ParameterBindingTests::test_sqldescribeparam_integer() {
    return run_test(
        "test_sqldescribeparam_integer",
        "SQLDescribeParam",
        "After PREPARE on `INSERT INTO t (integer_col) VALUES (?)`, "
        "SQLDescribeParam returns the column type and column_size",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeParam",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE for SQLDescribeParam INTEGER probe";
                result.diagnostic = last_ddl_error_;
                return;
            }

            SQLSMALLINT param_type = 0;
            SQLULEN col_size = 0;
            SQLSMALLINT scale = 0;
            SQLSMALLINT nullable = 0;
            SQLRETURN describe_rc = SQL_ERROR;
            // B3: the statement dies with the try block, so the SQLSTATE has to
            // be read while its handle is still alive.
            FailureClassification describe_failure;

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    drop_roundtrip_table();
                    return;
                }

                // Probe parameter 1 (the INTEGER column).
                describe_rc = SQLDescribeParam(stmt.get_handle(), 1,
                                               &param_type, &col_size, &scale, &nullable);
                if (!SQL_SUCCEEDED(describe_rc)) {
                    describe_failure =
                        classify_failure(SQL_HANDLE_STMT, stmt.get_handle());
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            if (!SQL_SUCCEEDED(describe_rc)) {
                // B3: optional, so SKIP is usually right - but only when the
                // driver says so, which is what the SQLSTATE decides.
                result.status = describe_failure.status;
                result.actual = "SQLDescribeParam reported " +
                    (describe_failure.sqlstate.empty() ? std::string("no SQLSTATE")
                                                       : describe_failure.sqlstate);
                result.diagnostic = describe_failure.message;
                if (result.status == TestStatus::FAIL &&
                    result.severity > Severity::ERR) {
                    result.severity = Severity::ERR;
                }
                result.suggestion =
                    "Driver does not implement SQLDescribeParam (Firebird ≤3.5.0 "
                    "returns SQL_ERROR). Scanner-style consumers must fall back to "
                    "static type knowledge for the INTEGER shape too.";
            } else {
                std::ostringstream actual;
                actual << "param_type=" << param_type
                       << " (expected SQL_INTEGER=" << SQL_INTEGER
                       << " or SQL_BIGINT=" << SQL_BIGINT << ")"
                       << " column_size=" << col_size
                       << " scale=" << scale
                       << " nullable=" << nullable;
                result.actual = actual.str();

                // B1 - see test_sqldescribeparam_varchar. An exact-integer
                // column may legitimately be described as INTEGER, BIGINT,
                // SMALLINT or (on engines that model integers as scale-0
                // decimals) DECIMAL/NUMERIC. A character or floating type is
                // not a difference of opinion.
                const bool is_exact_numeric =
                    param_type == SQL_INTEGER  || param_type == SQL_BIGINT  ||
                    param_type == SQL_SMALLINT || param_type == SQL_TINYINT ||
                    param_type == SQL_DECIMAL  || param_type == SQL_NUMERIC;
                if (!is_exact_numeric) {
                    result.status = TestStatus::FAIL;
                    result.severity = Severity::ERR;
                    result.suggestion =
                        "The parameter is bound to an INTEGER column, so "
                        "SQLDescribeParam must report an exact numeric type.";
                }
            }

            drop_roundtrip_table();
        });
}

// §1.3 `_endtran` — after binding once and executing N times on a prepared
// INSERT (no intervening close), does the trailing `SQLEndTran(SQL_COMMIT)`
// itself succeed? On some drivers the connection is left in a state where
// the second execute returns OK but COMMIT then fails with HY010. The
// `_row_count` cell measured row persistence; this cell measures the
// COMMIT return code in isolation so the failure mode can be reported
// even when individual executes look fine.
TestResult ParameterBindingTests::test_param_bind_once_execute_many_endtran() {
    return run_test(
        "test_param_bind_once_execute_many_endtran",
        "SQLEndTran(SQL_COMMIT)",
        "Bind once, mutate variable, execute N times, COMMIT — does the "
        "COMMIT itself succeed?",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLEndTran after re-execute",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            constexpr int kRowCount = 10;
            int execute_errors = 0;
            SQLRETURN commit_rc = SQL_ERROR;
            std::string first_error;

            try {
                // A14: this probe used to restore autocommit to a hard-coded ON in
                // three separate places. The guard records whatever it actually was
                // and puts that back however this block is left — including by the
                // exception path below.
                ScopedAutocommitOn ac(conn_.get_handle());

                // Switch to manual-commit so SQLEndTran has work to do — autocommit
                // would have already committed every row inside the loop.
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                  (SQLPOINTER)SQL_AUTOCOMMIT_OFF, 0);

                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    // A14: no manual restore — the guard above puts back the value
                    // the connection actually had, on this path too.
                    drop_roundtrip_table();
                    return;
                }

                SQLINTEGER id_val = 0, val_val = 0;
                SQLLEN id_ind = 0, val_ind = 0;
                SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                 SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                 &id_val, 0, &id_ind);
                SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                 SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                 &val_val, 0, &val_ind);

                for (int i = 1; i <= kRowCount; ++i) {
                    id_val = i;
                    val_val = i;
                    SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                    if (!SQL_SUCCEEDED(exec_rc)) {
                        ++execute_errors;
                        if (first_error.empty()) {
                            first_error = "SQLExecute row " + std::to_string(i) +
                                          " returned " + std::to_string(exec_rc);
                        }
                    }
                }

                // The probe under test — does COMMIT succeed?
                commit_rc = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);
                // A14: no manual restore — ScopedAutocommitOn above does it.
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            std::ostringstream actual;
            actual << "executes=" << kRowCount
                   << " execute_errors=" << execute_errors
                   << " commit_rc=" << commit_rc;
            result.actual = actual.str();

            if (!SQL_SUCCEEDED(commit_rc)) {
                result.status = TestStatus::FAIL;
                if (!first_error.empty()) result.diagnostic = first_error;
                result.suggestion =
                    "SQLEndTran(SQL_COMMIT) failed after a sequence of re-executes "
                    "on a prepared INSERT. Common cause: driver leaves the connection "
                    "in a state where the open cursor invalidates the txn (DuckDB "
                    "HY010 family). Application fix: SQLFreeStmt(SQL_CLOSE) before "
                    "COMMIT, or rebind+commit per row.";
            } else if (execute_errors > 0) {
                // COMMIT OK but some executes errored — note but don't fail.
                result.suggestion =
                    "Driver returned errors during execute but the COMMIT itself "
                    "succeeded. Check SQLGetDiagRec on the failing executes — "
                    "they probably wanted SQLFreeStmt(SQL_CLOSE).";
            }

            drop_roundtrip_table();
        });
}

// §1.7 DECIMAL cell — same shape as INTEGER/VARCHAR but the column is
// DECIMAL(10, 2). Uses its own table so the regular round-trip table
// schema stays unchanged.
TestResult ParameterBindingTests::test_sqldescribeparam_decimal() {
    return run_test(
        "test_sqldescribeparam_decimal",
        "SQLDescribeParam",
        "After PREPARE on `INSERT INTO t (decimal_col) VALUES (?)`, "
        "SQLDescribeParam returns the column type, precision, and scale",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeParam",
        [&](TestResult& result) {
            const std::string table_name = "ODBC_TEST_ROUNDTRIP_DEC";

            if (!create_roundtrip_table(table_name, "DECIMAL(10, 2)")) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE " + table_name +
                                " (ID INTEGER, VAL DECIMAL(10, 2))";
                result.diagnostic = last_ddl_error_;
                result.suggestion =
                    "Driver may not accept inline DECIMAL(p,s) DDL or the user "
                    "lacks DDL privileges. Skip — cannot probe SQLDescribeParam "
                    "for the DECIMAL shape.";
                return;
            }

            SQLSMALLINT param_type = 0;
            SQLULEN col_size = 0;
            SQLSMALLINT scale = 0;
            SQLSMALLINT nullable = 0;
            SQLRETURN describe_rc = SQL_ERROR;
            // B3: the statement dies with the try block, so the SQLSTATE has to
            // be read while its handle is still alive.
            FailureClassification describe_failure;

            try {
                core::OdbcStatement stmt(conn_);
                const std::string insert_sql =
                    "INSERT INTO " + table_name + " (ID, VAL) VALUES (?, ?)";
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)insert_sql.c_str(), SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    drop_roundtrip_table(table_name);
                    return;
                }

                // Probe parameter 2 (the DECIMAL column).
                describe_rc = SQLDescribeParam(stmt.get_handle(), 2,
                                               &param_type, &col_size, &scale, &nullable);
                if (!SQL_SUCCEEDED(describe_rc)) {
                    describe_failure =
                        classify_failure(SQL_HANDLE_STMT, stmt.get_handle());
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table(table_name);
                return;
            }

            if (!SQL_SUCCEEDED(describe_rc)) {
                // B3: optional, so SKIP is usually right - but only when the
                // driver says so, which is what the SQLSTATE decides.
                result.status = describe_failure.status;
                result.actual = "SQLDescribeParam reported " +
                    (describe_failure.sqlstate.empty() ? std::string("no SQLSTATE")
                                                       : describe_failure.sqlstate);
                result.diagnostic = describe_failure.message;
                if (result.status == TestStatus::FAIL &&
                    result.severity > Severity::ERR) {
                    result.severity = Severity::ERR;
                }
                result.suggestion =
                    "Driver does not implement SQLDescribeParam (Firebird ≤3.5.0 "
                    "returns SQL_ERROR). Scanner-style consumers must fall back to "
                    "static type knowledge for the DECIMAL shape too.";
            } else {
                std::ostringstream actual;
                actual << "param_type=" << param_type
                       << " (expected SQL_DECIMAL=" << SQL_DECIMAL
                       << " or SQL_NUMERIC=" << SQL_NUMERIC << ")"
                       << " precision=" << col_size
                       << " scale=" << scale
                       << " nullable=" << nullable;
                result.actual = actual.str();

                // B1 - see test_sqldescribeparam_varchar. DECIMAL and NUMERIC
                // are interchangeable in practice, and a driver that models
                // the column as a float is wrong in a way that costs
                // precision silently.
                if (param_type != SQL_DECIMAL && param_type != SQL_NUMERIC) {
                    result.status = TestStatus::FAIL;
                    result.severity = Severity::ERR;
                    result.suggestion =
                        "The parameter is bound to a DECIMAL column. "
                        "Reporting a floating type here is how exact values "
                        "start losing digits without anything failing.";
                }
            }

            drop_roundtrip_table(table_name);
        });
}

// §1.3 — does the driver require SQLFreeStmt(SQL_CLOSE) between consecutive
// SQLExecutes on a prepared INSERT? The DuckDB ODBC HY010 trap. Records
// whether the second execute succeeds without an intervening close, and
// whether closing-then-executing recovers if it didn't.
TestResult ParameterBindingTests::test_param_reexecute_requires_close() {
    return run_test(
        "test_param_reexecute_requires_close",
        "SQLExecute/SQLFreeStmt(SQL_CLOSE)",
        "Two consecutive SQLExecutes on a prepared INSERT — does the "
        "driver tolerate them, or require SQL_CLOSE in between?",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecute, SQLFreeStmt(SQL_CLOSE) — informational probe",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            SQLRETURN exec1 = SQL_ERROR;
            SQLRETURN exec2 = SQL_ERROR;
            SQLRETURN exec_after_close = SQL_ERROR;
            bool close_was_needed = false;

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    drop_roundtrip_table();
                    return;
                }

                SQLINTEGER id_val = 0, val_val = 0;
                SQLLEN id_ind = 0, val_ind = 0;
                SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                 SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                 &id_val, 0, &id_ind);
                SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                 SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                 &val_val, 0, &val_ind);

                id_val = 1; val_val = 1;
                exec1 = SQLExecute(stmt.get_handle());

                id_val = 2; val_val = 2;
                exec2 = SQLExecute(stmt.get_handle());

                if (!SQL_SUCCEEDED(exec2) && SQL_SUCCEEDED(exec1)) {
                    close_was_needed = true;
                    SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
                    id_val = 2; val_val = 2;
                    exec_after_close = SQLExecute(stmt.get_handle());
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            // A22: this commit only tidies up - the finding is already decided by
            // exec1/exec2 above - but a silently discarded return code is how a
            // broken commit stays invisible, so it is reported when it fails.
            const CommitOutcome commit = commit_now();

            std::ostringstream actual;
            actual << "exec1=" << exec1 << " exec2=" << exec2;
            if (!commit) actual << "  (" << commit.summary << ")";
            if (close_was_needed) {
                actual << " exec_after_close=" << exec_after_close
                       << "  (driver REQUIRES SQLFreeStmt(SQL_CLOSE) between executes)";
                result.suggestion =
                    "Driver returns an error on the second SQLExecute without an "
                    "intervening SQL_CLOSE. Bulk-insert consumers must close between "
                    "iterations or rebind per-row. This is the DuckDB ODBC behaviour.";
            } else if (SQL_SUCCEEDED(exec1) && SQL_SUCCEEDED(exec2)) {
                // B1/B2: both answers are conformant. The spec does not say
                // whether a prepared statement may be re-executed without an
                // intervening SQLFreeStmt(SQL_CLOSE), and the two behaviours
                // are what a bulk-insert consumer needs to plan around - so
                // this records which one the driver does rather than grading
                // it. (The first-execute failure below is still a skip: there
                // the probe never reached the question.)
                result.status = TestStatus::INFORMATIONAL;
                actual << "  (driver does NOT require close between executes)";
            } else if (!SQL_SUCCEEDED(exec1)) {
                actual << "  (first SQLExecute itself failed; result inconclusive)";
                result.status = TestStatus::SKIP_INCONCLUSIVE;
            }
            result.actual = actual.str();

            drop_roundtrip_table();
        });
}

// ── §1.8: SQLRowCount reliability matrix ───────────────────────────────────
//
// IMPROVEMENT_PLAN.md §1.8. Per the spec `SQLRowCount` returns the number
// of rows affected by the most recent INSERT/UPDATE/DELETE on the
// statement. In practice it lies on some drivers (-1 vs 0 confusion on
// stored-procedure paths in particular). These three tests probe each
// mutation path and report the actual value.
namespace {

// Record an explicit FAIL when SQLRowCount produces a wrong answer; the
// test message includes both expected and observed counts.
void record_rowcount_mismatch(TestResult& result, const std::string& op,
                              SQLLEN expected, SQLLEN observed) {
    result.status = TestStatus::FAIL;
    std::ostringstream actual;
    actual << "SQLRowCount after " << op << " returned " << observed
           << " (expected " << expected << ")";
    result.actual = actual.str();
    result.suggestion =
        "Driver's SQLRowCount is unreliable for this DML shape. "
        "Consumers that key on row count must round-trip via "
        "SELECT COUNT(*) instead.";
}

}  // namespace

TestResult ParameterBindingTests::test_sqlrowcount_after_insert() {
    return run_test(
        "test_sqlrowcount_after_insert",
        "SQLRowCount",
        "After INSERT INTO t VALUES (?, ?), SQLRowCount returns 1",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE for SQLRowCount probe";
                result.diagnostic = last_ddl_error_;
                return;
            }

            try {
                core::OdbcStatement stmt(conn_);
                stmt.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, '1')");
                SQLLEN row_count = -2;
                SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
                if (!SQL_SUCCEEDED(rc)) {
                    // B3: SQLRowCount is Core. Only a driver that says
                    // "not implemented" earns a SKIP here.
                    report_failure(result, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLRowCount");
                } else if (row_count != 1) {
                    record_rowcount_mismatch(result, "INSERT", 1, row_count);
                } else {
                    result.actual = "SQLRowCount = 1 after single-row INSERT";
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
            }

            drop_roundtrip_table();
        });
}

TestResult ParameterBindingTests::test_sqlrowcount_after_update() {
    return run_test(
        "test_sqlrowcount_after_update",
        "SQLRowCount",
        "After UPDATE t SET v=…, SQLRowCount returns the inserted row count",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            try {
                core::OdbcStatement seed(conn_);
                seed.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, 'a')");
                core::OdbcStatement seed2(conn_);
                seed2.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (2, 'b')");
                core::OdbcStatement seed3(conn_);
                seed3.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (3, 'c')");
                // A22: this is the seed, and the probe asserts SQLRowCount == 3
                // against it. If the seed did not commit, the probe's premise is
                // gone and any row count it reads means nothing - so say so rather
                // than grade the driver on a fixture that was never established.
                const CommitOutcome seed_commit = commit_now();
                if (!seed_commit) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "Could not seed 3 rows: " + seed_commit.summary;
                    drop_roundtrip_table();
                    return;
                }

                core::OdbcStatement stmt(conn_);
                stmt.execute("UPDATE ODBC_TEST_ROUNDTRIP SET VAL = 'X'");
                SQLLEN row_count = -2;
                SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
                if (!SQL_SUCCEEDED(rc)) {
                    // B3: SQLRowCount is Core. Only a driver that says
                    // "not implemented" earns a SKIP here.
                    report_failure(result, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLRowCount");
                } else if (row_count != 3) {
                    record_rowcount_mismatch(result, "UPDATE", 3, row_count);
                } else {
                    result.actual = "SQLRowCount = 3 after UPDATE matching all 3 rows";
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
            }

            drop_roundtrip_table();
        });
}

TestResult ParameterBindingTests::test_sqlrowcount_after_delete() {
    return run_test(
        "test_sqlrowcount_after_delete",
        "SQLRowCount",
        "After DELETE FROM t WHERE …, SQLRowCount returns the matched row count",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            try {
                core::OdbcStatement seed(conn_);
                seed.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, 'a')");
                core::OdbcStatement seed2(conn_);
                seed2.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (2, 'b')");
                // A22 - see test_sqlrowcount_after_update.
                const CommitOutcome seed_commit = commit_now();
                if (!seed_commit) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "Could not seed 2 rows: " + seed_commit.summary;
                    drop_roundtrip_table();
                    return;
                }

                core::OdbcStatement stmt(conn_);
                stmt.execute("DELETE FROM ODBC_TEST_ROUNDTRIP");
                SQLLEN row_count = -2;
                SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
                if (!SQL_SUCCEEDED(rc)) {
                    // B3: SQLRowCount is Core. Only a driver that says
                    // "not implemented" earns a SKIP here.
                    report_failure(result, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLRowCount");
                } else if (row_count != 2) {
                    record_rowcount_mismatch(result, "DELETE", 2, row_count);
                } else {
                    result.actual = "SQLRowCount = 2 after DELETE matching all 2 rows";
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
            }

            drop_roundtrip_table();
        });
}

// §1.8 final cell — `SQLRowCount` after `EXECUTE PROCEDURE` /
// `CALL <proc>(args)`. Per the ODBC spec this commonly returns -1
// ("affected count unknown"), but engines vary — Firebird returns 0,
// some engines return the actual row count from the SP body. The probe
// is informational: it always PASSes when the CALL succeeds, dumps the
// raw SQLRowCount value, and ALSO verifies via `verify_rows_persisted`
// that the rows the SP claims to have inserted actually landed. This
// catches "SP looked successful but didn't persist" bugs separately
// from the count-reporting question.
TestResult ParameterBindingTests::test_sqlrowcount_after_execute_procedure() {
    return run_test(
        "test_sqlrowcount_after_execute_procedure",
        "SQLRowCount",
        "After CALL INSERT_N_ROWS('ODBC_TEST_ROUNDTRIP', 5), report SQLRowCount "
        "(spec baseline = -1, may vary) and verify 5 rows persisted",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount after EXECUTE PROCEDURE — Appendix B",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            constexpr int kRowCount = 5;
            SQLRETURN exec_rc = SQL_ERROR;
            SQLLEN row_count = -2;
            bool sp_supported = false;

            try {
                core::OdbcStatement stmt(conn_);
                // Try the canonical INSERT_N_ROWS procedure (registered by the mock
                // driver). On real drivers this likely won't exist; SKIP_UNSUPPORTED
                // when the CALL fails because no such procedure exists, so the test
                // doesn't fail spuriously.
                exec_rc = SQLExecDirect(
                    stmt.get_handle(),
                    (SQLCHAR*)"CALL INSERT_N_ROWS('ODBC_TEST_ROUNDTRIP', 5)",
                    SQL_NTS);

                if (!SQL_SUCCEEDED(exec_rc)) {
                    // Try Firebird-style syntax as a fallback.
                    exec_rc = SQLExecDirect(
                        stmt.get_handle(),
                        (SQLCHAR*)"EXECUTE PROCEDURE INSERT_N_ROWS('ODBC_TEST_ROUNDTRIP', 5)",
                        SQL_NTS);
                }

                if (SQL_SUCCEEDED(exec_rc)) {
                    sp_supported = true;
                    SQLRowCount(stmt.get_handle(), &row_count);
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            if (!sp_supported) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "Driver does not have INSERT_N_ROWS procedure "
                                "(rc=" + std::to_string(exec_rc) + "). On real drivers "
                                "create one matching the mock signature, or run this "
                                "probe against the mock driver.";
                result.suggestion =
                    "INSERT_N_ROWS(table_name VARCHAR, n INTEGER) is registered by "
                    "the mock driver to standardise §1.8 SP probing. For real-driver "
                    "runs, port the same shape (a procedure that inserts N rows) and "
                    "re-run the probe.";
                drop_roundtrip_table();
                return;
            }

            // A22: keep the return code. A failed COMMIT used to be discarded,
            // and the missing rows were then blamed on the bind path.
            const CommitOutcome commit = commit_now();

            RowVerification v = verify_rows_persisted(
                "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kRowCount);

            std::ostringstream actual;
            actual << "exec_rc=" << exec_rc
                   << " SQLRowCount=" << row_count
                   << " (spec baseline -1; some engines return 0 or the real count)"
                   << " " << commit.summary
                   << " verify=" << (v.ok ? "OK" : v.diagnostic)
                   << " persisted_rows=" << v.actual_count;
            result.actual = actual.str();

            if (!v.ok) {
                // SQLRowCount is informational; the persistence check is the real
                // test. If the SP claimed success but rows didn't land, that's a
                // hard FAIL.
                result.status = TestStatus::FAIL;
                result.severity = Severity::CRITICAL;
                result.suggestion = commit
                    ? "Procedure CALL returned SQL_SUCCESS but the rows it claimed to "
                      "insert are not visible. This is a silent-corruption shape — "
                      "either the SP body never ran, the txn rolled back, or the data "
                      "went somewhere unexpected."
                    : "The COMMIT failed, so the rows are missing because the "
                      "transaction never committed — not because the driver lost "
                      "them on the bind path. Fix the commit failure first; this "
                      "probe cannot say anything about binding until it succeeds.";
            }

            drop_roundtrip_table();
        });
}

// ── §1.2: Per-row rebind + post-commit row count ──────────────────────────
//
// IMPROVEMENT_PLAN.md §1.2. The Firebird ≤3.5.0 silent-corruption shape:
// the application reset its parameter binding before every execute,
// committed at the end, then read back fewer rows than it inserted —
// every API call returned SQL_SUCCESS while the driver dropped most
// rows. This test inserts 100 rows with `SQLFreeStmt(SQL_RESET_PARAMS)
// + SQLBindParameter` between each `SQLExecute`, commits, and uses
// `verify_rows_persisted` to assert all 100 actually landed.
TestResult ParameterBindingTests::test_param_rebind_per_row_row_count() {
    return run_test(
        "test_param_rebind_per_row_row_count",
        "SQLBindParameter",
        "Per-row rebind/execute loop persists every row (Firebird #161 shape)",
        Severity::CRITICAL,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter, SQLFreeStmt(SQL_RESET_PARAMS)",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            constexpr int kRowCount = 100;
            int execute_errors = 0;
            std::string first_error;

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    drop_roundtrip_table();
                    return;
                }

                for (int i = 1; i <= kRowCount; ++i) {
                    // Reset previous bindings; this is the path that triggered #161.
                    SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);

                    SQLINTEGER id_val = i;
                    SQLINTEGER val_val = i;
                    SQLLEN id_ind = 0;
                    SQLLEN val_ind = 0;

                    SQLRETURN bid = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                                     SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                                     &id_val, 0, &id_ind);
                    SQLRETURN bvl = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                                     SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                                     &val_val, 0, &val_ind);
                    if (!SQL_SUCCEEDED(bid) || !SQL_SUCCEEDED(bvl)) {
                        ++execute_errors;
                        if (first_error.empty()) {
                            first_error = "Bind failed at row " + std::to_string(i) +
                                          " (id_rc=" + std::to_string(bid) +
                                          ", val_rc=" + std::to_string(bvl) + ")";
                        }
                        continue;
                    }

                    SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                    if (!SQL_SUCCEEDED(exec_rc)) {
                        ++execute_errors;
                        if (first_error.empty()) {
                            first_error = "SQLExecute row " + std::to_string(i) +
                                          " returned " + std::to_string(exec_rc);
                        }
                    }
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = std::string("Loop threw: ") + e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            // A22: keep the return code. A failed COMMIT used to be discarded,
            // and the missing rows were then blamed on the bind path.
            const CommitOutcome commit = commit_now();

            RowVerification v = verify_rows_persisted(
                "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kRowCount);

            if (!v.ok) {
                result.status = TestStatus::FAIL;
                std::ostringstream actual;
                actual << v.diagnostic
                       << " (count=" << v.actual_count
                       << ", fetched_rows=" << v.actual_values.size()
                       << ", execute_errors=" << execute_errors
                       << ", " << commit.summary << ")";
                result.actual = actual.str();
                if (!first_error.empty()) result.diagnostic = first_error;
                result.suggestion = commit
                    ? "Per-row rebind+execute lost rows — this is the Firebird #161 / "
                      "MySQL/MSSQL silent-corruption shape. Driver's parameter-binding "
                      "path is broken. Application-side workaround: bind once and reuse "
                      "the buffer instead of re-binding per row."
                    : "The COMMIT failed, so the rows are missing because the "
                      "transaction never committed — not because the driver lost "
                      "them on the bind path. Fix the commit failure first; this "
                      "probe cannot say anything about binding until it succeeds.";
            } else {
                if (execute_errors == 0) {
                    result.actual = "All " + std::to_string(kRowCount) +
                                    " per-row rebind+execute calls succeeded; "
                                    "verify_rows_persisted confirmed 100 rows.";
                } else {
                    result.status = TestStatus::FAIL;
                    result.severity = Severity::WARNING;
                    result.actual = "Rows persisted but " +
                                    std::to_string(execute_errors) +
                                    " bind/execute calls reported errors (first: " +
                                    first_error + ")";
                }
            }

            drop_roundtrip_table();
        });
}

// ── §1.3: Bind-once-execute-many stability ────────────────────────────────
//
// IMPROVEMENT_PLAN.md §1.3. The mirror image of §1.2: bind a parameter
// once into a stack variable, mutate the variable in a loop, and call
// SQLExecute repeatedly without re-binding or re-preparing. This is the
// `odbc-scanner` shape that produced HY010 "Function sequence error" on
// DuckDB after the second SQLExecute. The test records:
//
//   - whether all 50 SQLExecute calls succeed
//   - whether the post-loop COMMIT succeeds
//   - whether all 50 rows actually persist
//
// On HY010 / loop failure we PASS-with-warning rather than FAIL — the
// spec is genuinely ambiguous here and "the driver demands SQLFreeStmt
// (SQL_CLOSE) between executes" is a known-supported variant. The test
// surfaces the behaviour for the consumer to plan around.
TestResult ParameterBindingTests::test_param_bind_once_execute_many_row_count() {
    return run_test(
        "test_param_bind_once_execute_many_row_count",
        "SQLBindParameter+SQLExecute",
        "Bind once, mutate variable, execute N times — all rows persist",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter (parameter values persist across executions)",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            constexpr int kRowCount = 50;
            int execute_errors = 0;
            std::string first_error;
            SQLINTEGER id_val = 0;
            SQLINTEGER val_val = 0;
            SQLLEN id_ind = 0;
            SQLLEN val_ind = 0;

            try {
                core::OdbcStatement stmt(conn_);
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_INCONCLUSIVE;
                    result.actual = "SQLPrepare returned " + std::to_string(rc);
                    drop_roundtrip_table();
                    return;
                }

                // Bind ONCE, before the loop. The variables persist between executes.
                SQLRETURN bid = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                                 SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                                 &id_val, 0, &id_ind);
                SQLRETURN bvl = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                                 SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                                 &val_val, 0, &val_ind);
                if (!SQL_SUCCEEDED(bid) || !SQL_SUCCEEDED(bvl)) {
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "Initial SQLBindParameter failed (id_rc=" +
                                    std::to_string(bid) + ", val_rc=" +
                                    std::to_string(bvl) + ")";
                    drop_roundtrip_table();
                    return;
                }

                for (int i = 1; i <= kRowCount; ++i) {
                    id_val = i;
                    val_val = i;
                    SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                    if (!SQL_SUCCEEDED(exec_rc)) {
                        ++execute_errors;
                        if (first_error.empty()) {
                            first_error = "SQLExecute row " + std::to_string(i) +
                                          " returned " + std::to_string(exec_rc);
                        }
                    }
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = std::string("Loop threw: ") + e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            SQLRETURN commit_rc = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

            RowVerification v = verify_rows_persisted(
                "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kRowCount);

            std::ostringstream actual;
            actual << "executes=" << kRowCount
                   << " errors=" << execute_errors
                   << " commit_rc=" << commit_rc
                   << " verify=" << (v.ok ? "OK" : v.diagnostic)
                   << " count=" << v.actual_count;
            result.actual = actual.str();

            if (!v.ok) {
                result.status = TestStatus::FAIL;
                if (!first_error.empty()) result.diagnostic = first_error;
                result.suggestion =
                    "Bind-once-execute-many lost rows. If the executes returned "
                    "HY010, the driver requires SQLFreeStmt(SQL_CLOSE) between "
                    "executes (DuckDB ODBC behaviour). Application-side fix: close "
                    "the cursor between iterations, or rebind per row.";
            } else if (execute_errors > 0) {
                // Rows landed but executes errored — record but don't fail.
                result.suggestion =
                    "Driver returned errors during execute but the rows still "
                    "persisted. Worth checking SQLGetDiagRec on each non-success "
                    "return to see if the driver expected SQLFreeStmt(SQL_CLOSE).";
            }

            drop_roundtrip_table();
        });
}

// ── §1.5: Batch-insert then per-row tail (`odbc_copy` pattern) ────────────
//
// IMPROVEMENT_PLAN.md §1.5. The `odbc-scanner` bulk-copy path PREPAREs an
// `INSERT VALUES (?, ?), (?, ?), …` with N value-tuple slots, executes it
// for each full batch, then PREPAREs a single-row `INSERT VALUES (?, ?)`
// for the remaining tail rows. This test exercises both shapes back-to-back
// against the same statement handle (the actual scanner uses a fresh
// handle for the tail; we use the same one here to make sure the driver
// can re-prepare cleanly), then verifies the final row count.
TestResult ParameterBindingTests::test_param_batch_then_single_row_tail() {
    return run_test(
        "test_param_batch_then_single_row_tail",
        "SQLPrepare+SQLBindParameter+SQLExecute",
        "PREPARE multi-row INSERT batch + PREPARE single-row tail; "
        "all rows persist",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, SQLBindParameter (re-prepare on same handle)",
        [&](TestResult& result) {
            if (!create_roundtrip_table()) {
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.actual = "Could not CREATE TABLE";
                result.diagnostic = last_ddl_error_;
                return;
            }

            // 16 rows in the batch + 3 tail = 19 total rows.
            constexpr int kBatchSize = 16;
            constexpr int kTailSize = 3;
            constexpr int kTotalRows = kBatchSize + kTailSize;
            bool batch_executed = false;
            bool tail_executed = false;
            std::string failure_diag;

            try {
                core::OdbcStatement stmt(conn_);

                // Build the multi-row INSERT statement: (?,?),(?,?),... × kBatchSize
                std::string batch_sql = "INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES ";
                for (int i = 0; i < kBatchSize; ++i) {
                    if (i > 0) batch_sql += ", ";
                    batch_sql += "(?, ?)";
                }

                SQLRETURN rc = SQLPrepare(stmt.get_handle(),
                                          (SQLCHAR*)batch_sql.c_str(), SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::SKIP_UNSUPPORTED;
                    result.actual = "Batch SQLPrepare returned " + std::to_string(rc) +
                                    " — driver does not accept multi-row VALUES with " +
                                    std::to_string(kBatchSize) + " tuples.";
                    drop_roundtrip_table();
                    return;
                }

                std::vector<SQLINTEGER> ids(kBatchSize);
                std::vector<SQLINTEGER> vals(kBatchSize);
                std::vector<SQLLEN> id_inds(kBatchSize, 0);
                std::vector<SQLLEN> val_inds(kBatchSize, 0);
                for (int i = 0; i < kBatchSize; ++i) {
                    ids[i] = i + 1;
                    vals[i] = i + 1;
                    SQLRETURN bid = SQLBindParameter(stmt.get_handle(),
                                                     static_cast<SQLUSMALLINT>(2 * i + 1),
                                                     SQL_PARAM_INPUT,
                                                     SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                                     &ids[i], 0, &id_inds[i]);
                    SQLRETURN bvl = SQLBindParameter(stmt.get_handle(),
                                                     static_cast<SQLUSMALLINT>(2 * i + 2),
                                                     SQL_PARAM_INPUT,
                                                     SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                                     &vals[i], 0, &val_inds[i]);
                    if (!SQL_SUCCEEDED(bid) || !SQL_SUCCEEDED(bvl)) {
                        result.status = TestStatus::SKIP_UNSUPPORTED;
                        result.actual = "Batch bind row " + std::to_string(i + 1) +
                                        " failed (id_rc=" + std::to_string(bid) +
                                        ", val_rc=" + std::to_string(bvl) + ")";
                        drop_roundtrip_table();
                        return;
                    }
                }

                SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
                if (!SQL_SUCCEEDED(exec_rc)) {
                    result.status = TestStatus::FAIL;
                    result.actual = "Batch SQLExecute returned " + std::to_string(exec_rc);
                    result.suggestion =
                        "Driver claimed to accept the multi-row INSERT prepare but "
                        "rejected execution. Treat multi-row VALUES as unsupported.";
                    drop_roundtrip_table();
                    return;
                }
                batch_executed = true;

                // Re-PREPARE on the same statement with the single-row shape.
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);

                rc = SQLPrepare(stmt.get_handle(),
                                (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
                                SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    failure_diag = "Tail SQLPrepare returned " + std::to_string(rc);
                } else {
                    SQLINTEGER tail_id = 0;
                    SQLINTEGER tail_val = 0;
                    SQLLEN tail_id_ind = 0;
                    SQLLEN tail_val_ind = 0;
                    SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                                     SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                     &tail_id, 0, &tail_id_ind);
                    SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                                     SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                                     &tail_val, 0, &tail_val_ind);

                    for (int i = 0; i < kTailSize; ++i) {
                        tail_id = kBatchSize + i + 1;
                        tail_val = kBatchSize + i + 1;
                        SQLRETURN trc = SQLExecute(stmt.get_handle());
                        if (!SQL_SUCCEEDED(trc)) {
                            failure_diag = "Tail SQLExecute row " +
                                           std::to_string(i + 1) + " returned " +
                                           std::to_string(trc);
                            break;
                        }
                    }
                    if (failure_diag.empty()) tail_executed = true;
                }
            } catch (const core::OdbcError& e) {
                result.status = TestStatus::ERR;
                result.actual = e.what();
                result.diagnostic = e.format_diagnostics();
                drop_roundtrip_table();
                return;
            }

            // A22: keep the return code. A failed COMMIT used to be discarded,
            // and the missing rows were then blamed on the bind path.
            const CommitOutcome commit = commit_now();

            RowVerification v = verify_rows_persisted(
                "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kTotalRows);

            if (!v.ok) {
                result.status = TestStatus::FAIL;
                std::ostringstream actual;
                actual << "batch_executed=" << batch_executed
                       << " tail_executed=" << tail_executed
                       << " count=" << v.actual_count
                       << " expected=" << kTotalRows
                       << " " << commit.summary
                       << " verify_diag=" << v.diagnostic;
                result.actual = actual.str();
                if (!failure_diag.empty()) result.diagnostic = failure_diag;
                result.suggestion = commit
                    ? "Batch-then-tail INSERT lost rows. The most common cause is the "
                      "driver carrying batch-prepare state into the tail prepare; some "
                      "drivers need a fresh statement handle for the tail."
                    : "The COMMIT failed, so the rows are missing because the "
                      "transaction never committed — not because the driver lost "
                      "them on the bind path. Fix the commit failure first; this "
                      "probe cannot say anything about binding until it succeeds.";
            } else {
                result.actual = "All " + std::to_string(kTotalRows) +
                                " rows persisted (" + std::to_string(kBatchSize) +
                                " batch + " + std::to_string(kTailSize) + " tail)";
            }

            drop_roundtrip_table();
        });
}

} // namespace odbc_crusher::tests
