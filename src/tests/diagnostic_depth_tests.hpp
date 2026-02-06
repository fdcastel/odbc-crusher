#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Diagnostic Depth Tests (Phase 15.2c)
class DiagnosticDepthTests : public TestBase {
public:
    explicit DiagnosticDepthTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Diagnostic Depth Tests"; }
    
private:
    TestResult test_diagfield_sqlstate();
    TestResult test_diagfield_record_count();
    TestResult test_diagfield_row_count();
    TestResult test_multiple_diagnostic_records();
};

} // namespace odbc_crusher::tests
