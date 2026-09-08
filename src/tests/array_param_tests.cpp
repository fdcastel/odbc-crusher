#include "array_param_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

// ── Table lifecycle ──────────────────────────────────────────────────────────

bool ArrayParamTests::create_test_table() {
    // C4 - see TransactionTests::create_test_table. This copy was 96 lines.
    // Its value column is called NAME rather than VAL, which is why the
    // guard takes the column name as a parameter.
    table_.reset();
    table_.emplace(conn_, "ODBC_TEST_ARRAY", "VARCHAR(50)",
                   RoundTripTableGuard::default_id_ddl_variants(), "NAME");
    if (!table_->ok()) {
        last_ddl_error_ = table_->last_error();
        table_.reset();
        return false;
    }
    return true;
}

void ArrayParamTests::drop_test_table() {
    table_.reset();   // C4
}

// ── run() ────────────────────────────────────────────────────────────────────

std::vector<TestResult> ArrayParamTests::run() {
    std::vector<TestResult> results;

    // Create test table once for all array-param tests
    bool table_ok = create_test_table();
    
    // If table creation failed, short-circuit all tests with a clear message
    // explaining the root cause (DDL error) instead of letting each test
    // independently fail with the misleading "Could not prepare parameterized INSERT".
    if (!table_ok) {
        std::string base_msg = "Could not create test table for array parameter tests";
        std::string suggestion = "CREATE TABLE privilege is required on the connected database. ";
        if (!last_ddl_error_.empty()) {
            suggestion += "DDL error: " + last_ddl_error_;
        }
        
        auto make_skip = [&](const char* test_name, const char* function,
                             const char* expected, ConformanceLevel level,
                             const char* spec_ref) -> TestResult {
            TestResult r = make_result(test_name, function,
                TestStatus::SKIP_INCONCLUSIVE, expected, base_msg,
                Severity::INFO, level, spec_ref);
            r.suggestion = suggestion;
            return r;
        };
        
        results.push_back(make_skip("test_column_wise_array_binding",
            "SQLSetStmtAttr/SQLBindParameter/SQLExecute",
            "Column-wise array binding with PARAMSET_SIZE=3 executes successfully",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Arrays of Parameter Values: Column-wise binding"));
        results.push_back(make_skip("test_row_wise_array_binding",
            "SQLSetStmtAttr/SQLBindParameter/SQLExecute",
            "Row-wise array binding with struct layout executes successfully",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Arrays of Parameter Values: Row-wise binding"));
        results.push_back(make_skip("test_param_status_array",
            "SQLSetStmtAttr/SQLExecute",
            "SQL_ATTR_PARAM_STATUS_PTR is populated with SQL_PARAM_SUCCESS for each row",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Using Arrays of Parameters: Parameter status array"));
        results.push_back(make_skip("test_params_processed_count",
            "SQLSetStmtAttr/SQLExecute",
            "SQL_ATTR_PARAMS_PROCESSED_PTR reports correct count after array execution",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Using Arrays of Parameters: SQL_ATTR_PARAMS_PROCESSED_PTR"));
        results.push_back(make_skip("test_array_with_null_values",
            "SQLBindParameter/SQLExecute",
            "Array binding with SQL_NULL_DATA indicators in some rows executes successfully",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Arrays of Parameter Values: NULL indicators in arrays"));
        results.push_back(make_skip("test_param_operation_array",
            "SQLSetStmtAttr/SQLExecute",
            "SQL_ATTR_PARAM_OPERATION_PTR skips rows marked SQL_PARAM_IGNORE, status=SQL_PARAM_UNUSED",
            ConformanceLevel::LEVEL_1,
            "ODBC 3.x Using Arrays of Parameters: SQL_ATTR_PARAM_OPERATION_PTR"));
        results.push_back(make_skip("test_paramset_size_one",
            "SQLSetStmtAttr/SQLExecute",
            "SQL_ATTR_PARAMSET_SIZE=1 behaves like normal single-parameter execution",
            ConformanceLevel::CORE,
            "ODBC 3.x SQLSetStmtAttr: SQL_ATTR_PARAMSET_SIZE default is 1"));
        
        return results;
    }
    results.push_back(test_column_wise_array_binding());
    results.push_back(test_row_wise_array_binding());
    results.push_back(test_param_status_array());
    results.push_back(test_params_processed_count());
    results.push_back(test_array_with_null_values());
    results.push_back(test_param_operation_array());
    results.push_back(test_paramset_size_one());
    results.push_back(test_param_status_per_row_partial_failure());
    results.push_back(test_paramset_size_unsupported_returns_error());

    // Cleanup
    if (table_ok) drop_test_table();

    return results;
}

// ── Test 1: Column-Wise Array Binding ────────────────────────────────────────
// D66: set one statement attribute and report if the driver will not take it.
//
// The array probes set the attributes their assertions depend on and threw
// the return code away - 21 of them. SQL_ATTR_PARAM_STATUS_PTR is the
// sharpest case: a probe that sets it, does not check, and then reads the
// array it passed is reading *its own initialisation* and grading the driver
// on it. If the attribute was refused the array is untouched, and `{}` or
// `0xFFFF` is what gets reported.
//
// Same family as D29 (the SQL_C_NUMERIC ARD fields nothing checked) and C11
// (the same helper re-implemented inline with its return codes dropped).
bool ArrayParamTests::set_stmt_attr_or_skip(core::OdbcStatement& stmt,
                                            TestResult& r, SQLINTEGER attr,
                                            SQLPOINTER value,
                                            const char* attr_name) {
    SQLRETURN ret = SQLSetStmtAttr(stmt.get_handle(), attr, value, 0);
    if (SQL_SUCCEEDED(ret)) return true;
    r.status = TestStatus::SKIP_INCONCLUSIVE;
    r.actual = std::string("Driver refused SQLSetStmtAttr(") + attr_name +
               "), so this probe has nothing to assert about";
    r.suggestion = std::string("A driver claiming array-parameter support "
                               "must accept ") + attr_name;
    return false;
}

// C9: the two attributes that make an array execution an array execution.
//
// These were set inline at a dozen sites and the return code discarded at
// every one but the first. They are not decoration: if a driver refuses
// SQL_ATTR_PARAMSET_SIZE and the probe carries on, it executes a single row
// while asserting about many, and reports PASS for a driver that has no
// array support at all. That is the D29 shape - a probe passing because
// nothing checked whether the thing it depends on was accepted.
//
// Returns false with `r` already filled in, so a caller can `return`.
bool ArrayParamTests::configure_array_exec(core::OdbcStatement& stmt,
                                           TestResult& r,
                                           SQLULEN paramset_size) {
    SQLRETURN ret = SQLSetStmtAttr(
        stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
        reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN), 0);
    if (!SQL_SUCCEEDED(ret)) {
        r.status = TestStatus::SKIP_INCONCLUSIVE;
        r.actual = "SQLSetStmtAttr(SQL_ATTR_PARAM_BIND_TYPE) failed";
        return false;
    }

    ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                         reinterpret_cast<SQLPOINTER>(paramset_size), 0);
    if (!SQL_SUCCEEDED(ret)) {
        // B3: an optional attribute, so SKIP is usually right - but only
        // when the driver actually says "not implemented".
        report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                       "SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE > 1)");
        r.suggestion = "Implement SQL_ATTR_PARAMSET_SIZE support per ODBC 3.x "
                       "spec §Arrays of Parameters";
        return false;
    }
    return true;
}

TestResult ArrayParamTests::test_column_wise_array_binding() {
    return run_test(
        "test_column_wise_array_binding", "SQLSetStmtAttr/SQLBindParameter/SQLExecute",
        "Column-wise array binding with PARAMSET_SIZE=3 executes successfully",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Arrays of Parameter Values: Column-wise binding",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN ARRAY_SIZE = 3;
        
        // Prepare an INSERT statement
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare parameterized INSERT";
            return;
        }
        
        // Set column-wise binding (default, but explicit)
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
            reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN), 0);
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "SQLSetStmtAttr(SQL_ATTR_PARAM_BIND_TYPE) failed";
            return;
        }
        
        // Set paramset size
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(ARRAY_SIZE), 0);
        if (!SQL_SUCCEEDED(ret)) {
            // B3: optional attributes, so SKIP is usually right - but
            // only when the driver actually says "not implemented".
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLSetStmtAttr(SQL_ATTR_PARAMSET_SIZE > 1)");
            r.suggestion = "Implement SQL_ATTR_PARAMSET_SIZE support per ODBC 3.x spec §Arrays of Parameters";
            return;
        }
        
        // Bind integer array (column-wise: array of SQLINTEGER)
        SQLINTEGER id_array[ARRAY_SIZE] = {100, 200, 300};
        SQLLEN id_ind_array[ARRAY_SIZE] = {0, 0, 0};
        
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, id_array, 0, id_ind_array);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not bind integer array parameter";
            return;
        }
        
        // Bind string array (column-wise: 2D char array)
        constexpr int NAME_LEN = 51;
        char name_array[ARRAY_SIZE][NAME_LEN];
        SQLLEN name_ind_array[ARRAY_SIZE];
        
        std::strncpy(name_array[0], "Alice", NAME_LEN);
        std::strncpy(name_array[1], "Bob", NAME_LEN);
        std::strncpy(name_array[2], "Charlie", NAME_LEN);
        name_ind_array[0] = SQL_NTS;
        name_ind_array[1] = SQL_NTS;
        name_ind_array[2] = SQL_NTS;
        
        ret = SQLBindParameter(stmt.get_handle(), 2,
            SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            NAME_LEN - 1, 0, name_array, NAME_LEN, name_ind_array);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not bind string array parameter";
            return;
        }
        
        // Execute with array parameters
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        if (!SQL_SUCCEEDED(exec_ret)) {
            actual << "Array execution returned " << exec_ret;
            r.status = TestStatus::FAIL;
            r.suggestion = "Driver should execute the statement once per parameter set "
                               "when SQL_ATTR_PARAMSET_SIZE > 1. Per ODBC spec, drivers can "
                               "emulate this by executing the SQL once per parameter set.";
            r.actual = actual.str();
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            return;
        }

        actual << "Array execution with PARAMSET_SIZE=" << ARRAY_SIZE
               << " succeeded (ret=" << exec_ret << ")";

        // A20: the probe used to stop here, having looked only at exec_ret.
        // A driver that ran one parameter set instead of three, or wrote
        // "Alice" three times, or advanced the integer array correctly while
        // reusing the first string, passed all the same - and the string axis
        // is exactly what this file's own header warns about, because a
        // char[3][N] array is where row-wise offset arithmetic goes wrong.
        // Read the rows back and compare both columns.
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);

        RowVerification v = verify_rows_persisted(
            "ODBC_TEST_ARRAY", "ID", "NAME",
            static_cast<long>(ARRAY_SIZE));
        if (!v.ok) {
            r.status = TestStatus::FAIL;
            r.severity = Severity::CRITICAL;
            r.actual = actual.str() + ", but the rows are not there: " +
                       v.diagnostic;
            r.suggestion =
                "SQLExecute with SQL_ATTR_PARAMSET_SIZE = 3 must insert three "
                "rows. Returning success and inserting fewer is the silent "
                "shape this category exists to find.";
            return;
        }

        static const char* const kExpected[] = {"Alice", "Bob", "Charlie"};
        std::string mismatches;
        for (size_t i = 0; i < ARRAY_SIZE; ++i) {
            const std::string got = v.display(i);
            // CHAR-style padding is the engine's business; compare trimmed.
            std::string trimmed = got;
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
            if (trimmed != kExpected[i]) {
                if (!mismatches.empty()) mismatches += ", ";
                mismatches += "row " + std::to_string(i + 1) + ": expected '" +
                              kExpected[i] + "' got '" + got + "'";
            }
        }
        if (!mismatches.empty()) {
            r.status = TestStatus::FAIL;
            r.severity = Severity::CRITICAL;
            r.actual = actual.str() + "; string axis wrong: " + mismatches;
            r.suggestion =
                "Each parameter set must take the next element of the bound "
                "character array. Repeating one element is the classic "
                "column-wise offset bug, and it corrupts data without "
                "failing a single call.";
            return;
        }

        actual << "; all " << ARRAY_SIZE
               << " rows persisted with the right strings";
        r.actual = actual.str();
        // The paramset reset already happened above, before the rows
        // were read back - it has to, or verify_rows_persisted runs with
        // PARAMSET_SIZE still at 3.
        });
}

