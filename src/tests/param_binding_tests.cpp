#include "param_binding_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
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
    results.push_back(test_param_rebind_execute());
    results.push_back(test_bindparam_int_to_varchar_roundtrip());
    results.push_back(test_sqldescribeparam_varchar());
    results.push_back(test_sqlrowcount_after_insert());
    results.push_back(test_sqlrowcount_after_update());
    results.push_back(test_sqlrowcount_after_delete());
    results.push_back(test_param_rebind_per_row_row_count());
    results.push_back(test_param_bind_once_execute_many_row_count());
    results.push_back(test_param_batch_then_single_row_tail());

    return results;
}

// ── Round-trip test table lifecycle ─────────────────────────────────────────
//
// Keep this table distinct from ODBC_TEST_ARRAY so the two categories don't
// interfere. Follows the same CREATE-first / DROP-and-retry pattern as
// ArrayParamTests to survive Firebird's "DDL failure invalidates the txn" rule
// (see PROJECT_PLAN.md lesson 15).

bool ParameterBindingTests::create_roundtrip_table() {
    SQLUINTEGER old_ac = 0;
    SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT, &old_ac, 0, nullptr);
    SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                      (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);

    auto restore_ac = [&]() {
        SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                          (SQLPOINTER)(intptr_t)old_ac, 0);
    };

    const std::vector<std::string> ddl = {
        "CREATE TABLE ODBC_TEST_ROUNDTRIP (ID INTEGER, VAL VARCHAR(32))",
        "CREATE TABLE ODBC_TEST_ROUNDTRIP (ID INT, VAL VARCHAR(32))"
    };

    auto try_create = [&]() -> bool {
        for (const auto& sql : ddl) {
            try {
                core::OdbcStatement s(conn_);
                s.execute(sql);
                return true;
            } catch (const core::OdbcError& e) {
                last_ddl_error_ = e.format_diagnostics();
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
            } catch (...) {
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
            }
        }
        return false;
    };

    if (try_create()) { restore_ac(); return true; }

    try {
        core::OdbcStatement drop_stmt(conn_);
        drop_stmt.execute("DROP TABLE ODBC_TEST_ROUNDTRIP");
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
    }

    bool ok = try_create();
    restore_ac();
    return ok;
}

void ParameterBindingTests::drop_roundtrip_table() {
    SQLUINTEGER old_ac = 0;
    SQLGetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT, &old_ac, 0, nullptr);
    SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                      (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
    try {
        core::OdbcStatement s(conn_);
        s.execute("DROP TABLE ODBC_TEST_ROUNDTRIP");
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
    }
    SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                      (SQLPOINTER)(intptr_t)old_ac, 0);
}

