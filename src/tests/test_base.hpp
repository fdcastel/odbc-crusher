#pragma once

#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"
#include <functional>
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

// One dialect variant that failed to execute, and why — C2.
struct DialectFailure {
    std::string query;
    std::string sqlstate;    // empty when the driver posted no diagnostic
    std::string message;
};

// Outcome of trying a list of dialect variants until one executes — C2.
//
// Prior plan item 2.8 ("collect failing query text in retry loops") was closed
// as "implicitly addressed" by the run_test extraction. It was not: run_test
// centralises the *outer* catch, while ~33 hand-rolled fallback loops each
// carried their own inner `catch (const OdbcError&) { continue; }` that
// discarded the query, the SQLSTATE and the message. When every variant fails,
// the probe reports "No compatible query pattern found" and the report says
// nothing about what the driver actually objected to.
struct DialectAttempt {
    bool executed = false;                   // one of the queries ran
    std::string query;                       // the one that ran; empty if none
    std::vector<DialectFailure> failures;    // every variant that did not

    explicit operator bool() const { return executed; }

    // One line per failed variant, for TestResult::diagnostic.
    std::string format_failures() const;
};

// How a failed ODBC call should be reported — B3.
struct FailureClassification {
    TestStatus  status = TestStatus::FAIL;
    std::string sqlstate;   // the state that decided it; empty if none posted
    std::string message;    // the driver's message for that record
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

    // First SQLSTATE on a handle, or `fallback` when the driver posted no
    // diagnostic — C5.
    //
    // This shape was re-implemented six ways across the tree
    // (sqlstate_tests.cpp twice, state_machine_tests.cpp, advanced_tests.cpp,
    // metadata_tests.cpp twice) plus the file-local copies Phase 2 added while
    // waiting for this. B3's classify_failure() is built on it.
    static std::string first_sqlstate(SQLSMALLINT handle_type, SQLHANDLE handle,
                                      const std::string& fallback = "");

    // Execute the first query in `queries` that the driver accepts — C2.
    //
    // Probes offer the same query in several dialects ("SELECT 42",
    // "SELECT 42 FROM RDB$DATABASE", ...) and take whichever runs. Every one of
    // the ~33 hand-rolled versions of this loop swallowed the failures; this
    // one records them, so a probe that finds no working variant can say why
    // instead of shrugging.
    //
    // It reports only whether a query *executed*. Deciding what the result
    // means is the caller's job — and once a query has executed the probe is
    // conclusive, so a wrong value is a FAIL rather than a reason to try the
    // next dialect (A1).
    DialectAttempt execute_first_working(core::OdbcStatement& stmt,
                                         const std::vector<std::string>& queries);

    // Same, for probes whose loop prepares rather than executes — several
    // parameter-binding probes need the statement prepared so they can bind
    // before executing.
    DialectAttempt prepare_first_working(core::OdbcStatement& stmt,
                                         const std::vector<std::string>& queries);

    // Decide SKIP_UNSUPPORTED vs FAIL from the diagnostics on `handle` — B3.
    //
    // Not one probe in this codebase read a SQLSTATE before choosing between
    // the two, so a driver that failed a *Core* function and a driver that
    // declined an optional one were reported identically — and SKIP does not
    // affect the exit code, so the Core failure disappeared.
    //
    // The four states that mean "this driver does not implement an optional
    // feature" map to SKIP_UNSUPPORTED; everything else is the driver failing
    // something it was asked to do, which is a FAIL.
    //
    // Walks the whole diagnostic queue rather than reading record 1: a driver
    // manager may prepend its own record — unixODBC's IM006 "driver does not
    // support this function" is the one seen in practice — in front of the
    // driver's own. The two probes that already got this right
    // (`array_param_tests.cpp` and `metadata_tests.cpp`) both loop for exactly
    // that reason, and this helper is extracted from them.
    static FailureClassification classify_failure(SQLSMALLINT handle_type,
                                                  SQLHANDLE handle);

    // Apply that classification to a result, always recording the SQLSTATE.
    // `what` names the call that failed, e.g. "SQLTables".
    static void report_failure(TestResult& r, SQLSMALLINT handle_type,
                               SQLHANDLE handle, const std::string& what);

    // Dialect variants of a literal SELECT — A2.
    //
    // 15 of the 23 probe files carried a "... FROM RDB$DATABASE" variant by
    // hand. Four did not, and the consequence differed by file rather than
    // being obviously absent: numeric_struct threw, so 4 probes reported
    // ERROR; cursor_stress swallowed the throw and reported FAIL "cursor
    // exhaustion issues"; the escape probes reported a value of 'NULL'. The
    // whole 20-probe Escape Sequence category, all of Numeric Struct and all
    // of Cursor Stress were unusable against Firebird — the driver family this
    // repository sits inside — and the report did not say so.
    //
    // Built from the bare form rather than duplicating 58 string literals.
    static std::vector<std::string> literal_select_variants(const std::string& sql);

    // Execute a literal SELECT, trying those variants. Throws OdbcError when
    // none of them works — a drop-in for `stmt.execute("SELECT ...")` in
    // probes that already rely on run_test's catch for their error path.
    // Returns the variant that worked.
    std::string execute_literal_select(core::OdbcStatement& stmt,
                                       const std::string& sql);

    // The general form both of the above are written in terms of: run `body`
    // for each query until one completes without throwing OdbcError.
    //
    // Some probes need more than one call to decide whether a variant works —
    // prepare, then bind, then execute — and any of them can be the step this
    // dialect does not support. Throwing from `body` rejects the variant and
    // records why; returning normally accepts it.
    static DialectAttempt try_first_working(
        const std::vector<std::string>& queries,
        const std::function<void(const std::string&)>& body);

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

    // Movable — C10.
    //
    // It used to be non-movable, and two probes in unicode_tests.cpp
    // duplicated their entire body as a result: the author's own comment read
    // "the guard is non-movable, so we restructure: do the work inline
    // instead". Moving transfers ownership of the DROP; the source is left
    // not-ok so its destructor does nothing.
    //
    // Move-assignment stays deleted: the class holds a connection reference,
    // which cannot be rebound.
    RoundTripTableGuard(RoundTripTableGuard&& other) noexcept;
    RoundTripTableGuard& operator=(RoundTripTableGuard&&) = delete;

    // Create the table using the first val-column DDL the engine accepts — C10.
    //
    // The recurring shape is "try NVARCHAR(64), fall back to VARCHAR(64)",
    // which needed a second guard and therefore a second copy of the probe
    // body. Returns a guard that is ok() when one of the variants worked, and
    // carries the last error otherwise.
    static RoundTripTableGuard create_first_working(
        core::OdbcConnection& conn,
        const std::string& table_name,
        const std::vector<std::string>& val_ddl_variants,
        const std::vector<std::string>& id_ddl_variants = default_id_ddl_variants());

    bool ok() const { return ok_; }
    const std::string& name() const { return table_name_; }
    const std::string& last_error() const { return last_error_; }
    // Which val-column DDL was accepted — useful when create_first_working()
    // picked a fallback and the report should say which (C10).
    const std::string& val_ddl() const { return val_ddl_; }

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
