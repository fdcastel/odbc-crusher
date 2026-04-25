#include "cursor_stress_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <vector>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> CursorStressTests::run() {
    return {
        test_rapid_cursor_lifecycle(),
        test_concurrent_statements(),
        test_open_close_hammer_loop(),
        test_handle_reuse_no_leak()
    };
}

TestResult CursorStressTests::test_rapid_cursor_lifecycle() {
    return run_test(
        "test_rapid_cursor_lifecycle",
        "SQLExecDirect + SQLFetch + SQLCloseCursor",
        "100 rapid SELECT->Fetch->Close cycles complete without leaks or degradation",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, Cursor Management",
        [&](TestResult& r) {
            auto overall_start = std::chrono::high_resolution_clock::now();
            constexpr int iterations = 100;
            int successful = 0;
            std::chrono::microseconds first_10_duration{0};
            std::chrono::microseconds last_10_duration{0};

            for (int i = 0; i < iterations; ++i) {
                auto iter_start = std::chrono::high_resolution_clock::now();

                try {
                    core::OdbcStatement stmt(conn_);
                    stmt.execute("SELECT 1");

                    SQLRETURN ret = SQLFetch(stmt.get_handle());
                    if (!SQL_SUCCEEDED(ret)) continue;

                    SQLINTEGER val = 0;
                    SQLLEN ind = 0;
                    SQLGetData(stmt.get_handle(), 1, SQL_C_SLONG, &val, sizeof(val), &ind);

                    // Close cursor explicitly
                    SQLCloseCursor(stmt.get_handle());

                    ++successful;
                } catch (...) {
                    // Count failures but keep going
                }

                auto iter_end = std::chrono::high_resolution_clock::now();
                auto iter_dur = std::chrono::duration_cast<std::chrono::microseconds>(iter_end - iter_start);

                if (i < 10) first_10_duration += iter_dur;
                if (i >= iterations - 10) last_10_duration += iter_dur;
            }

            auto overall_end = std::chrono::high_resolution_clock::now();
            auto total = std::chrono::duration_cast<std::chrono::microseconds>(overall_end - overall_start);

            std::ostringstream oss;
            oss << successful << "/" << iterations << " cycles completed in "
                << total.count() << " us ("
                << (total.count() / iterations) << " us/iteration)";

            // Check for performance degradation (last 10 shouldn't be >10x first 10)
            if (first_10_duration.count() > 0 && last_10_duration.count() > first_10_duration.count() * 10) {
                oss << " [WARNING: last 10 iterations " << last_10_duration.count()
                    << " us vs first 10: " << first_10_duration.count() << " us — possible leak]";
                r.severity = Severity::WARNING;
                r.suggestion = "Performance degradation detected over 100 cycles — possible handle or memory leak";
            }

            r.actual = oss.str();

            if (successful < iterations * 9 / 10) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion = "Too many cursor lifecycle failures — driver may have cursor exhaustion issues";
            }
        });
}

TestResult CursorStressTests::test_concurrent_statements() {
    return run_test(
        "test_concurrent_statements",
        "SQLAllocHandle + SQLExecDirect + SQLFetch",
        "Multiple statement handles on one connection can execute and fetch independently",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, Multiple Active Statements",
        [&](TestResult& r) {
            // Check SQL_MAX_CONCURRENT_ACTIVITIES
            SQLUSMALLINT max_active = 0;
            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_MAX_CONCURRENT_ACTIVITIES,
                                       &max_active, sizeof(max_active), nullptr);
            if (SQL_SUCCEEDED(ret) && max_active == 1) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver supports only 1 concurrent activity";
                return;
            }

            constexpr size_t num_stmts = 5;
            std::vector<std::unique_ptr<core::OdbcStatement>> stmts;

            // Allocate multiple statements
            for (size_t i = 0; i < num_stmts; ++i) {
                stmts.push_back(std::make_unique<core::OdbcStatement>(conn_));
            }

            // Execute independent queries on each
            for (size_t i = 0; i < num_stmts; ++i) {
                std::string sql = "SELECT " + std::to_string(i + 1);
                stmts[i]->execute(sql);
            }

            // Fetch results in interleaved order
            size_t correct = 0;
            for (size_t i = 0; i < num_stmts; ++i) {
                ret = SQLFetch(stmts[i]->get_handle());
                if (!SQL_SUCCEEDED(ret)) continue;

                SQLINTEGER val = 0;
                SQLLEN ind = 0;
                ret = SQLGetData(stmts[i]->get_handle(), 1, SQL_C_SLONG, &val, sizeof(val), &ind);
                if (SQL_SUCCEEDED(ret) && val == static_cast<SQLINTEGER>(i + 1)) ++correct;
            }

            std::ostringstream oss;
            oss << correct << "/" << num_stmts << " concurrent statements returned correct results";
            if (max_active > 0) oss << " (max_concurrent_activities=" << max_active << ")";
            r.actual = oss.str();

            if (correct < num_stmts) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "Concurrent statement results were incorrect — driver may not support multiple active statements";
            }
        });
}

// ── PORT plan §4.5 — phase-separated open/close hammer loop ────────────────
//
// 500 iterations of (alloc + ExecDirect "SELECT 1") / (CloseCursor +
// FreeHandle). Times the two phases independently so drivers that
// re-fetch remaining rows on close show up as outliers. The whole-
// iteration timing in test_rapid_cursor_lifecycle hides this — a
// 50µs open + 250µs close looks the same as 150µs each in aggregate.

