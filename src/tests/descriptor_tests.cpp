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
    return run_test(
        "test_implicit_descriptors",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC/SQL_ATTR_IMP_ROW_DESC)",
        "Retrieve implicit APD, ARD, IPD, IRD descriptor handles",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetStmtAttr, Descriptor Handles",
        [&](TestResult& r) {
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

            // B1: every branch here passed or skipped, and the skip's own
            // suggestion said these handles are Core - so the probe knew the
            // answer and reported it as "unsupported" anyway. All four
            // implicit descriptors exist on every ODBC 3.x statement; a
            // driver that hands back none of them, or three of four, is
            // failing a Core requirement.
            r.actual = std::to_string(obtained) + "/4 implicit descriptor "
                       "handles obtained: " + details.str();
            if (obtained != 4) {
                r.status = TestStatus::FAIL;
                r.severity = obtained == 0 ? Severity::CRITICAL : Severity::ERR;
                r.suggestion =
                    "APD, ARD, IPD and IRD are implicitly allocated with every "
                    "statement in ODBC 3.x and SQLGetStmtAttr must return all "
                    "four. An application cannot inspect or rebind its own "
                    "parameter and row descriptors without them.";
            }
        });
}

TestResult DescriptorTests::test_ird_after_prepare() {
    return run_test(
        "test_ird_after_prepare",
        "SQLGetStmtAttr(SQL_ATTR_IMP_ROW_DESC)/SQLGetDescField",
        "IRD populated with column metadata after SQLPrepare",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLPrepare, IRD Auto-Population",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = prepare_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

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
                        r.status = TestStatus::PASS;
                        r.actual = "IRD has " + std::to_string(count) + " column(s) after SQLPrepare";
                        success = true;
                        break;
                    }
                }            } while (false);

            if (!success) {
                // B1: the suggestion already said what the spec requires;
                // the status said it did not matter. SQL_DESC_COUNT on the
                // IRD after a successful prepare is how an application learns
                // the shape of the result set before executing.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "Could not read SQL_DESC_COUNT from the IRD after "
                           "a successful SQLPrepare";
                r.suggestion =
                    "The IRD is auto-populated with column metadata after "
                    "SQLPrepare. A driver that cannot answer SQL_DESC_COUNT "
                    "there forces an application to execute the statement "
                    "just to find out how many columns it returns.";
            }
        });
}

TestResult DescriptorTests::test_apd_fields() {
    return run_test(
        "test_apd_fields",
        "SQLGetStmtAttr(SQL_ATTR_APP_PARAM_DESC)/SQLSetDescField",
        "APD fields can be set for parameter binding",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLSetDescField, APD",
        [&](TestResult& r) {
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
                        r.status = TestStatus::PASS;
                        r.actual = "APD DESC_COUNT set to 1 and verified";
                    } else {
                        // B1: this was a second PASS - "settable" - on the
                        // branch where the read-back disagreed with the write.
                        // A descriptor field that accepts a value and then
                        // reports a different one is worse than one that
                        // rejects the write: the application has no way to
                        // know its binding did not take.
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::ERR;
                        r.actual = "SQLSetDescField(APD, SQL_DESC_COUNT, 1) "
                                   "succeeded but the read-back returned " +
                                   std::to_string(check_count);
                        r.suggestion =
                            "A descriptor field that was set successfully must "
                            "read back as the value that was set.";
                    }
                } else {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLSetDescField on APD not supported";
                    r.suggestion = "Descriptor field manipulation is Core conformance per ODBC 3.x";
                }
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "APD handle not available";
            }
        });
}

TestResult DescriptorTests::test_copy_desc() {
    return run_test(
        "test_copy_desc", "SQLCopyDesc",
        "Copy descriptor fields between statement handles",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLCopyDesc",
        [&](TestResult& r) {
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
                    r.status = TestStatus::PASS;
                    r.actual = "SQLCopyDesc succeeded between two statement ARDs";
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLCopyDesc failed";
                    r.severity = Severity::WARNING;
                    r.suggestion = "SQLCopyDesc is a Core conformance function per ODBC 3.x SQLCopyDesc";
                }
            } else {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "ARD handles not available for copy";
            }
        });
}

TestResult DescriptorTests::test_auto_populate_after_exec() {
    return run_test(
        "test_auto_populate_after_exec",
        "SQLExecDirect/SQLNumResultCols",
        "Descriptors auto-populated after SQLExecDirect",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLExecDirect, IRD Auto-Population",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            std::vector<std::string> queries = {"SELECT 1", "SELECT 1 FROM RDB$DATABASE"};
            bool success = false;

            auto attempt = execute_first_working(stmt, queries);
            if (!attempt) {
                // C2: nothing executed. Say what each variant failed with,
                // instead of leaving the report to shrug.
                r.diagnostic = attempt.format_failures();
            } else do {
                // do/while(false): the body still uses `break` to mean
                // "stop here", which is what it meant when this was a
                // loop over dialect variants.

                // Check SQLNumResultCols (which reads from IRD)
                SQLSMALLINT num_cols = 0;
                SQLRETURN rc = SQLNumResultCols(stmt.get_handle(), &num_cols);

                if (SQL_SUCCEEDED(rc) && num_cols > 0) {
                    // Also verify column description works (reads from IRD)
                    core::GuardedBuffer<SQLCHAR> col_name(128, 0);  // D62
                    SQLSMALLINT name_len = 0, data_type = 0, nullable = 0;
                    SQLULEN col_size = 0;
                    SQLSMALLINT dec_digits = 0;

                    rc = SQLDescribeCol(stmt.get_handle(), 1,
                        col_name.data(), col_name.declared_bytes(), &name_len,
                        &data_type, &col_size, &dec_digits, &nullable);

                    if (SQL_SUCCEEDED(rc)) {
                        r.status = TestStatus::PASS;
                        r.actual = "After SQLExecDirect: " + std::to_string(num_cols) +
                                       " col(s), type=" + std::to_string(data_type);
                    } else {
                        // B1: a second PASS on the branch where SQLDescribeCol
                        // failed. The probe is about the IRD being populated
                        // after execute, and SQLNumResultCols answering while
                        // SQLDescribeCol does not means it is populated only
                        // half way - which is the defect, not a pass.
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::ERR;
                        r.actual = "SQLNumResultCols reported " +
                                   std::to_string(num_cols) +
                                   " column(s) after execute, but "
                                   "SQLDescribeCol(1) failed - the IRD is "
                                   "only partly populated";
                    }
                    success = true;
                    break;
                }            } while (false);

            if (!success) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query to test descriptor auto-population";
            }
        });
}

} // namespace odbc_crusher::tests
