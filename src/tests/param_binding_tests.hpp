#pragma once

#include "test_base.hpp"

#include <map>
#include <string>

namespace odbc_crusher::tests {

// Parameter Binding Tests (Phase 15.2e)
class ParameterBindingTests : public TestBase {
public:
    explicit ParameterBindingTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Parameter Binding Tests"; }

private:
    // Table lifecycle for round-trip tests — thin adapters over
    // RoundTripTableGuard since C4. Defaults match the original
    // (ODBC_TEST_ROUNDTRIP, VARCHAR(32)).
    bool create_roundtrip_table(
        const std::string& table_name = "ODBC_TEST_ROUNDTRIP",
        const std::string& val_ddl = "VARCHAR(32)");
    void drop_roundtrip_table(
        const std::string& table_name = "ODBC_TEST_ROUNDTRIP");

    // Stores the last DDL error for SKIP suggestions.
    std::string last_ddl_error_;

    // C4: live tables, keyed by name. A map rather than a single optional
    // because the round-trip matrix builds a table per type and a probe may
    // hold one open while creating another. `std::map` rather than
    // `unordered_map` because the guard is move-constructible but not
    // move-assignable (it holds a connection reference), and map's node-based
    // storage never needs to reassign an element.
    std::map<std::string, RoundTripTableGuard> tables_;

    TestResult test_bindparam_wchar_input();
    TestResult test_bindparam_null_indicator();
    // B8: renamed - it binds once and re-executes, which is the bind-once
    // contract, not a rebind.
    TestResult test_param_bound_value_reread_on_execute();

    // §1.1 — numeric-C → character-SQL round-trip matrix. Integer shapes
    // share `run_int_to_string_roundtrip<CType>`; float shapes use
    // `run_float_to_string_roundtrip<CType>` (numeric tolerance because
    // drivers format `1.0f` differently — "1", "1.0", "1.000000", "1e0"…).
    TestResult test_bindparam_tinyint_to_varchar_roundtrip();
    TestResult test_bindparam_short_to_varchar_roundtrip();
    TestResult test_bindparam_int_to_varchar_roundtrip();
    TestResult test_bindparam_bigint_to_varchar_roundtrip();
    TestResult test_bindparam_float_to_varchar_roundtrip();
    TestResult test_bindparam_double_to_varchar_roundtrip();
    TestResult test_bindparam_double_to_varchar_fractional_roundtrip();
    TestResult test_bindparam_int_to_char_roundtrip();
    TestResult test_bindparam_int_to_wvarchar_roundtrip();

    // A20: the cells prior work left out. The unsigned C types are the
    // reason this matters - a driver that reaches for a signed conversion
    // on an unsigned buffer produces a wrong value and still returns
    // SQL_SUCCESS, which is the shape verify_rows_persisted exists to
    // catch. The CHAR and WVARCHAR axes had only SQL_C_SLONG.
    TestResult test_bindparam_utinyint_to_varchar_roundtrip();
    TestResult test_bindparam_ushort_to_varchar_roundtrip();
    TestResult test_bindparam_ulong_to_varchar_roundtrip();
    TestResult test_bindparam_ubigint_to_varchar_roundtrip();
    TestResult test_bindparam_bigint_to_char_roundtrip();
    TestResult test_bindparam_double_to_char_roundtrip();
    TestResult test_bindparam_bigint_to_wvarchar_roundtrip();

    // Shared roundtrip helpers — defined in the .cpp; only invoked from this
    // class's own test methods, so implicit instantiation is sufficient.
    template <typename CType>
    TestResult run_int_to_string_roundtrip(
        const std::string& test_name,
        SQLSMALLINT c_type_id,
        const std::string& c_type_name,
        SQLSMALLINT sql_type_id,
        const std::string& sql_type_name,
        const std::string& table_name,
        const std::string& column_ddl,
        SQLULEN col_size,
        bool right_trim_for_compare);

    template <typename CType>
    TestResult run_float_to_string_roundtrip(
        const std::string& test_name,
        SQLSMALLINT c_type_id,
        const std::string& c_type_name,
        SQLSMALLINT sql_type_id,
        const std::string& sql_type_name,
        const std::string& table_name,
        const std::string& column_ddl,
        SQLULEN col_size,
        double value_offset = 0.0);  // value_offset!=0 forces fractional values
                                     // so trunc-style driver bugs are detectable.

    TestResult test_sqldescribeparam_varchar();
    TestResult test_sqldescribeparam_integer();
    TestResult test_sqldescribeparam_decimal();

    TestResult test_sqlrowcount_after_insert();
    TestResult test_sqlrowcount_after_update();
    TestResult test_sqlrowcount_after_delete();
    TestResult test_sqlrowcount_after_execute_procedure();

    TestResult test_param_rebind_per_row_row_count();
    TestResult test_param_bind_once_execute_many_row_count();
    TestResult test_param_bind_once_execute_many_endtran();
    TestResult test_param_reexecute_requires_close();
    TestResult test_param_batch_then_single_row_tail();
};

} // namespace odbc_crusher::tests
