#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Unicode-specific tests (Phase 15.2a)
class UnicodeTests : public TestBase {
public:
    explicit UnicodeTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Unicode Tests"; }
    
private:
    TestResult test_getinfo_wchar_strings();
    TestResult test_describecol_wchar_names();
    TestResult test_getdata_sql_c_wchar();
    TestResult test_columns_unicode_patterns();
    TestResult test_string_truncation_wchar();
};

} // namespace odbc_crusher::tests