TestResult ParameterBindingTests::test_bindparam_wchar_input() {
    TestResult result = make_result(
        "test_bindparam_wchar_input",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQLBindParameter with SQL_C_WCHAR input type accepts Unicode data",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_C_WCHAR for Unicode parameter data"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Prepare a parameterized query — try multiple patterns
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        // (some drivers export W-functions but have broken W→A conversion)
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare parameterized query";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
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
            actual << "SQLBindParameter with SQL_C_WCHAR returned " << ret;
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Driver may not support SQL_C_WCHAR parameter binding";
        }
        result.actual = actual.str();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult ParameterBindingTests::test_bindparam_null_indicator() {
    TestResult result = make_result(
        "test_bindparam_null_indicator",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQLBindParameter with SQL_NULL_DATA indicator passes NULL to driver",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_NULL_DATA in StrLen_or_IndPtr for NULL"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare query for NULL parameter test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
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
            result.status = TestStatus::FAIL;
            result.suggestion = "Drivers must accept SQL_NULL_DATA as parameter indicator";
        }
        result.actual = actual.str();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult ParameterBindingTests::test_param_rebind_execute() {
    TestResult result = make_result(
        "test_param_rebind_execute",
        "SQLBindParameter",
        TestStatus::PASS,
        "Bind, execute, rebind with new value, execute again",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: Parameters persist across executions"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS INTEGER)",
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare query for rebind test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // First bind and execute
        SQLINTEGER param_val = 1;
        SQLLEN ind = 0;
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, &param_val, 0, &ind);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not bind first parameter";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        SQLRETURN exec1 = SQLExecute(stmt.get_handle());
        
        // Close cursor if needed
        SQLCloseCursor(stmt.get_handle());
        
        // Rebind with different value and execute again
        param_val = 2;
        SQLRETURN exec2 = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "First execute: " << exec1 << "; Rebind + second execute: " << exec2;
        result.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Neither execution succeeded; driver may not support parameterized queries";
        } else if (SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            result.status = TestStatus::FAIL;
            result.suggestion = "Second execute after rebind should succeed if first did";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

// ── Numeric-C → character-SQL round-trip test ───────────────────────────────
//
// IMPROVEMENT_PLAN.md §1.1. This shape produced the Firebird #161
// silent-corruption bug: SQLBindParameter returned SQL_SUCCESS, SQLExecute
// returned SQL_SUCCESS, but the driver wrote zero or wrong bytes into the
// VARCHAR column's length-prefix region. A test that only checks return
// codes reports PASS; only reading the rows back catches the defect.
TestResult ParameterBindingTests::test_bindparam_int_to_varchar_roundtrip() {
    TestResult result = make_result(
        "test_bindparam_int_to_varchar_roundtrip",
        "SQLBindParameter",
        TestStatus::PASS,
        "INSERT 10 rows binding SQL_C_SLONG into VARCHAR column; "
        "read back rows ORDER BY id; actual values match std::to_string(i) for i=1..10",
        "",
        Severity::CRITICAL,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: numeric C → character SQL conversion, Appendix D"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE ODBC_TEST_ROUNDTRIP";
        result.diagnostic = last_ddl_error_;
        result.suggestion =
            "This test needs DDL + DML privileges. If running against a real "
            "driver, ensure the user can CREATE TABLE and INSERT.";
        result.duration = elapsed();
        return result;
    }

    constexpr int kRowCount = 10;
    bool insert_phase_ok = true;
    int insert_errors = 0;
    std::string first_insert_error;

    try {
        core::OdbcStatement stmt(conn_);
        SQLRETURN rc = SQLPrepare(
            stmt.get_handle(),
            (SQLCHAR*)"INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (?, ?)",
            SQL_NTS);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQLPrepare INSERT returned " + std::to_string(rc);
            result.suggestion =
                "Driver must support parameterised INSERT to exercise this path.";
            drop_roundtrip_table();
            result.duration = elapsed();
            return result;
        }

        SQLINTEGER id_param = 0;
        SQLINTEGER val_param = 0;
        SQLLEN id_ind = 0;
        SQLLEN val_ind = 0;

        rc = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                              SQL_C_SLONG, SQL_INTEGER, 0, 0,
                              &id_param, 0, &id_ind);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual =
                "SQLBindParameter(id, SQL_C_SLONG→SQL_INTEGER) returned " +
                std::to_string(rc);
            drop_roundtrip_table();
            result.duration = elapsed();
            return result;
        }

        // The core bind under test: SQL_C_SLONG → SQL_VARCHAR. The driver
        // must convert the int to a numeric string representation and store
        // it in the VARCHAR column.
        rc = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                              SQL_C_SLONG, SQL_VARCHAR, 32, 0,
                              &val_param, 0, &val_ind);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual =
                "SQLBindParameter(val, SQL_C_SLONG→SQL_VARCHAR) returned " +
                std::to_string(rc);
            result.suggestion =
                "Driver rejected SQL_C_SLONG→SQL_VARCHAR conversion at bind time. "
                "The round-trip cannot be exercised against this driver; skip, "
                "don't fail.";
            drop_roundtrip_table();
            result.duration = elapsed();
            return result;
        }

        for (int i = 1; i <= kRowCount; ++i) {
            id_param = i;
            val_param = i;
            SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
            if (!SQL_SUCCEEDED(exec_rc)) {
                insert_phase_ok = false;
                insert_errors++;
                if (first_insert_error.empty()) {
                    first_insert_error =
                        "SQLExecute for row " + std::to_string(i) +
                        " returned " + std::to_string(exec_rc);
                }
            }
        }
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = std::string("INSERT phase threw: ") + e.what();
        result.diagnostic = e.format_diagnostics();
        drop_roundtrip_table();
        result.duration = elapsed();
        return result;
    }

    if (!insert_phase_ok && insert_errors == kRowCount) {
        // Every insert failed — plausibly a driver that cannot do numeric→char
        // conversion at bind time. Skip rather than fail, but surface the error.
        result.status = TestStatus::SKIP_UNSUPPORTED;
        result.actual = "All " + std::to_string(kRowCount) +
                        " SQLExecute calls failed: " + first_insert_error;
        drop_roundtrip_table();
        result.duration = elapsed();
        return result;
    }

    // Ensure any pending writes are flushed before reading back.
    SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

    RowVerification v = verify_rows_persisted(
        "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kRowCount);

    if (!v.ok) {
        result.status = TestStatus::FAIL;
        result.actual = "verify_rows_persisted failed: " + v.diagnostic +
                        " (COUNT(*)=" + std::to_string(v.actual_count) +
                        ", fetched=" + std::to_string(v.actual_values.size()) + ")";
        result.suggestion =
            "Rows did not persist after SQL_SUCCESS INSERTs — this is the "
            "Firebird #161 silent-corruption shape. Check the driver's "
            "numeric-C → character-SQL conversion on the bind path.";
    } else {
        std::string mismatches;
        for (int i = 0; i < kRowCount; ++i) {
            const auto& actual = v.actual_values[i];
            std::string expected = std::to_string(i + 1);
            if (actual != expected) {
                if (!mismatches.empty()) mismatches += ", ";
                mismatches += "row " + std::to_string(i + 1) + ": expected '" +
                              expected + "' got '" + actual + "'";
            }
        }
        if (mismatches.empty()) {
            if (insert_phase_ok) {
                result.actual = "All " + std::to_string(kRowCount) +
                                " rows round-tripped correctly";
            } else {
                result.actual = "Round-trip succeeded but " +
                                std::to_string(insert_errors) +
                                " INSERTs reported errors (first: " +
                                first_insert_error + ")";
                result.status = TestStatus::FAIL;
                result.severity = Severity::WARNING;
                result.suggestion =
                    "Driver returned errors during execute but data still landed. "
                    "Indicator handling or post-execute state may be inconsistent.";
            }
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "Round-trip value mismatch: " + mismatches;
            result.suggestion =
                "Driver converted SQL_C_SLONG to VARCHAR incorrectly — "
                "numeric-C → character-SQL conversion is broken.";
        }
    }

    drop_roundtrip_table();
    result.duration = elapsed();
    return result;
}

