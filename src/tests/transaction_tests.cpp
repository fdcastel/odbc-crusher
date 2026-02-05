#include "transaction_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> TransactionTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_autocommit_on());
    results.push_back(test_autocommit_off());
    results.push_back(test_manual_commit());
    results.push_back(test_manual_rollback());
    results.push_back(test_transaction_isolation_levels());
    
    return results;
}

bool TransactionTests::create_test_table() {
    try {
        core::OdbcStatement stmt(conn_);
        
        // Try to drop first (ignore errors)
        try {
            stmt.execute("DROP TABLE ODBC_TEST_TXN");
        } catch (...) {
            // Ignore - table may not exist
        }
        
        // Create table - try different syntaxes
        std::vector<std::string> create_queries = {
            "CREATE TABLE ODBC_TEST_TXN (ID INTEGER, VALUE VARCHAR(50))",  // Standard
            "CREATE TABLE ODBC_TEST_TXN (ID INT, VALUE VARCHAR(50))"        // Alternative
        };
        
        for (const auto& query : create_queries) {
            try {
                stmt.execute(query);
                return true;
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        return false;
    } catch (...) {
        return false;
    }
}

void TransactionTests::drop_test_table() {
    try {
        core::OdbcStatement stmt(conn_);
        stmt.execute("DROP TABLE ODBC_TEST_TXN");
    } catch (...) {
        // Ignore cleanup errors
    }
}

TestResult TransactionTests::test_autocommit_on() {
    TestResult result = make_result(
        "test_autocommit_on",
        "SQLGetConnectAttr(SQL_ATTR_AUTOCOMMIT)",
        TestStatus::PASS,
        "Autocommit mode should be ON by default",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLGetConnectAttr"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        SQLUINTEGER autocommit = 0;
        SQLRETURN ret = SQLGetConnectAttr(
            conn_.get_handle(),
            SQL_ATTR_AUTOCOMMIT,
            &autocommit,
            0,
            nullptr
        );
        
        if (SQL_SUCCEEDED(ret)) {
            if (autocommit == SQL_AUTOCOMMIT_ON) {
                result.actual = "Autocommit is ON (default)";
                result.status = TestStatus::PASS;
            } else {
                result.actual = "Autocommit is OFF (unexpected default)";
                result.status = TestStatus::FAIL;
            }
        } else {
            result.actual = "Could not query autocommit mode";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "SQLGetConnectAttr for SQL_ATTR_AUTOCOMMIT did not succeed";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult TransactionTests::test_autocommit_off() {
    TestResult result = make_result(
        "test_autocommit_off",
        "SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT, OFF)",
        TestStatus::PASS,
        "Can disable autocommit mode",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLSetConnectAttr"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Turn off autocommit
        SQLRETURN ret = SQLSetConnectAttr(
            conn_.get_handle(),
            SQL_ATTR_AUTOCOMMIT,
            (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
            0
        );
        
        if (SQL_SUCCEEDED(ret)) {
            // Verify it's off
            SQLUINTEGER autocommit = 0;
            ret = SQLGetConnectAttr(
                conn_.get_handle(),
                SQL_ATTR_AUTOCOMMIT,
                &autocommit,
                0,
                nullptr
            );
            
            if (SQL_SUCCEEDED(ret) && autocommit == SQL_AUTOCOMMIT_OFF) {
                result.actual = "Successfully disabled autocommit";
                result.status = TestStatus::PASS;
                
                // Turn it back on for other tests
                SQLSetConnectAttr(
                    conn_.get_handle(),
                    SQL_ATTR_AUTOCOMMIT,
                    (SQLPOINTER)SQL_AUTOCOMMIT_ON,
                    0
                );
            } else {
                result.actual = "Autocommit mode did not change";
                result.status = TestStatus::FAIL;
            }
        } else {
            result.actual = "SQLSetConnectAttr for autocommit not supported";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Driver did not accept SQL_ATTR_AUTOCOMMIT change";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        
        // Try to restore autocommit
        try {
            SQLSetConnectAttr(
                conn_.get_handle(),
                SQL_ATTR_AUTOCOMMIT,
                (SQLPOINTER)SQL_AUTOCOMMIT_ON,
                0
            );
        } catch (...) {}
    }
    
    return result;
}

TestResult TransactionTests::test_manual_commit() {
    TestResult result = make_result(
        "test_manual_commit",
        "SQLEndTran(SQL_COMMIT)",
        TestStatus::PASS,
        "Can manually commit a transaction",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLEndTran"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Turn off autocommit
        SQLRETURN ret = SQLSetConnectAttr(
            conn_.get_handle(),
            SQL_ATTR_AUTOCOMMIT,
            (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
            0
        );
        
        if (!SQL_SUCCEEDED(ret)) {
            result.actual = "Cannot disable autocommit for manual transaction test";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Could not disable autocommit to test manual commit";
        } else {
            // Create test table
            if (!create_test_table()) {
                result.actual = "Could not create test table";
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.suggestion = "Test table creation failed; manual commit test could not run";
                
                // Restore autocommit
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            } else {
                // Insert data
                core::OdbcStatement stmt(conn_);
                stmt.execute("INSERT INTO ODBC_TEST_TXN VALUES (1, 'test')");
                
                // Commit
                ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);
                
                if (SQL_SUCCEEDED(ret)) {
                    // Verify data exists
                    stmt.execute("SELECT COUNT(*) FROM ODBC_TEST_TXN");
                    if (stmt.fetch()) {
                        SQLINTEGER count = 0;
                        SQLLEN indicator = 0;
                        SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                  &count, sizeof(count), &indicator);
                        
                        if (count == 1) {
                            result.actual = "Transaction committed successfully";
                            result.status = TestStatus::PASS;
                        } else {
                            result.actual = "Data not committed";
                            result.status = TestStatus::FAIL;
                        }
                    }
                } else {
                    result.actual = "SQLEndTran(COMMIT) failed";
                    result.status = TestStatus::FAIL;
                }
                
                // Cleanup
                drop_test_table();
                
                // Restore autocommit
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        
        // Cleanup
        try {
            drop_test_table();
            SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
        } catch (...) {}
    }
    
    return result;
}

TestResult TransactionTests::test_manual_rollback() {
    TestResult result = make_result(
        "test_manual_rollback",
        "SQLEndTran(SQL_ROLLBACK)",
        TestStatus::PASS,
        "Can manually rollback a transaction",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLEndTran"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Turn off autocommit
        SQLRETURN ret = SQLSetConnectAttr(
            conn_.get_handle(),
            SQL_ATTR_AUTOCOMMIT,
            (SQLPOINTER)SQL_AUTOCOMMIT_OFF,
            0
        );
        
        if (!SQL_SUCCEEDED(ret)) {
            result.actual = "Cannot disable autocommit for rollback test";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Could not disable autocommit to test manual rollback";
        } else {
            // Create test table
            if (!create_test_table()) {
                result.actual = "Could not create test table";
                result.status = TestStatus::SKIP_INCONCLUSIVE;
                result.suggestion = "Test table creation failed; rollback test could not run";
                
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            } else {
                // Commit the CREATE TABLE
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);
                
                // Insert data
                core::OdbcStatement stmt(conn_);
                stmt.execute("INSERT INTO ODBC_TEST_TXN VALUES (1, 'should_rollback')");
                
                // Rollback
                ret = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
                
                if (SQL_SUCCEEDED(ret)) {
                    // Verify data does NOT exist
                    stmt.execute("SELECT COUNT(*) FROM ODBC_TEST_TXN");
                    if (stmt.fetch()) {
                        SQLINTEGER count = 0;
                        SQLLEN indicator = 0;
                        SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG,
                                  &count, sizeof(count), &indicator);
                        
                        if (count == 0) {
                            result.actual = "Transaction rolled back successfully";
                            result.status = TestStatus::PASS;
                        } else {
                            result.actual = "Data was not rolled back";
                            result.status = TestStatus::FAIL;
                        }
                    }
                } else {
                    result.actual = "SQLEndTran(ROLLBACK) failed";
                    result.status = TestStatus::FAIL;
                }
                
                // Cleanup
                drop_test_table();
                
                // Restore autocommit
                SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                                (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
            }
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
        
        // Cleanup
        try {
            drop_test_table();
            SQLSetConnectAttr(conn_.get_handle(), SQL_ATTR_AUTOCOMMIT,
                            (SQLPOINTER)SQL_AUTOCOMMIT_ON, 0);
        } catch (...) {}
    }
    
    return result;
}

TestResult TransactionTests::test_transaction_isolation_levels() {
    TestResult result = make_result(
        "test_transaction_isolation_levels",
        "SQLSetConnectAttr(SQL_ATTR_TXN_ISOLATION)",
        TestStatus::PASS,
        "Can query and set transaction isolation levels",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 §SQLSetConnectAttr, §SQL_ATTR_TXN_ISOLATION"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        // Get current isolation level
        SQLUINTEGER isolation = 0;
        SQLRETURN ret = SQLGetConnectAttr(
            conn_.get_handle(),
            SQL_ATTR_TXN_ISOLATION,
            &isolation,
            0,
            nullptr
        );
        
        if (!SQL_SUCCEEDED(ret)) {
            result.actual = "Transaction isolation level query not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "Driver does not support querying SQL_ATTR_TXN_ISOLATION";
        } else {
            std::ostringstream oss;
            oss << "Current isolation: ";
            
            switch (isolation) {
                case SQL_TXN_READ_UNCOMMITTED:
                    oss << "READ UNCOMMITTED";
                    break;
                case SQL_TXN_READ_COMMITTED:
                    oss << "READ COMMITTED";
                    break;
                case SQL_TXN_REPEATABLE_READ:
                    oss << "REPEATABLE READ";
                    break;
                case SQL_TXN_SERIALIZABLE:
                    oss << "SERIALIZABLE";
                    break;
                default:
                    oss << "Unknown (" << isolation << ")";
            }
            
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

} // namespace odbc_crusher::tests
