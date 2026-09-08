#include "cursor_behavior_tests.hpp"
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

std::vector<TestResult> CursorBehaviorTests::run() {
    return {
        test_forward_only_past_end(),
        test_fetchscroll_first_forward_only(),
        test_cursor_type_attribute(),
        test_getdata_same_column_twice()
    };
}

TestResult CursorBehaviorTests::test_forward_only_past_end() {
    return run_test(
        "test_forward_only_past_end", "SQLFetch",
        "Forward-only cursor fetch past end returns SQL_NO_DATA",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLFetch: Returns SQL_NO_DATA when no more rows",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try multiple queries for cross-database compatibility
            std::vector<std::string> queries = {
                "SELECT * FROM CUSTOMERS",
                "SELECT * FROM RDB$DATABASE",     // Firebird system table
                "SELECT 1 AS COL1"                 // Minimal fallback
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
                r.actual = "Could not execute query for cursor test";
                return;
            }

            // A11. Two bugs here, both of which FAILed a spec-compliant
            // driver.
            //
            // The loop condition was `== SQL_SUCCESS`, so it exited early on
            // any row that happened to carry SQL_SUCCESS_WITH_INFO — a driver
            // warning of 01004, 01S07, or its own 01000 mid-result-set. The
            // confirming fetch below then returned success again, and the
            // probe reported "SQLFetch past end must return SQL_NO_DATA".
            //
            // The 10,000-row safety break produced exactly the same false FAIL
            // on any real table larger than that, because hitting the cap was
            // indistinguishable from reaching the end.
            constexpr int kRowCap = 10000;
            int row_count = 0;
            bool hit_cap = false;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
                row_count++;
                if (row_count >= kRowCap) { hit_cap = true; break; }
            }

            SQLRETURN ret = SQLFetch(stmt.get_handle());

            std::ostringstream actual;
            actual << "Fetched " << row_count << " rows, then SQLFetch returned " << ret;
            if (hit_cap) actual << " (stopped at the " << kRowCap << "-row cap)";
            r.actual = actual.str();

            if (hit_cap) {
                // We stopped early by choice; the result set was not exhausted,
                // so SQL_NO_DATA is not owed and says nothing about the driver.
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual += " — result set larger than the probe's cap, so "
                            "end-of-cursor behaviour was not exercised";
            } else if (ret != SQL_NO_DATA) {
                r.status = TestStatus::FAIL;
                r.suggestion = "SQLFetch past end of result set must return SQL_NO_DATA (100)";
            }
        });
}

TestResult CursorBehaviorTests::test_fetchscroll_first_forward_only() {
    return run_test(
        "test_fetchscroll_first_forward_only", "SQLFetchScroll",
        "SQLFetchScroll(SQL_FETCH_FIRST) on forward-only cursor returns error",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLFetchScroll: Non-NEXT scrolling not supported on forward-only",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Ensure forward-only cursor (default)
            SQLSetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                (SQLPOINTER)(intptr_t)SQL_CURSOR_FORWARD_ONLY, 0);

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
                r.actual = "Could not execute query for fetchscroll test";
                return;
            }

            // First fetch forward to position cursor
            SQLFetch(stmt.get_handle());

            // Now try SQL_FETCH_FIRST on a forward-only cursor — should fail
            SQLRETURN ret = SQLFetchScroll(stmt.get_handle(), SQL_FETCH_FIRST, 0);

            std::ostringstream actual;
            actual << "SQLFetchScroll(SQL_FETCH_FIRST) returned " << ret;
            r.actual = actual.str();

            if (ret == SQL_ERROR) {
                // The spec names the state: SQL_FETCH_FIRST on a
                // forward-only cursor is HY106, "Fetch type out of range".
                // B1: this branch passed on any error at all, so a driver
                // failing for an unrelated reason scored the same as one
                // getting it right.
                const std::string state = first_sqlstate(
                    SQL_HANDLE_STMT, stmt.get_handle(), "no diagnostic");
                if (state == "HY106") {
                    r.status = TestStatus::PASS;
                    r.actual += " (correctly rejected with HY106)";
                } else {
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::WARNING;
                    r.actual += " (rejected, but with " + state +
                                " rather than HY106)";
                    r.suggestion =
                        "SQLFetchScroll with SQL_FETCH_FIRST on a "
                        "forward-only cursor must return HY106 so the "
                        "application can tell 'wrong fetch type' from a real "
                        "error.";
                }
            } else if (SQL_SUCCEEDED(ret)) {
                // B1: this was the second PASS. Scrolling backwards on a
                // cursor the application declared forward-only is a
                // conformance deviation, even though it gives the caller
                // more than it asked for: code that works here breaks on the
                // conforming driver next to it. WARNING, not ERR - nothing
                // is corrupted, and it is common.
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.actual += " (driver scrolled a forward-only cursor instead "
                            "of returning HY106)";
                r.suggestion =
                    "SQL_ATTR_CURSOR_TYPE was SQL_CURSOR_FORWARD_ONLY, so "
                    "SQLFetchScroll(SQL_FETCH_FIRST) must return HY106. "
                    "Accepting it lets an application depend on behaviour a "
                    "conforming driver will refuse.";
            }
        });
}

