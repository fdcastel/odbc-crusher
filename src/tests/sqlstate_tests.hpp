#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief SQLSTATE Validation Tests (Phase 13.1)
 * 
 * Verifies that the driver returns spec-correct SQLSTATEs for invalid operations.
 * Per ODBC 3.8 spec, drivers are required to return specific SQLSTATEs for
 * specific error conditions.
 */
class SqlstateTests : public TestBase {
public:
    explicit SqlstateTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "SQLSTATE Validation"; }
    
private:
    // Returns the SQLSTATE from a statement handle's diagnostic record 1
    std::string get_stmt_sqlstate(SQLHSTMT hstmt);
    std::string get_conn_sqlstate(SQLHDBC hdbc);
    
    TestResult test_execute_without_prepare();    // HY010
    TestResult test_fetch_no_cursor();            // 24000
    TestResult test_getdata_col0_no_bookmark();   // 07009
    TestResult test_getdata_col_out_of_range();   // 07009
    TestResult test_execdirect_syntax_error();    // 42000
    TestResult test_bindparam_invalid_ctype();    // HY003
    TestResult test_getinfo_invalid_type();       // HY096
    TestResult test_setconnattr_invalid_attr();   // HY092
    TestResult test_closecursor_no_cursor();      // 24000
    TestResult test_connect_already_connected();  // HY010 / 08002
};

} // namespace odbc_crusher::tests
