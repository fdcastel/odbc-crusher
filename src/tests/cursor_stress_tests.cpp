#include "cursor_stress_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <vector>
#include <memory>

namespace odbc_crusher::tests {

std::vector<TestResult> CursorStressTests::run() {
    return {
        test_rapid_cursor_lifecycle(),
        test_concurrent_statements()
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

            constexpr int num_stmts = 5;
            std::vector<std::unique_ptr<core::OdbcStatement>> stmts;

            // Allocate multiple statements
            for (int i = 0; i < num_stmts; ++i) {
                stmts.push_back(std::make_unique<core::OdbcStatement>(conn_));
            }

            // Execute independent queries on each
            for (int i = 0; i < num_stmts; ++i) {
                std::string sql = "SELECT " + std::to_string(i + 1);
                stmts[i]->execute(sql);
            }

            // Fetch results in interleaved order
            int correct = 0;
            for (int i = 0; i < num_stmts; ++i) {
                ret = SQLFetch(stmts[i]->get_handle());
                if (!SQL_SUCCEEDED(ret)) continue;

                SQLINTEGER val = 0;
                SQLLEN ind = 0;
                ret = SQLGetData(stmts[i]->get_handle(), 1, SQL_C_SLONG, &val, sizeof(val), &ind);
                if (SQL_SUCCEEDED(ret) && val == i + 1) ++correct;
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

} // namespace odbc_crusher::tests
