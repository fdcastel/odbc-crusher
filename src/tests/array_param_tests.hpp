#pragma once

#include "test_base.hpp"

#include <optional>

namespace odbc_crusher::tests {

// Array Parameter Tests (Phase 16)
// Tests ODBC "Arrays of Parameter Values" feature — binding arrays of values
// to parameter markers and executing a statement for multiple parameter sets.
class ArrayParamTests : public TestBase {
public:
    explicit ArrayParamTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Array Parameter Tests"; }
    
protected:
    // Table lifecycle — creates ODBC_TEST_ARRAY with autocommit ON, drops on
    // cleanup. `protected` for the reason given in transaction_tests.hpp:
    // A15's reuse path needs a test, and a test cannot crash a previous run.
    bool create_test_table();
    void drop_test_table();

private:
    // C9: the two attributes that make an array execution an
    // array execution, set together and - unlike before - checked.
    // D66: set one attribute and report if the driver refuses it, rather
    // than carrying on and grading the driver on values we wrote ourselves.
    bool set_stmt_attr_or_skip(core::OdbcStatement& stmt, TestResult& r,
                               SQLINTEGER attr, SQLPOINTER value,
                               const char* attr_name);

    // C9: prepare the array INSERT, W with an ANSI fallback, or skip
    // saying why. Ten sites had this; two of them discarded the return code
    // and then blamed row-wise binding for a failed prepare.
    bool prepare_array_insert(core::OdbcStatement& stmt, TestResult& r,
                              const char* sql);

    bool configure_array_exec(core::OdbcStatement& stmt,
                              TestResult& r, SQLULEN paramset_size);

    // Stores the last DDL error message for reporting in skip suggestions
    std::string last_ddl_error_;

    // C4 — see transaction_tests.hpp.
    std::optional<RoundTripTableGuard> table_;
    
    TestResult test_column_wise_array_binding();
    TestResult test_row_wise_array_binding();
    TestResult test_param_status_array();
    TestResult test_params_processed_count();
    TestResult test_array_with_null_values();
    // P11 (IMPROVEMENT_PLAN_V2): SQLRowCount after an array execute,
    // graded against the driver's own SQL_PARAM_ARRAY_ROW_COUNTS answer.
    TestResult test_array_row_count_matches_its_claim();

    // P12 (IMPROVEMENT_PLAN_V2): the statement handle after a server
    // error inside the array - where PR #313 says the damage is, and
    // where no probe looked.
    TestResult test_handle_reuse_after_array_error();

    TestResult test_param_operation_array();
    TestResult test_paramset_size_one();

    // PORT plan §4.6 — driver-detected per-row failure (mid-batch constraint
    // violation), and the SQL_ATTR_PARAMSET_SIZE-unsupported fallback path.
    // The existing partial-error test uses SQL_PARAM_OPERATION_PTR with
    // SQL_PARAM_IGNORE (application-driven skip); these probes exercise the
    // driver-driven failure shape that's harder to detect.
    TestResult test_param_status_per_row_partial_failure();
    TestResult test_paramset_size_unsupported_returns_error();
};

} // namespace odbc_crusher::tests
