#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief Descriptor Tests
 * 
 * Tests ODBC descriptor handle operations:
 * - Implicit descriptor handles (APD/ARD/IPD/IRD) via SQLGetStmtAttr
 * - IRD field reading after SQLPrepare
 * - APD field setting for parameter binding
 * - SQLCopyDesc between descriptors
 * - Auto-population of descriptors after SQLExecDirect
 */
class DescriptorTests : public TestBase {
public:
    explicit DescriptorTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Descriptor Tests"; }
    
private:
    TestResult test_implicit_descriptors();
    TestResult test_ird_after_prepare();
    TestResult test_apd_fields();
    TestResult test_copy_desc();
    TestResult test_auto_populate_after_exec();
};

} // namespace odbc_crusher::tests