// ── §1.7: SQLDescribeParam reliability probe (VARCHAR shape) ───────────────
//
// IMPROVEMENT_PLAN.md §1.7. Some drivers (e.g., Firebird ≤3.5.0) return
// SQL_ERROR from SQLDescribeParam. Scanner-style consumers
// (`odbc-scanner::Params::CollectTypes`) need to know per-driver whether
// they can rely on this function at all, or have to fall back to static
// type knowledge.
TestResult ParameterBindingTests::test_sqldescribeparam_varchar() {
    TestResult result = make_result(
        "test_sqldescribeparam_varchar",
        "SQLDescribeParam",
        TestStatus::PASS,
        "After PREPARE on `INSERT INTO t (varchar_col) VALUES (?)`, "
        "SQLDescribeParam returns the column type and column_size",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeParam"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE for SQLDescribeParam probe";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
    }

    SQLSMALLINT param_type = 0;
    SQLULEN col_size = 0;
    SQLSMALLINT scale = 0;
    SQLSMALLINT nullable = 0;
    SQLRETURN describe_rc = SQL_ERROR;

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
            result.duration = elapsed();
            return result;
        }

        // Probe parameter 2 (the VARCHAR column).
        describe_rc = SQLDescribeParam(stmt.get_handle(), 2,
                                       &param_type, &col_size, &scale, &nullable);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        drop_roundtrip_table();
        result.duration = elapsed();
        return result;
    }

    if (!SQL_SUCCEEDED(describe_rc)) {
        result.status = TestStatus::SKIP_UNSUPPORTED;
        result.actual = "SQLDescribeParam returned " + std::to_string(describe_rc);
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
    }

    drop_roundtrip_table();
    result.duration = elapsed();
    return result;
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
    TestResult result = make_result(
        "test_sqlrowcount_after_insert",
        "SQLRowCount",
        TestStatus::PASS,
        "After INSERT INTO t VALUES (?, ?), SQLRowCount returns 1",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE for SQLRowCount probe";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
    }

    try {
        core::OdbcStatement stmt(conn_);
        stmt.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, '1')");
        SQLLEN row_count = -2;
        SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLRowCount returned " + std::to_string(rc);
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
    result.duration = elapsed();
    return result;
}

TestResult ParameterBindingTests::test_sqlrowcount_after_update() {
    TestResult result = make_result(
        "test_sqlrowcount_after_update",
        "SQLRowCount",
        TestStatus::PASS,
        "After UPDATE t SET v=…, SQLRowCount returns the inserted row count",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
    }

    try {
        core::OdbcStatement seed(conn_);
        seed.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, 'a')");
        core::OdbcStatement seed2(conn_);
        seed2.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (2, 'b')");
        core::OdbcStatement seed3(conn_);
        seed3.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (3, 'c')");
        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

        core::OdbcStatement stmt(conn_);
        stmt.execute("UPDATE ODBC_TEST_ROUNDTRIP SET VAL = 'X'");
        SQLLEN row_count = -2;
        SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLRowCount returned " + std::to_string(rc);
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
    result.duration = elapsed();
    return result;
}