// ── Test 2: Row-Wise Array Binding ───────────────────────────────────────────
TestResult ArrayParamTests::test_row_wise_array_binding() {
    return run_test(
        "test_row_wise_array_binding", "SQLSetStmtAttr/SQLBindParameter/SQLExecute",
        "Row-wise array binding with struct layout executes successfully",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Arrays of Parameter Values: Row-wise binding",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        
        // Define row structure
        struct ParamRow {
            SQLINTEGER id;
            SQLLEN id_ind;
            char name[51];
            SQLLEN name_ind;
        };
        
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare parameterized INSERT";
            return;
        }
        
        // Set row-wise binding: structure size
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(sizeof(ParamRow))), 0);
        if (!SQL_SUCCEEDED(ret)) {
            // B3: optional attributes, so SKIP is usually right - but
            // only when the driver actually says "not implemented".
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLSetStmtAttr(SQL_ATTR_PARAM_BIND_TYPE)");
            r.suggestion = "Implement SQL_ATTR_PARAM_BIND_TYPE per ODBC 3.x spec §Binding Arrays of Parameters";
            return;
        }
        
        // ── Safety probe ──
        // Some drivers (e.g. DuckDB) accept SQL_ATTR_PARAM_BIND_TYPE but
        // do not implement row-wise pointer arithmetic correctly.  Using
        // PARAMSET_SIZE > 1 with such a driver causes the driver to read
        // data from wrong offsets, corrupting memory and crashing the
        // process with a __fastfail (uncatchable by SEH).
        //
        // Strategy: first execute with PARAMSET_SIZE=1 (safe — offset 0
        // is always correct) using only a single integer parameter.
        // Then verify the inserted value.  If the single-row probe passes,
        // try PARAMSET_SIZE=2 with integer-only parameters (no strings,
        // since string offset errors cause buffer overruns).  Finally,
        // verify both rows.
        
        // Probe step 1: Single-row execute with row-wise binding
        {
            core::OdbcStatement probe_stmt(conn_);
            SQLPrepareW(probe_stmt.get_handle(),
                SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)").ptr(), SQL_NTS);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(sizeof(ParamRow))), 0);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            
            ParamRow single = {9990, 0, "", 0};
            SQLBindParameter(probe_stmt.get_handle(), 1,
                SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                0, 0, &single.id, 0, &single.id_ind);
            
            SQLRETURN probe_ret = SQLExecute(probe_stmt.get_handle());
            
            // Reset immediately
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
                reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN), 0);
            
            if (!SQL_SUCCEEDED(probe_ret)) {
                r.status = TestStatus::FAIL;
                r.actual = "Row-wise binding with PARAMSET_SIZE=1 failed (ret="
                    + std::to_string(probe_ret) + ")";
                r.suggestion = "Even with PARAMSET_SIZE=1, row-wise binding "
                    "should work identically to column-wise";                return;
            }
        }
        
        // Probe step 2: Two-row execute with integer-only (no strings)
        // to verify the driver correctly offsets by struct size.
        {
            core::OdbcStatement probe_stmt(conn_);
            SQLPrepareW(probe_stmt.get_handle(),
                SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)").ptr(), SQL_NTS);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(sizeof(ParamRow))), 0);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(2)), 0);
            
            ParamRow two_rows[2] = {{9991, 0, "", 0}, {9992, 0, "", 0}};
            SQLBindParameter(probe_stmt.get_handle(), 1,
                SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                0, 0, &two_rows[0].id, 0, &two_rows[0].id_ind);
            
            SQLRETURN probe_ret = SQLExecute(probe_stmt.get_handle());
            
            // Reset immediately
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            SQLSetStmtAttr(probe_stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
                reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN), 0);
            
            if (!SQL_SUCCEEDED(probe_ret)) {
                r.status = TestStatus::FAIL;
                r.actual = "Row-wise binding with PARAMSET_SIZE=2 (integer-only) failed";
                r.suggestion = "Row-wise binding should offset each row by "
                    "SQL_ATTR_PARAM_BIND_TYPE bytes. The driver may ignore the bind type.";                return;
            }
            
            // Verify: the driver should have inserted 9991 and 9992.
            // If it inserted 9991 twice, row-wise arithmetic is broken.
            core::OdbcStatement verify(conn_);
            verify.execute("SELECT ID FROM ODBC_TEST_ARRAY WHERE ID IN (9991, 9992) ORDER BY ID");
            
            std::vector<SQLINTEGER> ids;
            SQLINTEGER id_buf = 0;
            SQLLEN id_ind = 0;
            SQLBindCol(verify.get_handle(), 1, SQL_C_SLONG, &id_buf, 0, &id_ind);
            while (SQL_SUCCEEDED(SQLFetch(verify.get_handle()))) {
                ids.push_back(id_buf);
            }
            
            if (ids.size() != 2 || ids[0] != 9991 || ids[1] != 9992) {
                r.status = TestStatus::FAIL;
                std::ostringstream actual;
                actual << "Row-wise binding inserts wrong data: got {";
                for (size_t i = 0; i < ids.size(); ++i) {
                    if (i > 0) actual << ", ";
                    actual << ids[i];
                }
                actual << "} instead of {9991, 9992}. Driver stores "
                       "SQL_ATTR_PARAM_BIND_TYPE but does not use it for "
                       "pointer arithmetic.";
                r.actual = actual.str();
                r.suggestion = "The driver's parameter value reader must offset "
                    "by SQL_ATTR_PARAM_BIND_TYPE bytes per row, not by column size. "
                    "Executing with string parameters in this state would cause "
                    "memory corruption and a process crash.";                return;
            }
        }
        
        // Full test: now that we've verified integer row-wise works, test
        // with both integer and string parameters (PARAMSET_SIZE=3).
        {
            constexpr SQLULEN ARRAY_SIZE_FULL = 3;
            
            // Set paramset size
            ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(ARRAY_SIZE_FULL), 0);
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver does not support SQL_ATTR_PARAMSET_SIZE > 1";                return;
            }
            
            // Populate row array
            ParamRow rows[ARRAY_SIZE_FULL];
            rows[0] = {400, 0, "Dave", SQL_NTS};
            rows[1] = {500, 0, "Eve", SQL_NTS};
            rows[2] = {600, 0, "Frank", SQL_NTS};
            
            // Bind parameter 1 (ID)
            ret = SQLBindParameter(stmt.get_handle(), 1,
                SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                0, 0, &rows[0].id, 0, &rows[0].id_ind);
            
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not bind row-wise integer parameter";                return;
            }
            
            // Bind parameter 2 (NAME)
            ret = SQLBindParameter(stmt.get_handle(), 2,
                SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                50, 0, rows[0].name, sizeof(rows[0].name), &rows[0].name_ind);
            
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not bind row-wise string parameter";                return;
            }
            
            // Execute with array parameters
            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
            
            std::ostringstream actual;
            if (SQL_SUCCEEDED(exec_ret)) {
                actual << "Row-wise array execution with PARAMSET_SIZE="
                       << ARRAY_SIZE_FULL << " succeeded (ret=" << exec_ret << ")";
            } else {
                actual << "Row-wise array execution returned " << exec_ret;
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver should support row-wise parameter binding via "
                                   "SQL_ATTR_PARAM_BIND_TYPE = sizeof(struct). The driver "
                                   "calculates each row's address as: "
                                   "base + row_number * struct_size.";
            }
            r.actual = actual.str();
            
            // Reset
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_BIND_TYPE,
                reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN), 0);
        }
        });
}

