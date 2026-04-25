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

    // PORT plan §4.7 — bind a WCHAR string with non-BMP / non-ASCII
    // codepoints, INSERT, fetch back as SQL_C_WCHAR, codepoint-compare.
    // Detects drivers that re-encode through the system codepage.
    TestResult test_wchar_roundtrip_non_ascii();
    TestResult test_wchar_surrogate_pair_preserved();
};

} // namespace odbc_crusher::tests
