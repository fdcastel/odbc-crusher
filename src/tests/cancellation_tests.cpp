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

            // C2: the loop this replaces swallowed both failures, so a driver
            // that rejected every variant reported only "Could not test".
            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not test SQLCancel state reset";
                r.diagnostic = attempt.format_failures();
                return;
            }

            stmt.fetch();

            // A1: the query executed, so whatever SQLCancel does now is the
            // driver's answer. It used to fall through to the next dialect and
            // end as SKIP_INCONCLUSIVE, which hid the failure from the exit code.
            SQLRETURN rc = SQLCancel(stmt.get_handle());
            if (SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::PASS;
                r.actual = "SQLCancel after query execution succeeded";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLCancel after query execution returned " +
                           std::to_string(rc) + " (" +
                           first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(),
                                          "no SQLSTATE") + ")";
                r.severity = Severity::WARNING;
                r.suggestion = "SQLCancel on a statement with a result set should "
                               "succeed and reset the statement to the prepared or "
                               "allocated state.";
            }
        });
}

} // namespace odbc_crusher::tests