// ── Test 3: Parameter Status Array ───────────────────────────────────────────
TestResult ArrayParamTests::test_param_status_array() {
    return run_test(
        "test_param_status_array", "SQLSetStmtAttr/SQLExecute",
        "SQL_ATTR_PARAM_STATUS_PTR is populated with SQL_PARAM_SUCCESS for each row",
        Severity::WARNING, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Using Arrays of Parameters: Parameter status array",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN ARRAY_SIZE = 3;
        
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare statement";
            return;
        }
        
        // Set up array parameters
        if (!configure_array_exec(stmt, r, ARRAY_SIZE)) return;
        
        // Set up status array
        SQLUSMALLINT status_array[ARRAY_SIZE];
        // Initialize to a known value to detect whether driver wrote to it
        for (SQLULEN i = 0; i < ARRAY_SIZE; ++i) {
            status_array[i] = 0xFFFF;
        }
        
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR,
            status_array, 0);
        if (!SQL_SUCCEEDED(ret)) {
            // B3: optional attributes, so SKIP is usually right - but
            // only when the driver actually says "not implemented".
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLSetStmtAttr(SQL_ATTR_PARAM_STATUS_PTR)");
            r.suggestion = "Implement SQL_ATTR_PARAM_STATUS_PTR to report per-row status. "
                               "Per ODBC 3.x, the driver fills this array with SQL_PARAM_SUCCESS, "
                               "SQL_PARAM_ERROR, etc. after execution.";
            return;
        }
        
        // Bind parameters
        SQLINTEGER id_array[ARRAY_SIZE] = {700, 800, 900};
        SQLLEN id_ind[ARRAY_SIZE] = {0, 0, 0};
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, id_array, 0, id_ind);
        
        char name_array[ARRAY_SIZE][51] = {"Alpha", "Beta", "Gamma"};
        SQLLEN name_ind[ARRAY_SIZE] = {SQL_NTS, SQL_NTS, SQL_NTS};
        SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            50, 0, name_array, 51, name_ind);
        
        // Execute
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "Execute returned " << exec_ret << "; status array: [";
        
        bool all_success = true;
        for (SQLULEN i = 0; i < ARRAY_SIZE; ++i) {
            if (i > 0) actual << ", ";
            switch (status_array[i]) {
                case SQL_PARAM_SUCCESS: actual << "SUCCESS"; break;
                case SQL_PARAM_SUCCESS_WITH_INFO: actual << "SUCCESS_WITH_INFO"; break;
                case SQL_PARAM_ERROR: actual << "ERROR"; all_success = false; break;
                case SQL_PARAM_UNUSED: actual << "UNUSED"; break;
                case SQL_PARAM_DIAG_UNAVAILABLE: actual << "DIAG_UNAVAILABLE"; break;
                default: actual << "0x" << std::hex << status_array[i] << std::dec;
                         all_success = false; break;
            }
        }
        actual << "]";
        r.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec_ret)) {
            r.status = TestStatus::FAIL;
            r.suggestion = "Array execution should succeed for valid parameter sets";
        } else if (!all_success) {
            r.status = TestStatus::FAIL;
            r.suggestion = "All status entries should be SQL_PARAM_SUCCESS when no errors occur";
        }
        
        // Reset
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);
        });
}

