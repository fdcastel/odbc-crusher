#include "array_param_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
#include <cstdio>

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
    // P11 (IMPROVEMENT_PLAN_V2)
    results.push_back(test_array_row_count_matches_its_claim());
    // P12 (IMPROVEMENT_PLAN_V2)
    results.push_back(test_handle_reuse_after_array_error());

    // Cleanup
    if (table_ok) drop_test_table();

    return results;
}

// ── Test 1: Column-Wise Array Binding ────────────────────────────────────────
// C9: the prepare-INSERT prologue, which ten probes opened with.
//
// Eight were the same eight lines. The two in `test_row_wise_binding` were
// not: they discarded the `SQLPrepareW` result entirely, so a driver that
// could not prepare the INSERT was reported as failing *row-wise binding* -
// `"Row-wise binding with PARAMSET_SIZE=1 failed (ret=-1)"` - which is a wrong
// diagnosis, not a missing one. Same family as D68's twelve.
//
// The W-then-ANSI shape comes from the tenth site, which already had it. The
// other nine gave up when `SQLPrepareW` failed, and for an ANSI-only driver
// that is a skip which need not have happened. Consolidating on the better of
// two shapes is the reason to consolidate at all; consolidating on the more
// common one would have spread the weaker behaviour to all ten.
bool ArrayParamTests::prepare_array_insert(core::OdbcStatement& stmt,
                                           TestResult& r, const char* sql) {
    // D78: the W-then-ANSI shape C9 found at the best of the ten prologues now
    // lives in TestBase, where the other five copies of it went too. What is
    // left here is what is this helper's own - the skip, and a message naming
    // the statement.
    if (SQL_SUCCEEDED(prepare_w_then_ansi(stmt, {sql}))) return true;

    r.status = TestStatus::SKIP_INCONCLUSIVE;
    r.actual = std::string("Could not prepare `") + sql +
               "` (W and ANSI both failed; the DDL setup may not have run)";
    r.suggestion = "This probe needs a prepared array INSERT before it can "
                   "assert anything about array parameters. Check that "
                   "ODBC_TEST_ARRAY exists and that the driver accepts "
                   "parameter markers in an INSERT.";
    return false;
}

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
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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

        // Q1: the integer axis, which is the one #299 corrupts and the one this
        // probe could not see. `id_array` is bound as SQL_C_SLONG with
        // BufferLength 0 — the specification says the length is ignored for a
        // fixed-length C type and applications pass 0 — and a driver that uses
        // it as the element stride multiplies the set number by zero and reads
        // element 0 every time. The strings step correctly through the same
        // bug, because for SQL_C_CHAR the length *is* the element size, so
        // checking them alone reports a pass over three identical IDs.
        if (!v.keys_are({"100", "200", "300"})) {
            r.status = TestStatus::FAIL;
            r.severity = Severity::CRITICAL;
            r.actual = actual.str() + "; integer axis wrong: expected IDs " +
                       "100, 200, 300 but read back " + v.keys_joined();
            r.suggestion =
                "Each parameter set must take the next element of the bound "
                "integer array. SQLBindParameter's BufferLength is ignored for "
                "a fixed-length C type, so a driver that uses it as the element "
                "stride reads element 0 for every set: N identical rows, "
                "SQL_SUCCESS, and a correct processed count. Derive the stride "
                "from the C type when BufferLength is 0.";
            return;
        }

        actual << "; all " << ARRAY_SIZE
               << " rows persisted with the right strings and IDs";
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
        
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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
            // C9: the result was discarded here, so a driver that could
            // not prepare this INSERT was reported as failing row-wise
            // binding - a wrong diagnosis rather than a missing one.
            if (!prepare_array_insert(probe_stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)")) return;
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
            // C9: the result was discarded here, so a driver that could
            // not prepare this INSERT was reported as failing row-wise
            // binding - a wrong diagnosis rather than a missing one.
            if (!prepare_array_insert(probe_stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)")) return;
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
        
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;   // C9
        
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
// ── P11 (IMPROVEMENT_PLAN_V2) — the row count, against the driver's claim ──
//
// `SQLRowCount` was never called in this file, and `SQL_PARAM_ARRAY_ROW_COUNTS`
// and `SQL_PARAM_ARRAY_SELECTS` appear nowhere in `src/` — so neither what the
// driver does after an array execute nor what it says it does was read.
//
// The trick is the one `test_scalar_function_claim_vs_execute` uses, and it is
// the most productive shape in the suite: **ask the driver what it does, then
// check that it does it.** Either answer to `SQL_PARAM_ARRAY_ROW_COUNTS` is
// conformant —
//
//   SQL_PARC_BATCH     one row count per parameter set, stepped with
//                      SQLMoreResults;
//   SQL_PARC_NO_BATCH  one cumulative count for the whole execute.
//
// — and disagreeing with your own answer is not. A driver that answers BATCH,
// keeps only the last set's update count and then returns SQL_NO_DATA from
// SQLMoreResults gives an application 1 for a five-row insert and no way to
// find the rest.
TestResult ArrayParamTests::test_array_row_count_matches_its_claim() {
    return run_test(
        "test_array_row_count_matches_its_claim",
        "SQLGetInfo(SQL_PARAM_ARRAY_ROW_COUNTS)/SQLRowCount/SQLMoreResults",
        "After a 5-set array INSERT, SQLRowCount agrees with the driver's own "
        "SQL_PARAM_ARRAY_ROW_COUNTS answer",
        Severity::ERR, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Arrays of Parameter Values — row counts",
        [&](TestResult& r) {
        SQLUINTEGER claim = 0;
        SQLRETURN rc = SQLGetInfo(conn_.get_handle(), SQL_PARAM_ARRAY_ROW_COUNTS,
                                  &claim, sizeof(claim), nullptr);
        if (!SQL_SUCCEEDED(rc)) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "SQLGetInfo(SQL_PARAM_ARRAY_ROW_COUNTS) failed, so there "
                       "is no claim to grade the behaviour against";
            return;
        }
        const bool claims_batch = (claim == SQL_PARC_BATCH);
        const char* claim_name = claims_batch ? "SQL_PARC_BATCH" : "SQL_PARC_NO_BATCH";

        // A baseline, because earlier probes in this category have already put
        // rows in this table and the interesting number is the delta.
        // verify_rows_persisted is not used here: it grades a count against an
        // expectation, and this needs the count itself.
        auto count_rows = [&]() -> long {
            try {
                core::OdbcStatement c(conn_);
                c.execute("SELECT COUNT(*) FROM ODBC_TEST_ARRAY");
                if (!c.fetch()) return -1;
                SQLBIGINT n = 0;
                SQLLEN ind = 0;
                if (!SQL_SUCCEEDED(SQLGetData(c.get_handle(), 1, SQL_C_SBIGINT,
                                              &n, sizeof(n), &ind))) return -1;
                return static_cast<long>(n);
            } catch (const core::OdbcError&) {
                return -1;
            }
        };
        const long before = count_rows();
        if (before < 0) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not count the table before the execute";
            return;
        }

        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN kSets = 5;

        // The paramset size is set *before* the prepare, deliberately, and this
        // is the one probe in the category that does it that way. A driver that
        // latches its executor at prepare time - the second defect in PR #308 -
        // otherwise runs a single set here, and then there is no five-row
        // execute for the row count to be right or wrong about. Isolating the
        // fix under test from the one next door is the whole point of asking
        // the question in this order.
        if (!configure_array_exec(stmt, r, kSets)) return;
        if (!prepare_array_insert(stmt, r,
                "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;

        SQLINTEGER ids[kSets];
        SQLLEN id_ind[kSets];
        constexpr int kNameLen = 21;
        char names[kSets][kNameLen];
        SQLLEN name_ind[kSets];
        for (SQLULEN i = 0; i < kSets; ++i) {
            ids[i] = static_cast<SQLINTEGER>(7100 + i);
            id_ind[i] = 0;
            std::snprintf(names[i], kNameLen, "rowcount-%d", static_cast<int>(i));
            name_ind[i] = SQL_NTS;
        }
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG,
                         SQL_INTEGER, 0, 0, ids, 0, id_ind);
        SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT, SQL_C_CHAR,
                         SQL_VARCHAR, kNameLen - 1, 0, names, kNameLen, name_ind);

        SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
        SQLLEN row_count = -99;
        SQLRowCount(stmt.get_handle(), &row_count);

        // How many counts can the application actually reach?
        long long reachable = (row_count >= 0) ? row_count : 0;
        int more_result_sets = 0;
        while (SQLMoreResults(stmt.get_handle()) == SQL_SUCCESS) {
            if (++more_result_sets > 20) break;
            SQLLEN next = 0;
            if (SQL_SUCCEEDED(SQLRowCount(stmt.get_handle(), &next)) && next > 0) {
                reachable += next;
            }
        }

        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                       reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);

        const long after = count_rows();
        const long inserted = (after >= 0 && before >= 0) ? after - before : -1;

        std::ostringstream oss;
        oss << "driver claims " << claim_name << "; execute rc=" << exec_rc
            << "; SQLRowCount=" << row_count << "; " << more_result_sets
            << " further result set(s) via SQLMoreResults; rows actually "
               "inserted=" << inserted << "; total count reachable by the "
               "application=" << reachable;
        r.actual = oss.str();

        if (!SQL_SUCCEEDED(exec_rc)) {
            r.status = TestStatus::FAIL;
            r.actual += " — the array execute itself failed";
            return;
        }
        if (inserted != static_cast<long>(kSets)) {
            // Not this probe's subject: another probe in this category grades
            // whether all five sets ran. Without five rows there is no row
            // count to judge.
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual += " — the execute did not insert five rows, so the row "
                        "count has nothing to be right about";
            return;
        }

        if (reachable != static_cast<long long>(kSets)) {
            r.status = TestStatus::FAIL;
            r.severity = Severity::ERR;
            r.suggestion =
                std::string("The driver answers ") + claim_name +
                " for SQL_PARAM_ARRAY_ROW_COUNTS. Under SQL_PARC_NO_BATCH "
                "SQLRowCount must return the cumulative count for the whole "
                "execute; under SQL_PARC_BATCH there is one count per parameter "
                "set and SQLMoreResults must step to the next. Five rows went in "
                "and the application can only account for " +
                std::to_string(reachable) + " of them — an ORM using the count "
                "to confirm its write sees a number that is simply wrong, with "
                "no error to catch.";
        }
        });
}

