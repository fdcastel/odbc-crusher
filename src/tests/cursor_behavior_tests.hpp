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
    // D86: the same claim across two result sets, which D85 showed is a
    // different question and had no probe.
    TestResult test_getdata_restarts_in_a_new_result_set();
};

} // namespace odbc_crusher::tests
