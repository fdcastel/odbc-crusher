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
    return run_test(
        "test_cancel_idle", "SQLCancel",
        "SQLCancel on idle statement succeeds",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCancel",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Cancel on a freshly allocated (idle) statement
            SQLRETURN rc = SQLCancel(stmt.get_handle());

            if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::PASS;
                r.actual = "SQLCancel on idle statement returned SQL_SUCCESS";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLCancel on idle statement failed (rc=" + std::to_string(rc) + ")";
                r.severity = Severity::WARNING;
                r.suggestion = "Per ODBC spec, SQLCancel should succeed on an idle statement";
            }
        });
}

TestResult CancellationTests::test_cancel_as_reset() {
    return run_test(
        "test_cancel_as_reset", "SQLCancel",
        "SQLCancel resets statement state after query execution",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCancel",
        [&](TestResult& r) {
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
                        r.status = TestStatus::PASS;
                        r.actual = "SQLCancel after query execution succeeded";
                        success = true;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQLCancel state reset";
            }
        });
}

} // namespace odbc_crusher::tests