// ── P12 (IMPROVEMENT_PLAN_V2) — the handle, after the array threw ────────
//
// `test_param_status_per_row_partial_failure` executes once and reads the
// status array. It never touches the handle afterwards — and that is where the
// damage is.
//
// `executeStatementParamArray` pointed the APD's bind-offset pointer at a local
// for the duration of the loop and restored it on the normal exit and on the
// `inputParam` failure exit. A server error thrown by `executeStatement` took a
// third exit that restored nothing, so `sqlExecute` caught the exception with
// the descriptor pointing into a dead stack frame, and *the next execute on
// that handle* read a garbage offset. In the issue that was an access
// violation; on the operation-pointer branch it inserted a garbage row.
//
// So this probe provokes the error and then keeps using the handle, which is
// what an application does. It also checks the failed set is marked
// `SQL_PARAM_ERROR` rather than left `SQL_PARAM_UNUSED` — an application that
// reads the status array to find out which row to retry needs that.
//
// The error is a NOT NULL violation, which is standard SQL and needs no
// dialect knowledge. If the engine will not accept a NOT NULL column the probe
// has no way to fail one set out of five and says so.
TestResult ArrayParamTests::test_handle_reuse_after_array_error() {
    return run_test(
        "test_handle_reuse_after_array_error",
        "SQLExecute(paramset)/SQLExecute on the same handle",
        "After a server error inside a parameter array, the failed set is "
        "marked SQL_PARAM_ERROR and the statement handle is still usable",
        Severity::CRITICAL, ConformanceLevel::LEVEL_1,
        "ODBC 3.x Arrays of Parameter Values — error inside the array",
        [&](TestResult& r) {
        static const char* const kTable = "ODBC_TEST_ARRAY_ERR";
        auto guard = RoundTripTableGuard::create_first_working(
            conn_, kTable, {"VARCHAR(50) NOT NULL"});
        if (!guard.ok()) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = "Could not create a table with a NOT NULL column, so no "
                       "single parameter set can be made to fail";
            r.diagnostic = guard.last_error();
            return;
        }

        core::OdbcStatement stmt(conn_);
        constexpr SQLULEN kSets = 5;

        // Before the prepare, so a driver that latches its executor at prepare
        // time still runs five sets - PR #308's second defect would otherwise
        // turn this into a single-row insert with nothing to fail.
        if (!configure_array_exec(stmt, r, kSets)) return;
        if (!prepare_array_insert(stmt, r,
                std::string("INSERT INTO " + std::string(kTable) +
                            " (ID, " + guard.val_column() + ") VALUES (?, ?)").c_str())) {
            return;
        }

        SQLUSMALLINT status[kSets];
        for (SQLULEN i = 0; i < kSets; ++i) status[i] = SQL_PARAM_UNUSED;
        if (!set_stmt_attr_or_skip(stmt, r, SQL_ATTR_PARAM_STATUS_PTR, status,
                                   "SQL_ATTR_PARAM_STATUS_PTR")) return;
        SQLULEN processed = 0;
        if (!set_stmt_attr_or_skip(stmt, r, SQL_ATTR_PARAMS_PROCESSED_PTR,
                                   &processed, "SQL_ATTR_PARAMS_PROCESSED_PTR")) return;

        SQLINTEGER ids[kSets] = {8001, 8002, 8003, 8004, 8005};
        SQLLEN id_ind[kSets] = {0, 0, 0, 0, 0};
        char names[kSets][51] = {"one", "two", "three", "four", "five"};
        SQLLEN name_ind[kSets] = {SQL_NTS, SQL_NTS, SQL_NULL_DATA, SQL_NTS, SQL_NTS};
        //                                          ^ set 3 violates NOT NULL

        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG,
                         SQL_INTEGER, 0, 0, ids, 0, id_ind);
        SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT, SQL_C_CHAR,
                         SQL_VARCHAR, 50, 0, names, 51, name_ind);

        const SQLRETURN exec_rc = SQLExecute(stmt.get_handle());
        const std::string exec_state =
            first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "none");

        std::ostringstream oss;
        oss << "array execute rc=" << exec_rc << " (" << exec_state
            << "), processed=" << processed << ", status=[";
        for (SQLULEN i = 0; i < kSets; ++i) {
            if (i) oss << ", ";
            switch (status[i]) {
                case SQL_PARAM_SUCCESS: oss << "SUCCESS"; break;
                case SQL_PARAM_ERROR:   oss << "ERROR";   break;
                case SQL_PARAM_UNUSED:  oss << "UNUSED";  break;
                default:                oss << status[i]; break;
            }
        }
        oss << "]";

        const bool marked_error = (status[2] == SQL_PARAM_ERROR);

        // SQL_SUCCESS_WITH_INFO is the *correct* return when some parameter
        // sets succeed and others fail - only an execute where nothing worked
        // is SQL_ERROR. So "did a set fail" is answered by the return code or
        // by the status array, not by the return code alone; reading only the
        // return code made this probe skip against a driver that had just
        // reported the failure properly.
        bool any_set_failed = !SQL_SUCCEEDED(exec_rc);
        for (SQLULEN i = 0; i < kSets; ++i) {
            if (status[i] == SQL_PARAM_ERROR) any_set_failed = true;
        }
        if (!any_set_failed) {
            r.status = TestStatus::SKIP_INCONCLUSIVE;
            r.actual = oss.str() + " — the NOT NULL violation was accepted, so "
                                   "no set failed and there is no error path to "
                                   "exercise";
            return;
        }

        // The part that matters: keep using the handle. An application does.
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMSET_SIZE,
                       reinterpret_cast<SQLPOINTER>(static_cast<SQLULEN>(1)), 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAM_STATUS_PTR, nullptr, 0);
        SQLSetStmtAttr(stmt.get_handle(), SQL_ATTR_PARAMS_PROCESSED_PTR, nullptr, 0);

        SQLINTEGER reuse_id = 8007;
        SQLLEN reuse_id_ind = 0;
        char reuse_name[51] = "seven";
        SQLLEN reuse_name_ind = SQL_NTS;
        SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT, SQL_C_SLONG,
                         SQL_INTEGER, 0, 0, &reuse_id, 0, &reuse_id_ind);
        SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_INPUT, SQL_C_CHAR,
                         SQL_VARCHAR, 50, 0, reuse_name, 51, &reuse_name_ind);

        const SQLRETURN reuse_rc = SQLExecute(stmt.get_handle());
        oss << "; reuse execute rc=" << reuse_rc;
        if (!SQL_SUCCEEDED(reuse_rc)) {
            r.status = TestStatus::FAIL;
            r.severity = Severity::CRITICAL;
            oss << " (" << first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "none")
                << ")";
            r.actual = oss.str() + " — the handle was left unusable by the array error";
            r.suggestion =
                "A server error inside a parameter array must leave the "
                "statement handle as usable as any other failed execute. "
                "Restoring the descriptor's bind-offset pointer on the "
                "exception path is what makes that true; leaving it pointing "
                "into a dead stack frame gives the next execute a garbage "
                "offset.";
            return;
        }

        commit_now();

        // And did the reused handle write the row it was asked to write?
        std::string found;
        try {
            core::OdbcStatement check(conn_);
            check.execute("SELECT " + guard.val_column() + " FROM " +
                          std::string(kTable) + " WHERE ID = 8007");
            if (check.fetch()) found = get_string(check.get_handle(), 1);
        } catch (const core::OdbcError& e) {
            r.status = TestStatus::FAIL;
            r.actual = oss.str() + " — could not read the reused handle's row back: " +
                       e.what();
            return;
        }
        oss << "; row 8007 reads back as '" << found << "'";
        r.actual = oss.str();

        if (found != "seven") {
            r.status = TestStatus::FAIL;
            r.severity = Severity::CRITICAL;
            r.actual = oss.str() + ", expected 'seven'";
            r.suggestion =
                "The execute after the array error returned success and stored "
                "something else — the classic symptom of a descriptor left "
                "pointing at a dead stack frame. This is silent: the "
                "application is told the row went in.";
            return;
        }
        if (!marked_error) {
            // Reported after the handle check, because a wedged handle is the
            // worse fault and should be the headline when both are true.
            r.status = TestStatus::FAIL;
            r.severity = Severity::ERR;
            r.actual = oss.str() + " — but the failed set was not marked "
                                   "SQL_PARAM_ERROR";
            r.suggestion =
                "The set that failed must be marked SQL_PARAM_ERROR in "
                "SQL_ATTR_PARAM_STATUS_PTR. An application reading the status "
                "array to decide what to retry cannot otherwise tell which set "
                "was rejected from which was never attempted.";
        }
        });
}

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
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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
        if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID) VALUES (?)")) return;   // C9
        SQLRETURN ret = SQL_SUCCESS;
        
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

            // C9: this is where the W-then-ANSI fallback came
            // from; the helper carries it to the other nine.
            if (!prepare_array_insert(stmt, r, "INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (?, ?)")) return;
            SQLRETURN ret = SQL_SUCCESS;

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
                core::GuardedBuffer<char> state(6, 0);  // D62
                SQLINTEGER native = 0;
                core::GuardedBuffer<SQLCHAR> msg(256, 0);  // D62
                SQLSMALLINT msg_len = 0;
                SQLRETURN dr = SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(),
                                             i,
                                             reinterpret_cast<SQLCHAR*>(state.data()),
                                             &native, msg.data(), msg.declared_bytes(), &msg_len);
                if (dr == SQL_NO_DATA || !SQL_SUCCEEDED(dr)) break;
                // D68: the message to `msg_len`. The SQLSTATE beside it has
                // no length parameter at all in SQLGetDiagRec's signature, so
                // it is a different problem - D69.
                if (sqlstate.empty()) {
                    sqlstate = core::sqlstate_string(state.data());
                    msg_text = core::bounded_string(
                        reinterpret_cast<char*>(msg.data()), msg.declared_elements(), msg_len).value;
                }
                if (core::sqlstate_string(state.data()) == "HYC00") {
                    sqlstate = "HYC00";
                    msg_text = core::bounded_string(
                        reinterpret_cast<char*>(msg.data()), msg.declared_elements(), msg_len).value;
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