// ── Test 4: Params Processed Count ───────────────────────────────────────────
TestResult ArrayParamTests::test_params_processed_count() {
    return run_test(
        "test_params_processed_count", "SQLSetStmtAttr/SQLExecute",
        "SQL_ATTR_PARAMS_PROCESSED_PTR reports correct count after array execution",
        Severity::WARNING, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Using Arrays of Parameters: SQL_ATTR_PARAMS_PROCESSED_PTR",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN ARRAY_SIZE = 4;
        
        // Prepare
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare statement";
            return;
        }
        
        // Configure array execution
        if (!configure_array_exec(stmt, r, ARRAY_SIZE)) return;
        
        // Set params processed pointer
        SQLULEN params_processed = 0;
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR,
            &params_processed, 0);
        if (!SQL_SUCCEEDED(ret)) {
            // B3: optional attributes, so SKIP is usually right - but
            // only when the driver actually says "not implemented".
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLSetStmtAttr(SQL_ATTR_PARAMS_PROCESSED_PTR)");
            r.suggestion = "Implement SQL_ATTR_PARAMS_PROCESSED_PTR per ODBC 3.x spec. "
                               "The driver must set this to the number of parameter sets processed.";
            return;
        }
        
        // Bind parameter array
        SQLINTEGER id_array[ARRAY_SIZE] = {1000, 2000, 3000, 4000};
        SQLLEN id_ind[ARRAY_SIZE] = {0, 0, 0, 0};
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, id_array, 0, id_ind);
        
        // Execute
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "Execute returned " << exec_ret 
               << "; params_processed=" << params_processed
               << " (expected " << ARRAY_SIZE << ")";
        r.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec_ret)) {
            r.status = TestStatus::FAIL;
            r.suggestion = "Array execution should succeed";
        } else if (params_processed != ARRAY_SIZE) {
            r.status = TestStatus::FAIL;
            r.suggestion = "SQL_ATTR_PARAMS_PROCESSED_PTR must report the total number "
                               "of parameter sets processed (including error sets). "
                               "Expected " + std::to_string(ARRAY_SIZE) + " but got " + 
                               std::to_string(params_processed);
        }
        
        // Reset
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR, nullptr, 0);
        });
}