TestResult ParameterBindingTests::test_sqlrowcount_after_delete() {
    TestResult result = make_result(
        "test_sqlrowcount_after_delete",
        "SQLRowCount",
        TestStatus::PASS,
        "After DELETE FROM t WHERE …, SQLRowCount returns the matched row count",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLRowCount"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
    }

    try {
        core::OdbcStatement seed(conn_);
        seed.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (1, 'a')");
        core::OdbcStatement seed2(conn_);
        seed2.execute("INSERT INTO ODBC_TEST_ROUNDTRIP (ID, VAL) VALUES (2, 'b')");
        SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

        core::OdbcStatement stmt(conn_);
        stmt.execute("DELETE FROM ODBC_TEST_ROUNDTRIP");
        SQLLEN row_count = -2;
        SQLRETURN rc = SQLRowCount(stmt.get_handle(), &row_count);
        if (!SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLRowCount returned " + std::to_string(rc);
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
    result.duration = elapsed();
    return result;
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
    TestResult result = make_result(
        "test_param_rebind_per_row_row_count",
        "SQLBindParameter",
        TestStatus::PASS,
        "Per-row rebind/execute loop persists every row (Firebird #161 shape)",
        "",
        Severity::CRITICAL,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter, SQLFreeStmt(SQL_RESET_PARAMS)"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
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
            result.duration = elapsed();
            return result;
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
        result.duration = elapsed();
        return result;
    }

    SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

    RowVerification v = verify_rows_persisted(
        "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kRowCount);

    if (!v.ok) {
        result.status = TestStatus::FAIL;
        std::ostringstream actual;
        actual << v.diagnostic
               << " (count=" << v.actual_count
               << ", fetched_rows=" << v.actual_values.size()
               << ", execute_errors=" << execute_errors << ")";
        result.actual = actual.str();
        if (!first_error.empty()) result.diagnostic = first_error;
        result.suggestion =
            "Per-row rebind+execute lost rows — this is the Firebird #161 / "
            "MySQL/MSSQL silent-corruption shape. Driver's parameter-binding "
            "path is broken. Application-side workaround: bind once and reuse "
            "the buffer instead of re-binding per row.";
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
    result.duration = elapsed();
    return result;
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
    TestResult result = make_result(
        "test_param_bind_once_execute_many_row_count",
        "SQLBindParameter+SQLExecute",
        TestStatus::PASS,
        "Bind once, mutate variable, execute N times — all rows persist",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter (parameter values persist across executions)"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
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
            result.duration = elapsed();
            return result;
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
            result.duration = elapsed();
            return result;
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
        result.duration = elapsed();
        return result;
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
    result.duration = elapsed();
    return result;
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
    TestResult result = make_result(
        "test_param_batch_then_single_row_tail",
        "SQLPrepare+SQLBindParameter+SQLExecute",
        TestStatus::PASS,
        "PREPARE multi-row INSERT batch + PREPARE single-row tail; "
        "all rows persist",
        "",
        Severity::WARNING,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, SQLBindParameter (re-prepare on same handle)"
    );

    auto start_time = std::chrono::high_resolution_clock::now();
    auto elapsed = [&]() {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start_time);
    };

    if (!create_roundtrip_table()) {
        result.status = TestStatus::SKIP_INCONCLUSIVE;
        result.actual = "Could not CREATE TABLE";
        result.diagnostic = last_ddl_error_;
        result.duration = elapsed();
        return result;
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
            result.duration = elapsed();
            return result;
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
                result.duration = elapsed();
                return result;
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
            result.duration = elapsed();
            return result;
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
        result.duration = elapsed();
        return result;
    }

    SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);

    RowVerification v = verify_rows_persisted(
        "ODBC_TEST_ROUNDTRIP", "ID", "VAL", kTotalRows);

    if (!v.ok) {
        result.status = TestStatus::FAIL;
        std::ostringstream actual;
        actual << "batch_executed=" << batch_executed
               << " tail_executed=" << tail_executed
               << " count=" << v.actual_count
               << " expected=" << kTotalRows
               << " verify_diag=" << v.diagnostic;
        result.actual = actual.str();
        if (!failure_diag.empty()) result.diagnostic = failure_diag;
        result.suggestion =
            "Batch-then-tail INSERT lost rows. The most common cause is the "
            "driver carrying batch-prepare state into the tail prepare; some "
            "drivers need a fresh statement handle for the tail.";
    } else {
        result.actual = "All " + std::to_string(kTotalRows) +
                        " rows persisted (" + std::to_string(kBatchSize) +
                        " batch + " + std::to_string(kTailSize) + " tail)";
    }

    drop_roundtrip_table();
    result.duration = elapsed();
    return result;
}

} // namespace odbc_crusher::tests
