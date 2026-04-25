#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Parameter Binding Tests (Phase 15.2e)
class ParameterBindingTests : public TestBase {
public:
    explicit ParameterBindingTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Parameter Binding Tests"; }

private:
    // Table lifecycle for round-trip tests. Uses autocommit-on during DDL
    // so a failed DROP doesn't poison the transaction on Firebird-style drivers.
    bool create_roundtrip_table();
    void drop_roundtrip_table();

    // Stores the last DDL error for SKIP suggestions.
    std::string last_ddl_error_;

    TestResult test_bindparam_wchar_input();
    TestResult test_bindparam_null_indicator();
    TestResult test_param_rebind_execute();
    TestResult test_bindparam_int_to_varchar_roundtrip();
    TestResult test_sqldescribeparam_varchar();
    TestResult test_sqlrowcount_after_insert();
    TestResult test_sqlrowcount_after_update();
    TestResult test_sqlrowcount_after_delete();
};

} // namespace odbc_crusher::tests
