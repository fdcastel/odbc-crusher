#pragma once

#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace odbc_crusher::tests {

// Test status
enum class TestStatus {
    PASS,
    FAIL,
    SKIP_UNSUPPORTED,   // Driver doesn't support this optional feature
    SKIP_INCONCLUSIVE,  // Test couldn't determine result
    ERR                 // Changed from ERROR to avoid Windows macro conflict
};

// Severity level
enum class Severity {
    CRITICAL,
    ERR,      // Changed from ERROR to avoid Windows macro conflict
    WARNING,
    INFO
};

// ODBC conformance level
enum class ConformanceLevel {
    CORE,
    LEVEL_1,
    LEVEL_2
};

// Test result
struct TestResult {
    std::string test_name;
    std::string function;              // ODBC function tested
    TestStatus status;
    Severity severity;
    ConformanceLevel conformance = ConformanceLevel::CORE;
    std::string spec_reference;        // e.g. "ODBC 3.x, SQLGetInfo"
    std::string expected;
    std::string actual;
    std::optional<std::string> diagnostic;
    std::optional<std::string> suggestion;
    std::chrono::microseconds duration;
};

// Outcome of TestBase::verify_rows_persisted — whether the inserted rows
// actually made it to storage.
struct RowVerification {
    bool ok = false;
    long actual_count = -1;                  // SELECT COUNT(*), -1 on error
    std::vector<std::string> actual_values;  // Per-row value column, PK-ordered
    std::string diagnostic;                  // Empty when ok
};

// Base class for all ODBC tests
class TestBase {
public:
    explicit TestBase(core::OdbcConnection& conn)
        : conn_(conn) {}

    virtual ~TestBase() = default;

    // Run all tests in this category
    virtual std::vector<TestResult> run() = 0;

    // Get test category name
    virtual std::string category_name() const = 0;

protected:
    core::OdbcConnection& conn_;

    // Helper to create test result
    TestResult make_result(
        const std::string& test_name,
        const std::string& function,
        TestStatus status,
        const std::string& expected,
        const std::string& actual,
        Severity severity = Severity::INFO,
        ConformanceLevel conformance = ConformanceLevel::CORE,
        const std::string& spec_reference = ""
    );

    // Helper to time a test
    template<typename Func>
    auto time_test(Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }

    // Run a single test method. Wraps the body in (a) timing,
    // (b) make_result for the metadata, and (c) a catch for OdbcError that
    // would otherwise unwind out of the category. The body is a callable
    // taking `TestResult&` — it mutates `r.status`, `r.actual`, etc., and
    // may early-`return;` to short-circuit. By default an uncaught
    // OdbcError becomes an ERR (test infrastructure problem); pass
    // `TestStatus::FAIL` for tests where a thrown error means the driver
    // failed the probe.
    //
    // Replaces the ~10 lines of make_result + start_time + try/catch +
    // duration_cast that used to wrap every test.
    template <typename Func>
    TestResult run_test(
        const std::string& test_name,
        const std::string& function,
        const std::string& expected,
        Severity severity,
        ConformanceLevel conformance,
        const std::string& spec_reference,
        Func&& body,
        TestStatus on_odbc_error = TestStatus::ERR
    ) {
        TestResult result = make_result(test_name, function, TestStatus::PASS,
                                        expected, "", severity, conformance,
                                        spec_reference);
        auto start = std::chrono::high_resolution_clock::now();
        try {
            body(result);
        } catch (const core::OdbcError& e) {
            result.status = on_odbc_error;
            result.actual = e.what();
            result.diagnostic = e.format_diagnostics();
            if (on_odbc_error == TestStatus::FAIL && severity > Severity::ERR) {
                // ERR severity is the convention when an OdbcError breaks
                // a passing-by-default test — upgrade INFO/WARNING that
                // the metadata defaulted to without losing CRITICAL/ERR
                // when the caller deliberately chose those.
                result.severity = Severity::ERR;
            }
        }
        auto end = std::chrono::high_resolution_clock::now();
        result.duration =
            std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return result;
    }

    // Verify that rows written by the test actually made it to storage.
    // Runs `SELECT COUNT(*) FROM table` and
    // `SELECT value_col FROM table ORDER BY pk_col`, then returns the
    // actual count and values. Tests comparing against expected values
    // should treat any mismatch as a hard FAIL (CRITICAL) — never SKIP.
    // Without this, drivers that silently drop rows still report PASS.
    RowVerification verify_rows_persisted(
        const std::string& table,
        const std::string& pk_col,
        const std::string& value_col,
        long expected_count);
};

// Helper to convert conformance level to string
inline const char* conformance_to_string(ConformanceLevel level) {
    switch (level) {
        case ConformanceLevel::CORE: return "Core";
        case ConformanceLevel::LEVEL_1: return "Level 1";
        case ConformanceLevel::LEVEL_2: return "Level 2";
        default: return "Unknown";
    }
}

// Helper to convert test status to string
inline const char* status_to_string(TestStatus status) {
    switch (status) {
        case TestStatus::PASS: return "PASS";
        case TestStatus::FAIL: return "FAIL";
        case TestStatus::SKIP_UNSUPPORTED: return "SKIP_UNSUPPORTED";
        case TestStatus::SKIP_INCONCLUSIVE: return "SKIP_INCONCLUSIVE";
        case TestStatus::ERR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// True for any SKIP_* variant.
inline bool is_skipped(TestStatus status) {
    return status == TestStatus::SKIP_UNSUPPORTED
        || status == TestStatus::SKIP_INCONCLUSIVE;
}

// Helper to convert severity to string
inline const char* severity_to_string(Severity sev) {
    switch (sev) {
        case Severity::CRITICAL: return "CRITICAL";
        case Severity::ERR: return "ERROR";
        case Severity::WARNING: return "WARNING";
        case Severity::INFO: return "INFO";
        default: return "UNKNOWN";
    }
}

} // namespace odbc_crusher::tests
