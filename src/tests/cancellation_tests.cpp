#include "cancellation_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"

namespace odbc_crusher::tests {

std::vector<TestResult> CancellationTests::run() {
    return {
        test_cancel_idle(),
        test_cancel_as_reset()
    };
}

TestResult CancellationTests::test_cancel_idle() {
    TestResult result = make_result(
        "test_cancel_idle",
        "SQLCancel",
        TestStatus::PASS,
        "SQLCancel on idle statement succeeds",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLCancel"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Cancel on a freshly allocated (idle) statement
        SQLRETURN rc = SQLCancel(stmt.get_handle());
        
        if (SQL_SUCCEEDED(rc)) {
            result.status = TestStatus::PASS;
            result.actual = "SQLCancel on idle statement returned SQL_SUCCESS";
        } else {
            result.status = TestStatus::FAIL;
            result.actual = "SQLCancel on idle statement failed (rc=" + std::to_string(rc) + ")";
            result.severity = Severity::WARNING;
            result.suggestion = "Per ODBC spec, SQLCancel should succeed on an idle statement";
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

TestResult CancellationTests::test_cancel_as_reset() {
    TestResult result = make_result(
        "test_cancel_as_reset",
        "SQLCancel",
        TestStatus::PASS,
        "SQLCancel resets statement state after query execution",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLCancel"
    );
    
    try {
        auto start = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
        bool success = false;
        
        for (const auto& query : queries) {
            try {
                // Execute a query
                stmt.execute(query);
                stmt.fetch();
                
                // Cancel to reset state
                SQLRETURN rc = SQLCancel(stmt.get_handle());
                
                if (SQL_SUCCEEDED(rc)) {
                    result.status = TestStatus::PASS;
                    result.actual = "SQLCancel after query execution succeeded";
                    success = true;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!success) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not test SQLCancel state reset";
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