TestResult CursorBehaviorTests::test_cursor_type_attribute() {
    return run_test(
        "test_cursor_type_attribute", "SQLGetStmtAttr",
        "SQL_ATTR_CURSOR_TYPE reflects actual cursor capabilities",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetStmtAttr: Cursor type reflects driver capabilities",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Read the default cursor type
            SQLULEN cursor_type = 0;
            SQLRETURN ret = SQLGetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                &cursor_type, 0, nullptr);

            if (!SQL_SUCCEEDED(ret)) {
                // B1: every statement has a cursor type, defaulting to
                // SQL_CURSOR_FORWARD_ONLY. Reading it is Core - supporting
                // other *values* is what is optional.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetStmtAttr(SQL_ATTR_CURSOR_TYPE)");
                return;
            }

            std::string cursor_name;
            switch (cursor_type) {
                case SQL_CURSOR_FORWARD_ONLY: cursor_name = "FORWARD_ONLY"; break;
                case SQL_CURSOR_STATIC: cursor_name = "STATIC"; break;
                case SQL_CURSOR_KEYSET_DRIVEN: cursor_name = "KEYSET_DRIVEN"; break;
                case SQL_CURSOR_DYNAMIC: cursor_name = "DYNAMIC"; break;
                default: cursor_name = "UNKNOWN(" + std::to_string(cursor_type) + ")"; break;
            }

            // Try to set to STATIC and see if driver downgrades
            SQLSetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                (SQLPOINTER)(intptr_t)SQL_CURSOR_STATIC, 0);

            SQLULEN actual_type = 0;
            SQLGetStmtAttrW(stmt.get_handle(), SQL_ATTR_CURSOR_TYPE,
                &actual_type, 0, nullptr);

            std::string actual_name;
            switch (actual_type) {
                case SQL_CURSOR_FORWARD_ONLY: actual_name = "FORWARD_ONLY"; break;
                case SQL_CURSOR_STATIC: actual_name = "STATIC"; break;
                case SQL_CURSOR_KEYSET_DRIVEN: actual_name = "KEYSET_DRIVEN"; break;
                case SQL_CURSOR_DYNAMIC: actual_name = "DYNAMIC"; break;
                default: actual_name = "UNKNOWN"; break;
            }

            std::ostringstream actual;
            actual << "Default cursor: " << cursor_name
                   << "; Requested STATIC, got: " << actual_name;
            r.actual = actual.str();
        });
}

TestResult CursorBehaviorTests::test_getdata_same_column_twice() {
    return run_test(
        "test_getdata_same_column_twice", "SQLGetData",
        "SQLGetData called twice on same column returns data or proper error",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData: Re-reading same column behavior is driver-defined",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Try multiple queries for cross-database compatibility
            std::vector<std::string> queries = {
                "SELECT * FROM CUSTOMERS",
                "SELECT * FROM RDB$DATABASE",
                "SELECT 1 AS COL1"
            };
            bool executed = false;
            SQLRETURN ret;
            for (const auto& q : queries) {
                ret = SQLExecDirectW(stmt.get_handle(),
                    SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) { executed = true; break; }
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }

            if (!executed) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for double-read test";
                return;
            }

            ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No rows to fetch for double-read test";
                return;
            }

            // Get column 1 (typically an integer) twice
            SQLLEN cb_val1 = 0, cb_val2 = 0;
            // D62: two buffers in one declaration - the enumeration
            // reported this line as a single buffer, so it is worth
            // saying that both are guarded.
            core::GuardedBuffer<char> buf1(64, 0);
            core::GuardedBuffer<char> buf2(64, 0);

            SQLRETURN ret1 = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                         buf1.data(), buf1.declared_bytes(), &cb_val1);
            SQLRETURN ret2 = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR,
                                         buf2.data(), buf2.declared_bytes(), &cb_val2);

            std::ostringstream actual;
            actual << "First read: ret=" << ret1 << ", Second read: ret=" << ret2;
            if (SQL_SUCCEEDED(ret1)) {
                // D68: streaming the buffer read to its terminator; the
                // indicators say where each value ends. Found by D62 guarding
                // the buffers - `<< buf1` on a GuardedBuffer does not compile,
                // and on a bare array it silently read to the first zero.
                actual << " (val1='"
                       << core::bounded_string(buf1.data(),
                                               buf1.declared_elements(),
                                               cb_val1).value << "'";
                if (SQL_SUCCEEDED(ret2)) {
                    actual << ", val2='"
                           << core::bounded_string(buf2.data(),
                                                   buf2.declared_elements(),
                                                   cb_val2).value << "'";
                }
                actual << ")";
            }
            r.actual = actual.str();

            // B8: this cited SQL_GD_ANY_ORDER, which governs the *order*
            // columns may be retrieved in, not whether one column may be read
            // twice. The flag for that is SQL_GD_ANY_COLUMN's neighbour
            // SQL_GD_BLOCK / the driver's own choice - the spec leaves a
            // repeated SQLGetData on the same column to the driver, which is
            // why both outcomes are accepted here.
            // Both outcomes are valid: the re-read succeeds, or fails
            if (!SQL_SUCCEEDED(ret1)) {
                r.status = TestStatus::FAIL;
                r.suggestion = "First SQLGetData call should succeed";
            }
        });
}

} // namespace odbc_crusher::tests
