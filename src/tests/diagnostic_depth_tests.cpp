#include "diagnostic_depth_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> DiagnosticDepthTests::run() {
    return {
        test_diagfield_sqlstate(),
        test_diagfield_record_count(),
        test_diagfield_row_count(),
        test_diagfield_return_code(),
        test_diagfield_dynamic_function(),
        test_diagfield_cursor_row_count(),
        test_multiple_diagnostic_records()
    };
}

TestResult DiagnosticDepthTests::test_diagfield_sqlstate() {
    return run_test(
        "test_diagfield_sqlstate", "SQLGetDiagField",
        "SQLGetDiagField with SQL_DIAG_SQLSTATE returns 5-char state",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetDiagField: SQLSTATE is a 5-character string",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Generate an error by executing invalid SQL
            SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf("THIS IS INVALID SQL SYNTAX !@#$").ptr(), SQL_NTS);

            if (SQL_SUCCEEDED(ret)) {
                // Driver accepted it - try a different approach
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate an error to test diagnostics";
                return;
            }

            // Now use SQLGetDiagField to get SQLSTATE
            SQLWCHAR sqlstate[6] = {0};
            SQLSMALLINT len = 0;
            SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
                1, SQL_DIAG_SQLSTATE, sqlstate, sizeof(sqlstate), &len);

            if (SQL_SUCCEEDED(diag_ret)) {
                // Verify it's a 5-char SQLSTATE
                // Count SQLWCHAR chars
                int char_count = 0;
                for (int i = 0; i < 5 && sqlstate[i] != 0; i++) char_count++;

                std::ostringstream actual;
                actual << "SQLSTATE has " << char_count << " chars";
                r.actual = actual.str();

                if (char_count != 5) {
                    r.status = TestStatus::FAIL;
                    r.suggestion = "SQLSTATE must be exactly 5 characters per ODBC spec";
                }
            } else {
                // B3: not inconclusive. SQLExecDirectW returned SQL_ERROR just
                // above, so the spec requires diagnostic record 1 to exist —
                // a driver that cannot produce it has failed a Core
                // requirement, and SKIP would keep that out of the exit code.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLGetDiagField(record 1, SQL_DIAG_SQLSTATE) returned " +
                           std::to_string(diag_ret) + " after a SQL_ERROR";
                r.suggestion = "Every SQL_ERROR must be accompanied by at least "
                               "one diagnostic record, numbered from 1.";
            }
        });
}

TestResult DiagnosticDepthTests::test_diagfield_record_count() {
    return run_test(
        "test_diagfield_record_count", "SQLGetDiagField",
        "SQLGetDiagField with SQL_DIAG_NUMBER returns correct record count",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetDiagField: SQL_DIAG_NUMBER returns count of records",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Generate an error
            SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf("THIS IS INVALID SQL !@#$").ptr(), SQL_NTS);

            if (SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate error for diagnostic test";
                return;
            }

            // Get diagnostic record count (header field, rec number = 0).
            //
            // A25: this was an SQLLEN, which is 8 bytes on LP64 and LLP64
            // while SQL_DIAG_NUMBER is an SQLINTEGER header field — 4. The
            // driver wrote 4 bytes into an 8-byte slot, and it worked only
            // because the variable was zero-initialised and the platform is
            // little-endian. A big-endian build, or a driver that does not
            // pre-clear, would have read garbage in the high half.
            SQLINTEGER diag_count = 0;
            SQLSMALLINT len = 0;
            SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
                0, SQL_DIAG_NUMBER, &diag_count, SQL_IS_INTEGER, &len);

            if (SQL_SUCCEEDED(diag_ret)) {
                std::ostringstream actual;
                actual << "SQL_DIAG_NUMBER returned " << diag_count << " diagnostic record(s)";
                r.actual = actual.str();

                if (diag_count < 1) {
                    r.status = TestStatus::FAIL;
                    r.suggestion = "After an error, SQL_DIAG_NUMBER should be >= 1";
                }
            } else {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLGetDiagField for SQL_DIAG_NUMBER did not succeed";
            }
        });
}

