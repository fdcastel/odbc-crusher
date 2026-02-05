#include <gtest/gtest.h>
#include "core/odbc_error.hpp"

using namespace odbc_crusher::core;

TEST(OdbcErrorTest, ConstructWithMessage) {
    OdbcError error("Test error");
    EXPECT_STREQ(error.what(), "Test error");
}

TEST(OdbcErrorTest, DiagnosticsEmpty) {
    OdbcError error("Test error");
    EXPECT_TRUE(error.diagnostics().empty());
}

TEST(OdbcErrorTest, FormatDiagnostics) {
    std::vector<OdbcDiagnostic> diags;
    
    OdbcDiagnostic diag1;
    diag1.sqlstate = "08001";
    diag1.native_error = 12345;
    diag1.message = "Connection failed";
    diag1.record_number = 1;
    diags.push_back(diag1);
    
    OdbcError error("Connection error", std::move(diags));
    
    std::string formatted = error.format_diagnostics();
    EXPECT_NE(formatted.find("08001"), std::string::npos);
    EXPECT_NE(formatted.find("12345"), std::string::npos);
    EXPECT_NE(formatted.find("Connection failed"), std::string::npos);
}