// ── Test 5: Array with NULL Values ───────────────────────────────────────────
TestResult ArrayParamTests::test_array_with_null_values() {
    return run_test(
        "test_array_with_null_values", "SQLBindParameter/SQLExecute",
        "Array binding with SQL_NULL_DATA indicators in some rows executes successfully",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Arrays of Parameter Values: NULL indicators in arrays",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN ARRAY_SIZE = 3;
        
        // Prepare
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare statement";
            return;
        }
        
        // Configure
        if (!configure_array_exec(stmt, r, ARRAY_SIZE)) return;
        
        SQLUSMALLINT status_array[ARRAY_SIZE] = {};
        if (!set_stmt_attr_or_skip(stmt, r, SQL_ATTR_PARAM_STATUS_PTR,
                                   status_array, "SQL_ATTR_PARAM_STATUS_PTR")) return;
        
        // Bind integer array — row 1 is NULL
        SQLINTEGER id_array[ARRAY_SIZE] = {100, 200, 300};
        SQLLEN id_ind[ARRAY_SIZE] = {0, SQL_NULL_DATA, 0};
        
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, id_array, 0, id_ind);
        
        // Bind string array — all non-NULL
        char name_array[ARRAY_SIZE][51] = {"NullTest1", "NullTest2", "NullTest3"};
        SQLLEN name_ind[ARRAY_SIZE] = {SQL_NTS, SQL_NTS, SQL_NTS};
        
        SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            50, 0, name_array, 51, name_ind);
        
        // Execute
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(exec_ret)) {
            actual << "Array execution with NULL in row 1 succeeded (ret=" << exec_ret << ")";
        } else {
            actual << "Array execution with NULL returned " << exec_ret;
            // B1: the excuse was "some drivers may reject NULLs depending on
            // column constraints" - but this probe creates its own table,
            // whose NAME column has no NOT NULL constraint, so there is no
            // constraint to blame. A driver that cannot insert SQL_NULL_DATA
            // through an array-bound parameter has a real defect, and the
            // SQLSTATE says which: 23000 would be an actual constraint
            // violation and still deserves a skip.
            const std::string state = first_sqlstate(
                SQL_HANDLE_STMT, stmt.get_handle(), "no diagnostic");
            actual << " [" << state << "]";
            if (state == "23000") {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "Integrity constraint violation - the test "
                               "table appears to disallow NULL in this column.";
            } else {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion =
                    "SQL_NULL_DATA in a bound parameter array must insert "
                    "SQL NULL. The column this probe creates is nullable, so "
                    "there is no constraint to explain the rejection.";
            }
        }
        r.actual = actual.str();
        
        // Reset
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);
        });
}