TestResult DiagnosticDepthTests::test_diagfield_row_count() {
    return run_test(
        "test_diagfield_row_count", "SQLGetDiagField",
        "SQLGetDiagField with SQL_DIAG_ROW_COUNT returns row count after query",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetDiagField: SQL_DIAG_ROW_COUNT reports affected rows",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Execute a SELECT to generate a result set
            // Try multiple queries for cross-database compatibility
            std::vector<std::string> queries = {
                "SELECT * FROM CUSTOMERS",
                "SELECT * FROM RDB$DATABASE",
                "SELECT 1 AS COL1"
            };
            bool executed = false;
            for (const auto& q : queries) {
                SQLRETURN rc = SQLExecDirectW(stmt.get_handle(),
                    SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
                if (SQL_SUCCEEDED(rc)) { executed = true; break; }
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }

            if (!executed) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for row count test";
                return;
            }

            // Get row count from header diagnostic field
            SQLLEN row_count = -1;
            SQLSMALLINT len = 0;
            SQLRETURN diag_ret = SQLGetDiagFieldW(SQL_HANDLE_STMT, stmt.get_handle(),
                0, SQL_DIAG_ROW_COUNT, &row_count, 0, &len);

            std::ostringstream actual;
            if (SQL_SUCCEEDED(diag_ret)) {
                actual << "SQL_DIAG_ROW_COUNT = " << row_count;
            } else {
                actual << "SQLGetDiagField for SQL_DIAG_ROW_COUNT returned " << diag_ret;
            }
            r.actual = actual.str();

            // Row count may be -1 or 0 for SELECT — this is driver-defined
            // B1: the excuse was "may not be available for all statement
            // types", but the statement here is an executed SELECT, which is
            // exactly the case the header field is defined for. A driver that
            // cannot answer it is failing a Core diagnostic read.
            if (!SQL_SUCCEEDED(diag_ret)) {
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetDiagField(SQL_DIAG_ROW_COUNT)");
                return;
            }

            // D22: the probe stopped there, so a driver that answered every
            // read with a hard-coded 0 passed it — which is exactly what the
            // mock did, because the statement's row count lived in a field
            // that shadowed the one SQLGetDiagField read. The value itself is
            // driver-defined for a SELECT, but SQL_DIAG_ROW_COUNT and
            // SQLRowCount describe the *same* statement and must agree; that
            // is checkable without knowing anything about the driver's data.
            SQLLEN from_api = -12345;
            const SQLRETURN rc_ret = SQLRowCount(stmt.get_handle(), &from_api);
            if (!SQL_SUCCEEDED(rc_ret)) {
                actual << "; SQLRowCount returned " << rc_ret;
                r.actual = actual.str();
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLRowCount");
                return;
            }
            actual << "; SQLRowCount = " << from_api;
            r.actual = actual.str();
            if (row_count != from_api) {
                r.status = TestStatus::FAIL;
                r.suggestion =
                    "SQL_DIAG_ROW_COUNT and SQLRowCount report the row count "
                    "of the same statement and must return the same value. "
                    "Two different numbers usually means the diagnostic "
                    "header field is not wired to the statement's row count "
                    "at all.";
            }
        });
}

// ── B11 — the header fields no probe used to read ────────────────────────
//
// The tool read SQL_DIAG_ROW_COUNT and nothing else from record 0. The mock
// had SQL_DIAG_RETURNCODE, SQL_DIAG_DYNAMIC_FUNCTION and
// SQL_DIAG_CURSOR_ROW_COUNT wrong — declared, read, and written by nobody —
// until D22, and no probe could tell. A driver that never writes them would
// ship the same way.

