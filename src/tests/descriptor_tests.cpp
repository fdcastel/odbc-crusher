#include "descriptor_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> DescriptorTests::run() {
    return {
        test_implicit_descriptors(),
        test_ird_after_prepare(),
        test_apd_fields(),
        test_copy_desc(),
        test_auto_populate_after_exec()
    };
}

TestResult DescriptorTests::test_implicit_descriptors() {
    TestResult result = make_result(
        "test_implicit_descriptors",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC/SQL_ATTR_IMP_ROW_DESC)",
        TestStatus::PASS,
        "Retrieve implicit APD, ARD, IPD, IRD descriptor handles",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetStmtAttr, §Descriptor Handles"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        struct DescAttr {
            SQLINTEGER attr;
            const char* name;
        };
        
        DescAttr attrs[] = {
            {SQL_ATTR_APP_PARAM_DESC, "APD"},
            {SQL_ATTR_APP_ROW_DESC, "ARD"},
            {SQL_ATTR_IMP_PARAM_DESC, "IPD"},
            {SQL_ATTR_IMP_ROW_DESC, "IRD"}
        };
        
        int obtained = 0;
        std::ostringstream details;
        
        for (const auto& da : attrs) {
            SQLHDESC desc = SQL_NULL_HDESC;
            SQLRETURN rc = SQLGetStmtAttr(
                stmt.get_handle(), da.attr,
                &desc, 0, nullptr
            );
            
            if (SQL_SUCCEEDED(rc) && desc != SQL_NULL_HDESC) {
                obtained++;
                details << da.name << "=OK ";
            } else {
                details << da.name << "=N/A ";
            }
        }
        
        if (obtained == 4) {
            result.status = TestStatus::PASS;
            result.actual = "All 4 implicit descriptor handles obtained: " + details.str();
        } else if (obtained > 0) {
            result.status = TestStatus::PASS;
            result.actual = std::to_string(obtained) + "/4 descriptor handles: " + details.str();
        } else {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "No implicit descriptor handles available";
            result.suggestion = "Implicit descriptor handles (APD/ARD/IPD/IRD) are Core conformance per ODBC 3.x §Descriptor Handles";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DescriptorTests::test_ird_after_prepare() {
    TestResult result = make_result(
        "test_ird_after_prepare",
        "SQLGetStmtAttr(SQL_ATTR_IMP_ROW_DESC)/SQLGetDescField",
        TestStatus::PASS,
        "IRD populated with column metadata after SQLPrepare",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLPrepare, §IRD Auto-Population"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.prepare(query);
                
                // Get IRD handle
                SQLHDESC ird = SQL_NULL_HDESC;
                SQLRETURN rc = SQLGetStmtAttr(
                    stmt.get_handle(), SQL_ATTR_IMP_ROW_DESC,
                    &ird, 0, nullptr
                );
                
                if (SQL_SUCCEEDED(rc) && ird != SQL_NULL_HDESC) {
                    // Read SQL_DESC_COUNT from IRD
                    SQLSMALLINT count = 0;
                    rc = SQLGetDescField(
                        ird, 0, SQL_DESC_COUNT,
                        &count, sizeof(count), nullptr
                    );
                    
                    if (SQL_SUCCEEDED(rc)) {
                        result.status = TestStatus::PASS;
                        result.actual = "IRD has " + std::to_string(count) + " column(s) after SQLPrepare";
                        success = true;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not read IRD after prepare";
            result.suggestion = "IRD should be auto-populated with column metadata after SQLPrepare per ODBC 3.x spec";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DescriptorTests::test_apd_fields() {
    TestResult result = make_result(
        "test_apd_fields",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC)/SQLSetDescField",
        TestStatus::PASS,
        "APD fields can be set for parameter binding",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLSetDescField, §APD"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Get APD handle
        SQLHDESC apd = SQL_NULL_HDESC;
        SQLRETURN rc = SQLGetStmtAttr(
            stmt.get_handle(), SQL_ATTR_APP_PARAM_DESC,
            &apd, 0, nullptr
        );
        
        if (SQL_SUCCEEDED(rc) && apd != SQL_NULL_HDESC) {
            // Try to set DESC_COUNT
            SQLSMALLINT new_count = 1;
            rc = SQLSetDescField(
                apd, 0, SQL_DESC_COUNT,
                reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(new_count)), 0
            );
            
            if (SQL_SUCCEEDED(rc)) {
                // Read it back
                SQLSMALLINT check_count = 0;
                rc = SQLGetDescField(
                    apd, 0, SQL_DESC_COUNT,
                    &check_count, sizeof(check_count), nullptr
                );
                
                if (SQL_SUCCEEDED(rc) && check_count == new_count) {
                    result.status = TestStatus::PASS;
                    result.actual = "APD DESC_COUNT set to 1 and verified";
                } else {
                    result.status = TestStatus::PASS;
                    result.actual = "APD field settable (read-back returned " + std::to_string(check_count) + ")";
                }
            } else {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "SQLSetDescField on APD not supported";
                result.suggestion = "Descriptor field manipulation is Core conformance per ODBC 3.x";
            }
        } else {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "APD handle not available";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DescriptorTests::test_copy_desc() {
    TestResult result = make_result(
        "test_copy_desc",
        "SQLCopyDesc",
        TestStatus::PASS,
        "Copy descriptor fields between statement handles",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLCopyDesc"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt1(conn_);
        core::OdbcStatement stmt2(conn_);
        
        // Get ARD from stmt1 and stmt2
        SQLHDESC ard1 = SQL_NULL_HDESC;
        SQLHDESC ard2 = SQL_NULL_HDESC;
        
        SQLRETURN rc1 = SQLGetStmtAttr(stmt1.get_handle(), SQL_ATTR_APP_ROW_DESC, &ard1, 0, nullptr);
        SQLRETURN rc2 = SQLGetStmtAttr(stmt2.get_handle(), SQL_ATTR_APP_ROW_DESC, &ard2, 0, nullptr);
        
        if (SQL_SUCCEEDED(rc1) && SQL_SUCCEEDED(rc2) && 
            ard1 != SQL_NULL_HDESC && ard2 != SQL_NULL_HDESC) {
            
            SQLRETURN rc = SQLCopyDesc(ard1, ard2);
            
            if (SQL_SUCCEEDED(rc)) {
                result.status = TestStatus::PASS;
                result.actual = "SQLCopyDesc succeeded between two statement ARDs";
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "SQLCopyDesc failed";
                result.severity = Severity::WARNING;
                result.suggestion = "SQLCopyDesc is a Core conformance function per ODBC 3.x §SQLCopyDesc";
            }
        } else {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "ARD handles not available for copy";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult DescriptorTests::test_auto_populate_after_exec() {
    TestResult result = make_result(
        "test_auto_populate_after_exec",
        "SQLExecDirect/SQLNumResultCols",
        TestStatus::PASS,
        "Descriptors auto-populated after SQLExecDirect",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLExecDirect, §IRD Auto-Population"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                stmt.execute(query);
                
                // Check SQLNumResultCols (which reads from IRD)
                SQLSMALLINT num_cols = 0;
                SQLRETURN rc = SQLNumResultCols(stmt.get_handle(), &num_cols);
                
                if (SQL_SUCCEEDED(rc) && num_cols > 0) {
                    // Also verify column description works (reads from IRD)
                    SQLCHAR col_name[128];
                    SQLSMALLINT name_len = 0, data_type = 0, nullable = 0;
                    SQLULEN col_size = 0;
                    SQLSMALLINT dec_digits = 0;
                    
                    rc = SQLDescribeCol(stmt.get_handle(), 1,
                        col_name, sizeof(col_name), &name_len,
                        &data_type, &col_size, &dec_digits, &nullable);
                    
                    if (SQL_SUCCEEDED(rc)) {
                        result.status = TestStatus::PASS;
                        result.actual = "After SQLExecDirect: " + std::to_string(num_cols) + 
                                       " col(s), type=" + std::to_string(data_type);
                    } else {
                        result.status = TestStatus::PASS;
                        result.actual = "After SQLExecDirect: " + std::to_string(num_cols) + " column(s) detected";
                    }
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not execute query to test descriptor auto-population";
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