// ── Test 6: Parameter Operation Array (SQL_PARAM_IGNORE) ─────────────────────
TestResult ArrayParamTests::test_param_operation_array() {
    return run_test(
        "test_param_operation_array", "SQLSetStmtAttr/SQLExecute",
        "SQL_ATTR_PARAM_OPERATION_PTR skips rows marked SQL_PARAM_IGNORE, status=SQL_PARAM_UNUSED",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Using Arrays of Parameters: SQL_ATTR_PARAM_OPERATION_PTR",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN ARRAY_SIZE = 4;
        
        // Prepare
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare statement";
            return;
        }
        
        // Configure array execution
        if (!configure_array_exec(stmt, r, ARRAY_SIZE)) return;
        
        // Set up operation array: skip rows 1 and 3 (0-indexed)
        SQLUSMALLINT operation_array[ARRAY_SIZE] = {
            SQL_PARAM_PROCEED, SQL_PARAM_IGNORE, SQL_PARAM_PROCEED, SQL_PARAM_IGNORE
        };
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_OPERATION_PTR,
            operation_array, 0);
        if (!SQL_SUCCEEDED(ret)) {
            // B3: optional attributes, so SKIP is usually right - but
            // only when the driver actually says "not implemented".
            report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                           "SQLSetStmtAttr(SQL_ATTR_PARAM_OPERATION_PTR)");
            r.suggestion = "Implement SQL_ATTR_PARAM_OPERATION_PTR per ODBC 3.x spec. "
                               "This allows applications to skip specific parameter sets.";
            return;
        }
        
        // Set up status array to check results
        SQLUSMALLINT status_array[ARRAY_SIZE];
        for (SQLULEN i = 0; i < ARRAY_SIZE; ++i) status_array[i] = 0xFFFF;
        if (!set_stmt_attr_or_skip(stmt, r, SQL_ATTR_PARAM_STATUS_PTR,
                                   status_array, "SQL_ATTR_PARAM_STATUS_PTR")) return;
        
        SQLULEN params_processed = 0;
        if (!set_stmt_attr_or_skip(stmt, r, SQL_ATTR_PARAMS_PROCESSED_PTR,
                                   &params_processed,
                                   "SQL_ATTR_PARAMS_PROCESSED_PTR")) return;
        
        // Bind parameter array
        SQLINTEGER id_array[ARRAY_SIZE] = {10, 20, 30, 40};
        SQLLEN id_ind[ARRAY_SIZE] = {0, 0, 0, 0};
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, id_array, 0, id_ind);
        
        // Execute
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "Execute returned " << exec_ret << "; params_processed=" << params_processed
               << "; status: [";
        
        bool correct = true;
        for (SQLULEN i = 0; i < ARRAY_SIZE; ++i) {
            if (i > 0) actual << ", ";
            switch (status_array[i]) {
                case SQL_PARAM_SUCCESS: actual << "SUCCESS"; break;
                case SQL_PARAM_UNUSED: actual << "UNUSED"; break;
                case SQL_PARAM_ERROR: actual << "ERROR"; break;
                default: actual << "0x" << std::hex << status_array[i] << std::dec; break;
            }
            
            // Rows 1 and 3 should be UNUSED (they were IGNORED)
            if ((i == 1 || i == 3) && status_array[i] != SQL_PARAM_UNUSED) {
                correct = false;
            }
            // Rows 0 and 2 should be SUCCESS
            if ((i == 0 || i == 2) && status_array[i] != SQL_PARAM_SUCCESS) {
                correct = false;
            }
        }
        actual << "]";
        r.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec_ret)) {
            r.status = TestStatus::FAIL;
            r.suggestion = "Array execution with IGNORE rows should still succeed for non-ignored rows";
        } else if (!correct) {
            r.status = TestStatus::FAIL;
            r.suggestion = "Ignored rows must have status SQL_PARAM_UNUSED, "
                               "executed rows must have status SQL_PARAM_SUCCESS";
        }
        
        // Reset
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR, nullptr, 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_OPERATION_PTR, nullptr, 0);
        });
}

// ── Test 7: PARAMSET_SIZE=1 (Normal Execution) ──────────────────────────────
TestResult ArrayParamTests::test_paramset_size_one() {
    return run_test(
        "test_paramset_size_one", "SQLSetStmtAttr/SQLExecute",
        "SQL_ATTR_PARAMSET_SIZE=1 behaves like normal single-parameter execution",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.x SQLSetStmtAttr: SQL_ATTR_PARAMSET_SIZE default is 1",
        [&](TestResult& r) {
        core::OdbcStatement stmt(conn_);
        
        // Prepare
        SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
            SqlWcharBuf("INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)").ptr(), SQL_NTS);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not prepare statement";
            return;
        }
        
        // Explicitly set PARAMSET_SIZE = 1
        ret = SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
            reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        
        if (!SQL_SUCCEEDED(ret)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not set SQL_ATTR_PARAMSET_SIZE to 1";
            return;
        }
        
        // Set up status/processed pointers.
        //
        // A8: both return codes used to be discarded, and the assertions below
        // then FAILed on the sentinel values a driver leaves behind when it
        // declines the attribute — processed stays 0, status stays 0xFFFF. Both
        // attributes are optional Level 1, so that was a guaranteed FAIL at
        // Core for a perfectly correct driver. The sibling probe 90 lines up
        // checks the same return code and SKIPs; the two disagreed.
        SQLUSMALLINT status = 0xFFFF;
        SQLULEN processed = 0;
        const bool status_ptr_ok = SQL_SUCCEEDED(SQLSetStmtAttr(
            stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, &status, 0));
        const bool processed_ptr_ok = SQL_SUCCEEDED(SQLSetStmtAttr(
            stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0));
        
        // Bind single parameter
        SQLINTEGER id_val = 999;
        SQLLEN id_ind = 0;
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, &id_val, 0, &id_ind);
        
        // Execute
        SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "Execute returned " << exec_ret;
        if (processed_ptr_ok) actual << "; processed=" << processed;
        else                  actual << "; PARAMS_PROCESSED_PTR not supported";
        if (status_ptr_ok)    actual << "; status=" << status;
        else                  actual << "; PARAM_STATUS_PTR not supported";
        r.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec_ret)) {
            r.status = TestStatus::FAIL;
            r.suggestion = "PARAMSET_SIZE=1 should execute normally";
        } else if (!processed_ptr_ok && !status_ptr_ok) {
            // A8: nothing to verify. Execute worked, which is the part that is
            // required at Core; the outputs this probe exists to inspect are
            // both optional and both declined.
            r.status = TestStatus::SKIP_UNSUPPORTED;
            r.actual = "Execute succeeded, but the driver accepts neither "
                       "SQL_ATTR_PARAMS_PROCESSED_PTR nor SQL_ATTR_PARAM_STATUS_PTR";
            r.suggestion = "Both are optional Level 1 attributes; there is "
                           "nothing to check when a driver declines them.";
        } else if (processed_ptr_ok && processed != 1) {
            r.status = TestStatus::FAIL;
            r.suggestion = "With PARAMSET_SIZE=1, params_processed should be 1";
        } else if (status_ptr_ok && status != SQL_PARAM_SUCCESS) {
            r.status = TestStatus::FAIL;
            r.suggestion = "With PARAMSET_SIZE=1 and successful execution, status should be SQL_PARAM_SUCCESS";
        }
        
        // Reset
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR, nullptr, 0);
        });
}