TestResult DiagnosticDepthTests::test_diagfield_return_code() {
    return run_test(
        "test_diagfield_return_code", "SQLGetDiagField",
        "SQL_DIAG_RETURNCODE reports the return code of the failed call, "
        "not SQL_SUCCESS",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetDiagField: SQL_DIAG_RETURNCODE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // A statement no engine will accept. If a driver somehow does,
            // there is nothing to measure here.
            const SQLRETURN exec_ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf("THIS IS INVALID SQL SYNTAX !@#$").ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(exec_ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Driver accepted deliberately invalid SQL, so there "
                           "is no failed call to read a return code from";
                return;
            }

            SQLRETURN reported = SQL_SUCCESS;
            SQLSMALLINT len = 0;
            const SQLRETURN diag_ret = SQLGetDiagFieldW(
                SQL_HANDLE_STMT, stmt.get_handle(), 0, SQL_DIAG_RETURNCODE,
                &reported, 0, &len);
            if (!SQL_SUCCEEDED(diag_ret)) {
                r.actual = "SQLGetDiagField(SQL_DIAG_RETURNCODE) returned "
                         + std::to_string(diag_ret);
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetDiagField(SQL_DIAG_RETURNCODE)");
                return;
            }

            std::ostringstream oss;
            oss << "SQLExecDirect returned " << exec_ret
                << "; SQL_DIAG_RETURNCODE = " << reported;
            r.actual = oss.str();
            if (reported == exec_ret) {
                // A23-style: measured, not assumed. Against a driver that
                // deliberately reports SQL_SUCCESS in this field (the mock
                // before D22 did exactly that), the probe still read
                // SQL_ERROR - so on Windows the driver manager answers
                // SQL_DIAG_RETURNCODE itself and the agreement is a fact
                // about the stack, not about the driver. Reported, not
                // scored (B2). The disagreement branch below stays scored:
                // if a mismatch ever does reach the application, it is worth
                // failing on whoever produced it.
                r.status = TestStatus::INFORMATIONAL;
                return;
            }
            if (reported != exec_ret) {
                r.status = TestStatus::FAIL;
                r.suggestion =
                    "SQL_DIAG_RETURNCODE is the return code of the last "
                    "function called on the handle. Reporting SQL_SUCCESS "
                    "after a call that returned SQL_ERROR usually means the "
                    "field is never written at all.";
            }
        });
}

TestResult DiagnosticDepthTests::test_diagfield_dynamic_function() {
    return run_test(
        "test_diagfield_dynamic_function", "SQLGetDiagField",
        "SQL_DIAG_DYNAMIC_FUNCTION_CODE names the statement just executed",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetDiagField: SQL_DIAG_DYNAMIC_FUNCTION_CODE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // A SELECT is the one statement every engine in the matrix runs,
            // and its code is unambiguous.
            const auto variants = literal_select_variants("SELECT 1");
            bool executed = false;
            for (const auto& q : variants) {
                if (SQL_SUCCEEDED(SQLExecDirectW(stmt.get_handle(),
                                                 SqlWcharBuf(q.c_str()).ptr(),
                                                 SQL_NTS))) {
                    executed = true;
                    break;
                }
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
            if (!executed) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No SELECT variant executed, so there is no "
                           "statement to name";
                return;
            }

            SQLINTEGER code = -1;
            SQLSMALLINT len = 0;
            const SQLRETURN diag_ret = SQLGetDiagFieldW(
                SQL_HANDLE_STMT, stmt.get_handle(), 0,
                SQL_DIAG_DYNAMIC_FUNCTION_CODE, &code, 0, &len);
            if (!SQL_SUCCEEDED(diag_ret)) {
                r.actual = "SQLGetDiagField(SQL_DIAG_DYNAMIC_FUNCTION_CODE) "
                           "returned " + std::to_string(diag_ret);
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetDiagField(SQL_DIAG_DYNAMIC_FUNCTION_CODE)");
                return;
            }

            std::ostringstream oss;
            oss << "SQL_DIAG_DYNAMIC_FUNCTION_CODE = " << code
                << " (expected " << SQL_DIAG_SELECT_CURSOR
                << ", SQL_DIAG_SELECT_CURSOR)";
            r.actual = oss.str();
            if (code != SQL_DIAG_SELECT_CURSOR) {
                r.status = TestStatus::FAIL;
                r.suggestion =
                    code == SQL_DIAG_UNKNOWN_STATEMENT
                    ? "The driver reports SQL_DIAG_UNKNOWN_STATEMENT after a "
                      "SELECT. That code means \"I do not know what this "
                      "statement was\", which is rarely true of the statement "
                      "the driver just parsed and executed."
                    : "SQL_DIAG_DYNAMIC_FUNCTION_CODE should be "
                      "SQL_DIAG_SELECT_CURSOR after a SELECT that opened a "
                      "cursor.";
            }
        });
}