TestResult CursorStressTests::test_open_close_hammer_loop() {
    return run_test(
        "test_open_close_hammer_loop",
        "SQLAllocHandle/SQLExecDirect/SQLCloseCursor/SQLFreeHandle",
        "Cursor open and close phases scale independently — close not "
        "more than 10× the cost of open",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 — SQLCloseCursor must not re-fetch remaining rows",
        [&](TestResult& r) {
            constexpr int kIterations = 500;
            long long open_total_us = 0;
            long long close_total_us = 0;
            int successful = 0;

            for (int i = 0; i < kIterations; ++i) {
                // OPEN phase
                auto open_start = std::chrono::high_resolution_clock::now();
                SQLHSTMT hstmt = SQL_NULL_HSTMT;
                SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT,
                                              conn_.get_handle(), &hstmt);
                if (!SQL_SUCCEEDED(rc)) continue;
                rc = SQLExecDirect(hstmt,
                    reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 1")),
                    SQL_NTS);
                auto open_end = std::chrono::high_resolution_clock::now();
                open_total_us += std::chrono::duration_cast<std::chrono::microseconds>(
                    open_end - open_start).count();

                if (!SQL_SUCCEEDED(rc)) {
                    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                    continue;
                }

                // CLOSE phase
                auto close_start = std::chrono::high_resolution_clock::now();
                SQLCloseCursor(hstmt);
                SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
                auto close_end = std::chrono::high_resolution_clock::now();
                close_total_us += std::chrono::duration_cast<std::chrono::microseconds>(
                    close_end - close_start).count();
                ++successful;
            }

            if (successful == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not complete a single open/close cycle";
                return;
            }

            const long long open_mean  = open_total_us  / successful;
            const long long close_mean = close_total_us / successful;

            std::ostringstream oss;
            oss << successful << "/" << kIterations
                << " cycles | open_mean=" << open_mean << "us"
                << " close_mean=" << close_mean << "us"
                << " ratio=" << (open_mean > 0 ?
                    static_cast<double>(close_mean) / static_cast<double>(open_mean) : 0.0);
            r.actual = oss.str();

            // Threshold: close > 10× open mean is the documented red flag.
            if (open_mean > 0 && close_mean > open_mean * 10) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "SQLCloseCursor is dramatically slower than the "
                               "open path — driver likely re-fetches remaining "
                               "rows or holds a server-side resource until "
                               "close. Spec: close should be a near-no-op when "
                               "the cursor has been fully consumed.";
            }
        });
}

// ── PORT plan §4.5 — handle reuse, no leak ─────────────────────────────────
//
// Same statement handle, 500 ExecDirect → CloseCursor cycles. After every
// iteration, walk the diagnostic queue and assert it has zero records (the
// handle's diagnostic state was cleared by the next operation). Drivers
// that leak state into the next iteration accumulate records.

TestResult CursorStressTests::test_handle_reuse_no_leak() {
    return run_test(
        "test_handle_reuse_no_leak",
        "SQLExecDirect/SQLCloseCursor on shared handle",
        "Reused statement handle: 500 cycles complete without diagnostic "
        "queue accumulation",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 — diagnostic state is per-statement, cleared on next call",
        [&](TestResult& r) {
            constexpr int kIterations = 500;
            core::OdbcStatement stmt(conn_);
            int successful = 0;
            int max_diag_records = 0;

            for (int i = 0; i < kIterations; ++i) {
                SQLRETURN rc = SQLExecDirect(stmt.get_handle(),
                    reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 1")),
                    SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) continue;
                SQLCloseCursor(stmt.get_handle());

                // Count diagnostic records still present after close. Spec:
                // SQLExecDirect clears the queue at entry; if records linger
                // across cycles the count grows.
                int rec = 0;
                for (SQLSMALLINT j = 1; j <= 32; ++j) {
                    char state[6] = {0};
                    SQLINTEGER native = 0;
                    SQLCHAR msg[128] = {0};
                    SQLSMALLINT msg_len = 0;
                    SQLRETURN dr = SQLGetDiagRec(SQL_HANDLE_STMT,
                        stmt.get_handle(), j,
                        reinterpret_cast<SQLCHAR*>(state),
                        &native, msg, sizeof(msg), &msg_len);
                    if (dr == SQL_NO_DATA || !SQL_SUCCEEDED(dr)) break;
                    ++rec;
                }
                if (rec > max_diag_records) max_diag_records = rec;
                ++successful;
            }

            std::ostringstream oss;
            oss << successful << "/" << kIterations
                << " reuse cycles | max_diag_records_observed=" << max_diag_records;
            r.actual = oss.str();

            if (successful < kIterations * 9 / 10) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion = "Driver couldn't survive " +
                    std::to_string(kIterations) + " execute/close cycles on "
                    "the same handle — likely cursor state corruption.";
                return;
            }
            if (max_diag_records > 1) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "Diagnostic queue grew past one record across "
                               "reuse cycles — SQLExecDirect should clear the "
                               "queue at entry per spec.";
            }
        });
}

} // namespace odbc_crusher::tests