// ── Test 8: Array Partial Error ──────────────────────────────────────────────
// ── PORT plan §4.6 — driver-detected per-row failure ────────────────────────
//
// Bind a 5-row INSERT batch; expect that any one row violating an integrity
// constraint shows up as SQL_PARAM_ERROR in the per-row status array while
// the surrounding rows show SQL_PARAM_SUCCESS. The existing
// test_param_operation_array uses SQL_PARAM_OPERATION_PTR/IGNORE — that's
// application-driven. This is the driver-driven shape.

TestResult ArrayParamTests::test_param_status_per_row_partial_failure() {
    return run_test(
        "test_param_status_per_row_partial_failure", "SQLSetStmtAttr/SQLExecute",
        "Driver fills per-row SQL_PARAM_STATUS_PTR with SUCCESS for ok rows "
        "and ERROR for the row that violates a server-side constraint",
        Severity::WARNING, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Using Arrays of Parameters: SQL_ATTR_PARAM_STATUS_PTR per-row outcome",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            constexpr SQLULEN kSize = 5;

            const char* sql_a = "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)";
            SQLRETURN ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(sql_a).ptr(), SQL_NTS);
            if (!SQL_SUCCEEDED(ret)) {
                ret = SQLPrepare(stmt.get_handle(),
                    reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql_a)),
                    SQL_NTS);
            }
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not prepare INSERT (W and ANSI both failed; "
                           "DDL setup may have failed)";
                return;
            }

            // D66: this was unchecked while the PARAMSET_SIZE call just
            // below it was checked — the probe already knew the question
            // was worth asking, and asked it of only one of the two.
            if (!set_stmt_attr_or_skip(
                    stmt, r, SQL_ATTR_PARAM_BIND_TYPE,
                    reinterpret_cast<SQLPOINTER>(SQL_PARAM_BIND_BY_COLUMN),
                    "SQL_ATTR_PARAM_BIND_TYPE")) {
                return;
            }
            SQLRETURN ps_ret = SQLSetStmtAttr(stmt.get_handle(),
                SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(kSize), 0);
            if (!SQL_SUCCEEDED(ps_ret)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver rejected SQL_ATTR_PARAMSET_SIZE=" + std::to_string(kSize);
                return;
            }

            SQLUSMALLINT status[kSize];
            for (SQLULEN i = 0; i < kSize; ++i) status[i] = 0xFFFF;
            ret = SQLSetStmtAttr(stmt.get_handle(),
                SQL_ATTR_PARAM_STATUS_PTR, status, 0);
            if (!SQL_SUCCEEDED(ret)) {
                // B3: optional attributes, so SKIP is usually right - but
                // only when the driver actually says "not implemented".
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLSetStmtAttr(SQL_ATTR_PARAM_STATUS_PTR)");
                return;
            }

            SQLINTEGER ids[kSize] = {1001, 1002, 1003, 1004, 1005};
            SQLLEN id_inds[kSize] = {0, 0, 0, 0, 0};
            SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                SQL_C_SLONG, SQL_INTEGER, 0, 0, ids, 0, id_inds);
            char names[kSize][51] = {"row0", "row1", "row2", "row3", "row4"};
            SQLLEN name_inds[kSize] = {SQL_NTS, SQL_NTS, SQL_NTS, SQL_NTS, SQL_NTS};
            SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT,
                SQL_C_CHAR, SQL_VARCHAR, 50, 0, names, 51, name_inds);

            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());

            std::ostringstream actual;
            actual << "Execute rc=" << exec_ret << " status=[";
            int succ = 0, err = 0, other = 0, err_index = -1;
            for (SQLULEN i = 0; i < kSize; ++i) {
                if (i > 0) actual << ", ";
                switch (status[i]) {
                    case SQL_PARAM_SUCCESS:
                    case SQL_PARAM_SUCCESS_WITH_INFO:
                        actual << "OK"; ++succ; break;
                    case SQL_PARAM_ERROR:
                        actual << "ERR"; ++err;
                        if (err_index < 0) err_index = static_cast<int>(i);
                        break;
                    default:
                        actual << "0x" << std::hex << status[i] << std::dec;
                        ++other; break;
                }
            }
            actual << "] (succ=" << succ << " err=" << err << " other=" << other << ")";
            r.actual = actual.str();

            // Reset before any return path so cleanup doesn't trip.
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
            SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);

            // The probe is **informational against drivers** — without an
            // injected failure the all-success outcome is correct. The
            // canary path injects a failure via mock knob; in that scenario
            // err > 0 must hold AND succ > 0 (mixed outcome).
            if (other > 0) {
                r.status = TestStatus::FAIL;
                r.suggestion = "SQL_ATTR_PARAM_STATUS_PTR contains values "
                               "outside the documented set "
                               "{SUCCESS, SUCCESS_WITH_INFO, ERROR, "
                               "UNUSED, DIAG_UNAVAILABLE}.";
                return;
            }
            if (err > 0) {
                // At least one row failed — verify the surrounding rows are
                // still reported as SUCCESS so the per-row contract holds.
                if (succ == 0) {
                    r.status = TestStatus::FAIL;
                    r.suggestion = "Driver reported failure for some rows but "
                                   "marked the rest as ERROR too — per-row "
                                   "contract requires SUCCESS for the rows "
                                   "that did succeed.";
                }
                // else: PASS — mixed outcome reported correctly.
            }
            // err == 0: green-path PASS, the suggestion only fires under
            // the canary scenario.
        });
}

