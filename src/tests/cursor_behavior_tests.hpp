#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Cursor Behavior Tests (Phase 15.2d)
class CursorBehaviorTests : public TestBase {
public:
    explicit CursorBehaviorTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Cursor Behavior Tests"; }
    
private:
    TestResult test_forward_only_past_end();
    TestResult test_fetchscroll_first_forward_only();
    TestResult test_cursor_type_attribute();
    TestResult test_getdata_same_column_twice();
};

} // namespace odbc_crusher::tests
