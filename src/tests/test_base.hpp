#pragma once

#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <typeinfo>

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

// Result of turning a driver-filled character buffer into a std::string
// without trusting the driver's reported length — A3.
struct BoundedString {
    std::string value;
    bool truncated = false;        // driver had more than the buffer could hold
    bool length_unknown = false;   // driver returned SQL_NO_TOTAL, or a negative
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

    // Build a std::string from a buffer the driver filled, using the length
    // the driver reported — safely. A3.
    //
    // The reported length is *total available* bytes, not bytes written, so on
    // truncation it exceeds the buffer; `SQL_SUCCEEDED` accepts the 01004
    // warning that accompanies it, so callers reached `std::string(buf, len)`
    // with a length past the end of their own stack buffer. `SQL_NO_TOTAL`
    // (-4) is worse: converted to size_t it becomes SIZE_MAX - 3.
    //
    // In a tool whose stated philosophy is "never crash" — and where main.cpp's
    // crash guard would have reported the resulting fault as a *driver* crash.
    //
    // `capacity` is the full buffer size (use sizeof); one byte is reserved for
    // the terminator, matching what a driver is allowed to write.
    static BoundedString bounded_string(const char* buf, size_t capacity,
                                        SQLLEN reported);


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
        } catch (const std::exception& e) {
            // A9. Anything that is not an OdbcError used to unwind out of
            // the category, through crash_guard (which deliberately lets C++
            // exceptions past), to main's `return 3` — discarding all 195
            // results and, with `-o json -f`, writing no file at all, because
            // the report is only serialised in report_end(). One std::bad_alloc
            // in probe #4 lost the other 191. Worse, the crash guard reported
            // it as a *driver* crash. AGENTS.md says this tool never crashes.
            //
            // A non-OdbcError escaping a probe is a bug in the probe or an
            // exhausted resource, not a driver verdict, so it is always ERR
            // regardless of the caller's on_odbc_error choice.
            result.status = TestStatus::ERR;
            result.actual = std::string("Unhandled ") + typeid(e).name() +
                            ": " + e.what();
            result.diagnostic =
                "The probe threw a non-ODBC exception. This is a defect in "
                "odbc-crusher (or an exhausted system resource), not a finding "
                "about the driver under test.";
            if (severity > Severity::ERR) result.severity = Severity::ERR;
        } catch (...) {
            // Non-std exception types cannot carry a message, but they must
            // not be allowed to destroy the run either. This does not — and
            // must not — catch SEH faults on MSVC: those are the crash
            // guard's job, and an access violation inside the driver is a
            // genuine driver finding.
            result.status = TestStatus::ERR;
            result.actual = "Unhandled non-std::exception thrown by the probe";
            result.diagnostic =
                "The probe threw an object not derived from std::exception. "
                "This is a defect in odbc-crusher, not a finding about the "
                "driver under test.";
            if (severity > Severity::ERR) result.severity = Severity::ERR;
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

// RAII guard for a round-trip test table.
//
// Generalizes the CREATE-with-fallback / DROP-on-scope-exit pattern that
// param_binding_tests, transaction_tests, and array_param_tests each
// re-implement. New ports (numeric byte-equality round-trip, NULL-vs-empty
// distinction, NVARCHAR Unicode round-trip, etc.) use this helper directly
// so that early-return on a probe failure still drops the table.
//
// Construction:
//   - Saves SQL_ATTR_AUTOCOMMIT, sets it ON for DDL.
//   - Tries `CREATE TABLE <name> (ID <id_ddl_variants[i]>, VAL <val_ddl>)`
//     for each id_ddl variant in order until one succeeds.
//   - On a failed CREATE, calls `SQLEndTran(SQL_HANDLE_DBC, ROLLBACK)` to
//     unstick Firebird-style "DDL failure poisons the txn" state.
//   - If every variant fails, attempts DROP + retry once (the table likely
//     already exists from a prior aborted run).
//   - Restores autocommit to its prior value.
//
// Destruction:
//   - Best-effort DROP with the same autocommit handling. Errors are
//     swallowed (a missing table on cleanup is not a probe failure).
//
// The guard is non-copyable, non-movable: tests instantiate it on the stack
// inside the run_test body. `ok()` reports whether the table is usable;
// when false, the probe should set SKIP_INCONCLUSIVE with `last_error()`.
class RoundTripTableGuard {
public:
    // Default integer ID column variants — every existing helper in the
    // project tries INTEGER first then INT, so keep that ordering here.
    static const std::vector<std::string>& default_id_ddl_variants();

    RoundTripTableGuard(
        core::OdbcConnection& conn,
        std::string table_name,
        std::string val_ddl,
        const std::vector<std::string>& id_ddl_variants = default_id_ddl_variants());

    ~RoundTripTableGuard();

    RoundTripTableGuard(const RoundTripTableGuard&) = delete;
    RoundTripTableGuard& operator=(const RoundTripTableGuard&) = delete;
    RoundTripTableGuard(RoundTripTableGuard&&) = delete;
    RoundTripTableGuard& operator=(RoundTripTableGuard&&) = delete;

    bool ok() const { return ok_; }
    const std::string& name() const { return table_name_; }
    const std::string& last_error() const { return last_error_; }

private:
    core::OdbcConnection& conn_;
    std::string table_name_;
    std::string val_ddl_;
    bool ok_ = false;
    std::string last_error_;
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
