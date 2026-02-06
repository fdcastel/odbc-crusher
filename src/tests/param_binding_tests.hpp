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
    TestResult test_bindparam_wchar_input();
    TestResult test_bindparam_null_indicator();
    TestResult test_param_rebind_execute();
};

} // namespace odbc_crusher::tests
