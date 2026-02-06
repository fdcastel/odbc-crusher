#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Advanced ODBC feature tests (Phase 9 + Phase 12 extensions)
class AdvancedTests : public TestBase {
public:
    explicit AdvancedTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Advanced Features"; }
    
private:
    TestResult test_cursor_types();
    TestResult test_array_binding();
    TestResult test_async_capability();
    TestResult test_rowset_size();
    TestResult test_positioned_operations();
    TestResult test_statement_attributes();
    
    // Phase 12: Scrollable Cursor Tests
    TestResult test_fetch_scroll_next();
    TestResult test_fetch_scroll_first_last();
    TestResult test_fetch_scroll_absolute();
    TestResult test_cursor_scrollable_attr();
};

} // namespace odbc_crusher::tests