TestResult DiagnosticDepthTests::test_diagfield_cursor_row_count() {
    return run_test(
        "test_diagfield_cursor_row_count", "SQLGetDiagField",
        "SQL_DIAG_CURSOR_ROW_COUNT agrees with the number of rows fetched",
        Severity::WARNING, ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLGetDiagField: SQL_DIAG_CURSOR_ROW_COUNT",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            const auto variants = literal_select_variants("SELECT 1");
            bool executed = false;
            for (const auto& q : variants) {
                if (SQL_SUCCEEDED(SQLExecDirectW(stmt.get_handle(),
                                                 SqlWcharBuf(q.c_str()).ptr(),
                                                 SQL_NTS))) {
                    executed = true;
                    break;
                }
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
            if (!executed) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No SELECT variant executed";
                return;
            }

            SQLLEN reported = -1;
            SQLSMALLINT len = 0;
            const SQLRETURN diag_ret = SQLGetDiagFieldW(
                SQL_HANDLE_STMT, stmt.get_handle(), 0,
                SQL_DIAG_CURSOR_ROW_COUNT, &reported, 0, &len);

            // Count what the cursor actually holds. The loop must accept
            // SQL_SUCCESS_WITH_INFO: a driver that posts a per-row warning
            // still delivered the row, and `== SQL_SUCCESS` would stop at the
            // first one and then blame the driver for a row count the cursor
            // does have. The e2e harness has a scenario for exactly this
            // mistake, and it caught this probe making it.
            SQLLEN fetched = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) ++fetched;
            SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);

            if (!SQL_SUCCEEDED(diag_ret)) {
                r.actual = "SQLGetDiagField(SQL_DIAG_CURSOR_ROW_COUNT) "
                           "returned " + std::to_string(diag_ret);
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion =
                    "SQL_DIAG_CURSOR_ROW_COUNT is only defined for drivers "
                    "that can count a cursor's rows without fetching them; a "
                    "driver that declines to answer is not violating Core.";
                return;
            }

            std::ostringstream oss;
            oss << "SQL_DIAG_CURSOR_ROW_COUNT = " << reported
                << "; rows actually fetched = " << fetched;
            r.actual = oss.str();

            // A driver that cannot know the count before fetching reports 0
            // or -1, which is legitimate. A positive number that disagrees
            // with the cursor is not.
            if (reported > 0 && reported != fetched) {
                r.status = TestStatus::FAIL;
                r.suggestion =
                    "SQL_DIAG_CURSOR_ROW_COUNT reported a row count the "
                    "cursor does not have. Reporting 0 or -1 for \"not "
                    "known without fetching\" is fine; reporting a different "
                    "positive number is not.";
            }
        });
}

TestResult DiagnosticDepthTests::test_multiple_diagnostic_records() {
    return run_test(
        "test_multiple_diagnostic_records", "SQLGetDiagRec",
        "Multiple diagnostic records from a single operation",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 Diagnostic Records: Multiple records can exist per error",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Execute invalid SQL to generate diagnostics
            SQLRETURN ret = SQLExecDirectW(stmt.get_handle(),
                SqlWcharBuf("INVALID SQL THAT SHOULD FAIL !@#$").ptr(), SQL_NTS);

            if (SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not generate error for multiple-record test";
                return;
            }

            // Iterate all diagnostic records
            int rec_count = 0;
            for (SQLSMALLINT i = 1; i <= 10; i++) {
                SQLWCHAR sqlstate[6] = {0};
                SQLINTEGER native_error = 0;
                SQLWCHAR message[512] = {0};
                SQLSMALLINT msg_len = 0;

                SQLRETURN diag_ret = SQLGetDiagRecW(SQL_HANDLE_STMT, stmt.get_handle(),
                    i, sqlstate, &native_error, message,
                    sizeof(message)/sizeof(SQLWCHAR), &msg_len);

                if (diag_ret == SQL_NO_DATA) break;
                if (SQL_SUCCEEDED(diag_ret)) rec_count++;
            }

            std::ostringstream actual;
            actual << "Found " << rec_count << " diagnostic record(s) after error";
            r.actual = actual.str();

            if (rec_count < 1) {
                r.status = TestStatus::FAIL;
                r.suggestion = "At least 1 diagnostic record should exist after an error";
            }
        });
}

} // namespace odbc_crusher::tests
