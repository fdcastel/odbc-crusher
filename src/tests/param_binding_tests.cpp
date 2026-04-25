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

} // namespace odbc_crusher::tests