// ── PORT plan §4.6 — paramset-size unsupported / fallback contract ──────────
//
// Some drivers reject SQL_ATTR_PARAMSET_SIZE > 1 entirely (e.g., older Sybase
// and pre-3.5 Firebird). The conformance contract is: return SQL_ERROR with
// SQLSTATE HYC00 ("Optional feature not implemented") so applications can
// fall back to row-by-row execution. Anything else (silent acceptance,
// SQL_SUCCESS while still executing once) is the bug shape this probe
// catches. SKIP_UNSUPPORTED on HYC00 is the correct outcome for a known
// limitation.

TestResult ArrayParamTests::test_paramset_size_unsupported_returns_error() {
    return run_test(
        "test_paramset_size_unsupported_returns_error", "SQLSetStmtAttr",
        "Driver either accepts SQL_ATTR_PARAMSET_SIZE > 1 OR returns HYC00 "
        "with no other side effect",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.x SQL_ATTR_PARAMSET_SIZE — HYC00 fallback contract",
        [&](TestResult& r) {
            // Use a fresh statement with no prepare — SQL_ATTR_PARAMSET_SIZE
            // is settable on any allocated stmt handle, and skipping prepare
            // avoids unixODBC W-path fallout polluting the diagnostic queue.
            core::OdbcStatement stmt(conn_);

            SQLRETURN set_rc = SQLSetStmtAttr(stmt.get_handle(),
                SQL_ATTR_PARAMSET_SIZE,
                reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(10)), 0);

            if (SQL_SUCCEEDED(set_rc)) {
                r.actual = "SQLSetStmtAttr accepted PARAMSET_SIZE=10 — driver "
                           "supports array parameter execution";
                SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                    reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
                return;
            }

            // Failure path — walk the diagnostic queue looking for HYC00.
            // Some driver managers add their own DM-layer diag record (e.g.
            // unixODBC IM006 "could not load driver helper") in front of the
            // driver-emitted one; loop instead of reading record 1 only.
            std::string sqlstate;
            std::string msg_text;
            for (SQLSMALLINT i = 1; i <= 16; ++i) {
                char state[6] = {0};
                SQLINTEGER native = 0;
                SQLCHAR msg[256] = {0};
                SQLSMALLINT msg_len = 0;
                SQLRETURN dr = SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(),
                                             i,
                                             reinterpret_cast<SQLCHAR*>(state),
                                             &native, msg, sizeof(msg), &msg_len);
                if (dr == SQL_NO_DATA || !SQL_SUCCEEDED(dr)) break;
                // D68: the message to `msg_len`. The SQLSTATE beside it has
                // no length parameter at all in SQLGetDiagRec's signature, so
                // it is a different problem - D69.
                if (sqlstate.empty()) {
                    sqlstate = core::sqlstate_string(state);
                    msg_text = core::bounded_string(
                        reinterpret_cast<char*>(msg), sizeof(msg), msg_len).value;
                }
                if (core::sqlstate_string(state) == "HYC00") {
                    sqlstate = "HYC00";
                    msg_text = core::bounded_string(
                        reinterpret_cast<char*>(msg), sizeof(msg), msg_len).value;
                    break;
                }
            }

            std::ostringstream actual;
            actual << "SQLSetStmtAttr returned " << set_rc
                   << " state=" << sqlstate
                   << " msg='" << msg_text << "'";
            r.actual = actual.str();

            if (sqlstate == "HYC00") {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Driver doesn't support array-parameter execution. "
                               "Applications must fall back to row-by-row "
                               "SQLExecute to use this driver.";
                return;
            }
            if (sqlstate.empty()) {
                // Driver-manager layer (e.g. unixODBC on Linux) sometimes
                // intercepts SetStmtAttr and returns SQL_ERROR with NO
                // diagnostic record — the underlying driver's HYC00 never
                // reaches us. Can't draw a conclusion from that.
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SetStmtAttr failed but produced no diagnostic. "
                               "Likely a driver-manager-layer rejection that "
                               "didn't forward the underlying SQLSTATE.";
                return;
            }
            r.status = TestStatus::FAIL;
            r.suggestion = "Failure SQLSTATE should be HYC00 ('Optional feature "
                           "not implemented') so applications recognize the "
                           "fallback case. Got '" + sqlstate + "' instead.";
        });
}

} // namespace odbc_crusher::tests
