#pragma once

#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include "core/guarded_buffer.hpp"
#include "core/odbc_statement.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <typeinfo>
#include <iostream>

namespace odbc_crusher::tests {

// S2 — say which probe is running, before it runs.
//
// The console reporter prints a whole category at once, after `run()` returns,
// so a probe that never returns prints nothing at all: the two stress-test runs
// of the H17 Firebird pair were killed at the 570 s cap with text reports that
// ended at `Phase 2: Running ODBC tests...`, and a wedged run was
// indistinguishable from a run that produced nothing. Locating the culprit
// needed the Windows driver-manager trace and the last ODBC call with no
// matching EXIT, which is not a reasonable diagnostic path for a tool whose job
// is diagnosis.
//
// stderr, because stdout carries the report — G1's rule, and the e2e harness
// asserts it by capturing the two separately. Flushed, because the whole point
// is what survives a SIGKILL. One line per probe start: not the completion,
// since the interesting probe is the one that never completes.
inline void note_probe_start(const std::string& category,
                             const std::string& probe) {
    std::cerr << "  -> " << category << " / " << probe << '\n' << std::flush;
}


// Test status
enum class TestStatus {
    PASS,
    FAIL,
    SKIP_UNSUPPORTED,   // Driver doesn't support this optional feature
    SKIP_INCONCLUSIVE,  // Test couldn't determine result
    ERR,                // Changed from ERROR to avoid Windows macro conflict

    // Reported, but not scored — B2.
    //
    // Some probes cannot fail, and should not: they record what the driver
    // said (a SQLGetInfo bitmask, a raw byte layout, a row count the spec
    // leaves to the engine) so a reader can compare drivers, without there
    // being a right answer to grade. Expressing that as PASS put a fixed
    // floor under every driver's score — about 13% of the suite — and made
    // the headline number mean less the more of them there were.
    //
    // INFORMATIONAL results appear in the report exactly like any other, and
    // are excluded from the pass rate's denominator rather than being counted
    // as passes. A probe that *can* fail must never use this: the point of
    // the status is that there was nothing to grade, not that grading was
    // inconvenient.
    INFORMATIONAL
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

    // Per-row value column, PK-ordered. `nullopt` means the column was SQL
    // NULL — A19. It used to be a `vector<std::string>`, in which a NULL and
    // an empty string were the same value; the header advertised this helper
    // for NULL-versus-empty work, which it could not do.
    std::vector<std::optional<std::string>> actual_values;

    // Q1 — the key column, same order, same length as `actual_values`.
    //
    // This helper used to select the value column alone, which is why it could
    // not see #299: `test_column_wise_array_binding` binds `SQL_C_SLONG` with
    // `BufferLength = 0`, the exact shape that made every parameter set read
    // element 0, and then checked only the string column — the one column that
    // bug spares, because for `SQL_C_CHAR` the length *is* the element size.
    // The keys collapsed to a single value and the probe reported a pass.
    //
    // A probe that knows what keys it inserted should assert them. `keys_are()`
    // is the short form.
    std::vector<std::optional<std::string>> actual_keys;

    // The i-th key rendered for a message, NULL as `<NULL>` — see display().
    std::string display_key(size_t i) const {
        if (i >= actual_keys.size()) return "<missing>";
        return actual_keys[i] ? *actual_keys[i] : std::string("<NULL>");
    }

    // Do the keys read back, in order, equal `expected`? Compared as text
    // because that is how they arrive: a driver may render an INTEGER key as
    // "1" or "1.0" and both are the key the probe inserted, so callers pass the
    // spelling they expect this engine to produce.
    bool keys_are(const std::vector<std::string>& expected) const {
        if (actual_keys.size() != expected.size()) return false;
        for (size_t i = 0; i < expected.size(); ++i) {
            if (!actual_keys[i] || *actual_keys[i] != expected[i]) return false;
        }
        return true;
    }

    // Every key rendered, for a diagnostic that has to show what came back.
    std::string keys_joined() const {
        std::string out;
        for (size_t i = 0; i < actual_keys.size(); ++i) {
            if (i) out += ", ";
            out += display_key(i);
        }
        return out;
    }

    std::string diagnostic;                  // Empty when ok

    // The i-th value rendered for a message. A19: probes print these into
    // `actual`, and "" for a NULL reads as a driver that stored an empty
    // string. Renders NULL as the unquotable token <NULL> instead.
    std::string display(size_t i) const {
        if (i >= actual_values.size()) return "<missing>";
        return actual_values[i] ? *actual_values[i] : std::string("<NULL>");
    }
};

// A table found through SQLTables — C7.
//
// Five copies of the discovery loop existed, with this struct declared twice
// in one file. Two probes skipped discovery altogether and tried only
// `RDB$DATABASE`, `information_schema.TABLES` and `sys.tables`, none of
// which exist on PostgreSQL, DuckDB or ClickHouse — so they reported
// "callable (no primary keys)" without ever having seen a table that has
// one.
struct DiscoveredTable {
    std::string catalog;
    std::string schema;
    std::string name;

    // Qualified as a probe would print it, skipping the parts the driver
    // left empty.
    std::string qualified() const {
        std::string out;
        if (!catalog.empty()) out += catalog + ".";
        if (!schema.empty()) out += schema + ".";
        return out + name;
    }
};

// Outcome of TestBase::commit_now — A22.
//
// The probes that INSERT, commit and then check the rows landed used to
// throw the commit's return code away. When the commit failed,
// verify_rows_persisted found nothing and the probe blamed the *bind path*
// — "this is the Firebird #161 silent-corruption shape" — for what was a
// plain commit failure, at CRITICAL. Root-cause text belongs to the first
// failure, which is the same principle AGENTS.md states for rollback paths.
struct CommitOutcome {
    SQLRETURN rc = SQL_SUCCESS;
    bool ok = true;
    std::string sqlstate;   // First SQLSTATE on the connection, when !ok
    std::string summary;    // Ready to print: rc, SQLSTATE and driver message

    explicit operator bool() const { return ok; }
};

// Run DDL with autocommit ON, restoring the previous setting on scope exit
// — A14.
//
// Promoted out of test_base.cpp so that transaction_tests,
// array_param_tests and param_binding_tests stop hand-rolling the same
// save/restore. Each of their copies had a defect this one does not:
// treating a failed read as SQL_AUTOCOMMIT_OFF, forcing ON in a drop path
// and never restoring, or restoring to a hard-coded ON.
class ScopedAutocommitOn {
public:
    explicit ScopedAutocommitOn(SQLHDBC hdbc) : hdbc_(hdbc) {
        // A14. `saved_` used to be initialised to 0 and the SQLGetConnectAttr
        // return code ignored — and SQL_AUTOCOMMIT_OFF *is* 0. So a driver
        // that declined the read got autocommit switched OFF by this
        // destructor, and every later probe category ran inside an open
        // transaction: exactly the state Firebird's DDL handling cannot
        // survive. Default to ON and only restore what was actually read.
        saved_ = SQL_AUTOCOMMIT_ON;
        SQLUINTEGER value = 0;
        if (SQL_SUCCEEDED(SQLGetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                                            &value, 0, nullptr))) {
            saved_ = value;
            have_saved_ = true;
        }
        SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                          reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
    }
    ~ScopedAutocommitOn() {
        // If the read failed there is nothing to restore *to*; leaving
        // autocommit ON is the safe state and the one the connection almost
        // certainly started in.
        if (!have_saved_) return;
        SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                          reinterpret_cast<SQLPOINTER>(
                              static_cast<intptr_t>(saved_)), 0);
    }
    ScopedAutocommitOn(const ScopedAutocommitOn&) = delete;
    ScopedAutocommitOn& operator=(const ScopedAutocommitOn&) = delete;
private:
    SQLHDBC hdbc_;
    SQLUINTEGER saved_ = SQL_AUTOCOMMIT_ON;
    bool have_saved_ = false;
};

// D92: hold a transaction open for the duration of a persistence probe.
//
// The mirror of ScopedAutocommitOn above, and the reason it was needed:
// probes that INSERT, COMMIT and then verify persistence were running in
// autocommit ON, where SQLEndTran(SQL_COMMIT) is a no-op - every row was
// already committed by the INSERT that made it. `commit_now()` therefore
// reported success whatever the driver would have done with a real commit,
// and A22's attribution machinery had nothing to attribute. Under
// `FailOn=SQLEndTran` all three probes the e2e scenario watches passed.
//
// `engaged()` says whether the transaction was actually opened. A driver
// entitled to decline SQL_AUTOCOMMIT_OFF is not failing this probe, which
// tests the bind path; the probe carries on and reports that the commit could
// not be exercised, rather than grading a driver on something else.
class ScopedAutocommitOff {
public:
    explicit ScopedAutocommitOff(SQLHDBC hdbc) : hdbc_(hdbc) {
        // A14's lesson, applied the other way round: a failed read must not
        // be mistaken for a value. Default to ON - the state a connection is
        // in unless someone changed it - and only restore what was read.
        saved_ = SQL_AUTOCOMMIT_ON;
        SQLUINTEGER value = 0;
        if (SQL_SUCCEEDED(SQLGetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                                            &value, 0, nullptr))) {
            saved_ = value;
            have_saved_ = true;
        }
        if (saved_ == SQL_AUTOCOMMIT_OFF) {
            // Already in a transaction: nothing to open and nothing to undo.
            engaged_ = true;
            return;
        }
        engaged_ = SQL_SUCCEEDED(SQLSetConnectAttr(
            hdbc_, SQL_ATTR_AUTOCOMMIT,
            reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_OFF), 0)) != 0;
        changed_ = engaged_;
    }

    ~ScopedAutocommitOff() {
        if (!changed_) return;
        SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                          reinterpret_cast<SQLPOINTER>(
                              static_cast<intptr_t>(
                                  have_saved_ ? saved_ : SQL_AUTOCOMMIT_ON)), 0);
    }

    // True when a transaction is open, so a COMMIT means something.
    bool engaged() const { return engaged_; }

    ScopedAutocommitOff(const ScopedAutocommitOff&) = delete;
    ScopedAutocommitOff& operator=(const ScopedAutocommitOff&) = delete;

private:
    SQLHDBC hdbc_;
    SQLUINTEGER saved_ = SQL_AUTOCOMMIT_ON;
    bool have_saved_ = false;
    bool engaged_ = false;
    bool changed_ = false;
};

// Result of turning a driver-filled character buffer into a std::string
// without trusting the driver's reported length — A3.
// D60: the definition moved to core/guarded_buffer.hpp so the discovery layer
// can use it too — it had grown an unbounded `reinterpret_cast<char*>(buf)`
// for want of exactly this. The name stays visible here, and every existing
// caller and test is unaffected.
using core::BoundedString;

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
    // C13: the connection string comes with the connection now.
    //
    // A probe that wants to disconnect mid-transaction, or take a connection
    // out of a pool and put it back broken, cannot use `conn_` - the rest of
    // the run depends on it. It needs one of its own, and to open one it
    // needs the string. `get_environment()` was already reachable; this was
    // the missing half.
    //
    // Defaulted so the 23 existing categories and their tests construct
    // unchanged; `open_sibling_connection()` says clearly what it means when
    // the string was never supplied.
    explicit TestBase(core::OdbcConnection& conn,
                      std::string connection_string = {})
        : conn_(conn), connection_string_(std::move(connection_string)) {}

    virtual ~TestBase() = default;

    // Run all tests in this category
    virtual std::vector<TestResult> run() = 0;

    // Get test category name
    virtual std::string category_name() const = 0;

    // S3 — what survived a category that crashed.
    //
    // `run()` is written as `return { probe_a(), probe_b(), … }` in most
    // categories, so a driver fault in probe C destroys A's and B's results
    // along with it: the crash guard catches the fault, but the vector was
    // never built. On 3.0.1.21 the Descriptor Tests category faulted and *five*
    // probes vanished from the report — which is why the two runs of the H17
    // pair differ by four in their totals, and why both triage agents had to
    // caveat the baseline pass rate as "optimistic by an unknown margin".
    //
    // The margin need not be unknown. `run_test` records every probe as it
    // completes, so on a crash the caller can report what actually ran instead
    // of discarding it, and name the probe that did not return.
    const std::vector<TestResult>& completed_results() const { return completed_; }
    const std::string& last_probe_started() const { return last_started_; }

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

    // Try each query as Unicode, then each as ANSI; return the last result
    // code. D78.
    //
    // Six probes had this inline, in two shapes. The fallback exists for a
    // driver that exports both widths and whose W conversion is broken, which
    // is a real shape - but it cannot help against a Unicode-only driver,
    // because the driver manager converts the ANSI attempt back into the same
    // W entry point. D78 measured that: naming the W function and naming the
    // ANSI one produce identical reports.
    //
    // So this branch cannot be exercised end to end by any test here. One copy
    // of an untestable branch is better than six.
    static SQLRETURN prepare_w_then_ansi(core::OdbcStatement& stmt,
                                         const std::vector<std::string>& queries);

    // As above, for SQLExecDirectW / SQLExecDirect. Separate rather than a flag
    // because the two reset differently between attempts: a failed prepare
    // resets parameters, a failed execute closes the cursor.
    static SQLRETURN exec_direct_w_then_ansi(core::OdbcStatement& stmt,
                                             const std::vector<std::string>& queries);

    // Point an ARD field at SQL_C_NUMERIC with a precision and scale — C11.
    //
    // The ODBC spec requires this before SQLGetData with SQL_C_NUMERIC: the
    // descriptor carries the precision and scale, and without setting them
    // the driver has no way to know what shape the caller wants.
    //
    // It lived in numeric_struct_tests.cpp and was re-implemented inline in
    // datatype_edge_tests.cpp **ignoring all four return codes**. That is the
    // exact shape D29 found: SQL_C_NUMERIC ARD binding did not work at all in
    // the mock, and two probes passed anyway because they never looked at
    // whether the fields they set were accepted. Returns false when any step
    // is refused, so a caller can say "the driver would not take the
    // descriptor" instead of blaming SQLGetData for the answer that follows.
    static bool set_numeric_descriptor(SQLHSTMT hstmt, SQLSMALLINT col,
                                       SQLSMALLINT precision,
                                       SQLSMALLINT scale) {
        SQLHDESC ard = SQL_NULL_HDESC;
        SQLRETURN ret = SQLGetStmtAttr(hstmt, SQL_ATTR_APP_ROW_DESC, &ard, 0,
                                       nullptr);
        if (!SQL_SUCCEEDED(ret) || ard == SQL_NULL_HDESC) return false;

        ret = SQLSetDescField(ard, col, SQL_DESC_TYPE,
                              reinterpret_cast<SQLPOINTER>(SQL_C_NUMERIC), 0);
        if (!SQL_SUCCEEDED(ret)) return false;

        ret = SQLSetDescField(
            ard, col, SQL_DESC_PRECISION,
            reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(precision)), 0);
        if (!SQL_SUCCEEDED(ret)) return false;

        ret = SQLSetDescField(
            ard, col, SQL_DESC_SCALE,
            reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(scale)), 0);
        return SQL_SUCCEEDED(ret);
    }

    // Read one character column in full, however long it is — A19.
    //
    // A single `SQLGetData` into a fixed buffer returns SQL_SUCCESS_WITH_INFO
    // with SQLSTATE 01004 when the value does not fit, and `SQL_SUCCEEDED`
    // accepts that: a value longer than the buffer was silently cut short and
    // then compared against what the probe inserted, so a *correct* driver was
    // reported as having corrupted the data. Calling `SQLGetData` again on the
    // same column continues where the last call stopped, which is what this
    // does until the driver stops warning.
    //
    // Returns false only on a real error (rc not succeeded); `is_null` is set
    // for SQL_NULL_DATA, in which case `out` is left empty.
    static bool get_data_full(SQLHSTMT hstmt, SQLUSMALLINT col,
                              std::string& out, bool& is_null,
                              std::string& error);

    // Render a table or column name for interpolation into SQL — A19.
    //
    // **Quotes only names that cannot be written bare.** A19 asked for every
    // identifier to be quoted with `SQL_IDENTIFIER_QUOTE_CHAR`; that is wrong
    // here, and quoting all three names in `verify_rows_persisted` broke 13
    // probes against the mock before the reasoning was worked through:
    //
    //   * Every table this tool creates is created *unquoted*
    //     (`CREATE TABLE ODBC_TEST_PARAM (...)`), and an unquoted name is
    //     folded by the engine — up-cased by Firebird and Oracle,
    //     **down**-cased by PostgreSQL. Quoting only the SELECT side asks for
    //     `"ODBC_TEST_PARAM"` where PostgreSQL stored `odbc_test_param`, so
    //     the quoting introduces a failure on an engine that had none.
    //     Quoting is only safe when both ends agree, and the DDL side is
    //     spread across `RoundTripTableGuard` and three `create_test_table`
    //     implementations.
    //   * The real hazard A19 names — a name needing quotes interpolated
    //     without them — cannot arise for a bare-safe name, and a name that
    //     is *not* bare-safe cannot have been created unquoted in the first
    //     place. So the safe rule is: leave bare-safe names alone, quote the
    //     rest.
    //
    // A driver that reports the quote character as a single space means "no
    // quoting supported", so the name is returned unchanged rather than
    // wrapped in spaces. A name already containing the quote character is
    // returned unchanged too: there is no such name in this codebase, and
    // inventing a per-engine escaping rule would be worse than leaving it.
    std::string quote_identifier(const std::string& ident);

    // True when `ident` can be interpolated into SQL without quoting: an
    // ASCII letter or underscore followed by letters, digits or underscores.
    // Deliberately stricter than any engine's rule — it decides whether to
    // leave a name alone, so erring towards "needs quoting" is the safe way
    // to be wrong. A19.
    static bool is_bare_identifier(const std::string& ident);


protected:
    core::OdbcConnection& conn_;
    std::string connection_string_;   // C13

    // S3. Written by run_test as it goes, read by run_test_category only when
    // the crash guard fires. `completed_` is a copy of every verdict already
    // reached; `last_started_` is the probe that was running when the process
    // faulted, which is the one the report most needs to name.
    std::vector<TestResult> completed_;
    std::string last_started_;

    // C13: the string `conn_` was opened with, when the caller supplied it.
    const std::string& connection_string() const { return connection_string_; }

    // Open a second connection to the same target, for a probe that needs one
    // it is allowed to damage — C13.
    //
    // Returns nullptr when no connection string was supplied, which is the
    // case in unit tests that construct a category directly. A probe must
    // treat that as "cannot run here" rather than as a driver fault: SKIP,
    // not FAIL. Errors from `connect()` propagate as OdbcError, because a
    // sibling that will not open *is* a finding once we know the primary did.
    std::unique_ptr<core::OdbcConnection> open_sibling_connection() {
        if (connection_string_.empty()) return nullptr;
        auto sibling =
            std::make_unique<core::OdbcConnection>(conn_.get_environment());
        sibling->connect(connection_string_);
        return sibling;
    }

    // H15: can `sibling` see a table created on the primary connection?
    //
    // Every cross-connection probe assumes both handles address the same
    // catalog. That holds for a server — postgres, mysql, firebird — and does
    // not hold for a data source that is per-connection. DuckDB with no
    // `Database=` opens `:memory:` and then deliberately declines to cache
    // the instance, so each `SQLConnect` receives its own database object;
    // the sibling is connected to a different, empty database and never sees
    // the primary's table.
    //
    // Without this check those probes report `Table ... does not exist` as a
    // driver fault, which is wrong twice over: the driver is behaving exactly
    // as documented, and no driver could pass. The honest answer is
    // SKIP_UNSUPPORTED — the question cannot be asked of this data source.
    // Fixing the connection string is the other half; this half means the
    // probes stay correct against any per-connection driver, including one
    // whose config we do not control.
    bool sibling_shares_catalog(core::OdbcConnection& sibling,
                                const std::string& table_name) {
        try {
            core::OdbcStatement s(sibling);
            s.execute("SELECT 1 FROM " + table_name + " WHERE 1=0");
            return true;
        } catch (...) {
            // Best-effort: leave the sibling usable for the SKIP path. This
            // swallows deliberately and rethrows nothing, so there is no
            // primary exception for the rollback to hide.
            SQLEndTran(SQL_HANDLE_DBC, sibling.get_handle(), SQL_ROLLBACK);
            return false;
        }
    }

    // The SKIP text every caller of sibling_shares_catalog() shares, so the
    // three probes cannot drift into describing the same condition three ways.
    static std::string per_connection_catalog_skip(const std::string& table_name) {
        return "The second connection cannot see " + table_name +
               ", which was created on the first — this data source gives "
               "each connection its own catalog, so there is no shared state "
               "for a cross-connection probe to observe. Point the driver at "
               "a persistent database (for DuckDB, `Database=<file>`) to make "
               "this testable.";
    }

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

    // C6: `time_test` stood here with no callers. run_test() below times
    // every probe already, which is why nothing ever needed it.

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
        note_probe_start(category_name(), test_name);   // S2
        last_started_ = test_name;                      // S3
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
        // S3: keep a copy, so a later probe faulting the process does not take
        // this one's verdict with it. Costs one vector append per probe.
        completed_.push_back(result);
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

    // Ask SQLTables for up to `limit` real tables — C7.
    //
    // Returns them in the order the driver reported. Empty when SQLTables
    // fails or the catalog has none, which a caller must treat as "cannot
    // test against a real table" rather than as a pass: an empty result here
    // is why the primary-key and statistics probes used to pass without ever
    // seeing a table.
    //
    // `types` is the SQLTables table-type filter; the default asks for base
    // tables only, since a view has no statistics or primary key to find.
    std::vector<DiscoveredTable> discover_tables(
        size_t limit = 5, const std::string& types = "TABLE");

    // Read one character column of the current row into a string — C7.
    //
    // "SQLGetData into a std::string" was written four ways across the suite
    // and none of them looped on 01004, so each silently truncated at its own
    // buffer size. This is get_data_full (A19) with the parts a caller does
    // not need folded away: empty string for NULL, empty string on error.
    // Use get_data_full directly where NULL and error have to be told apart.
    static std::string get_string(SQLHSTMT hstmt, SQLUSMALLINT col);

    // SQLEndTran(SQL_COMMIT) on this connection, keeping the return code and
    // the diagnostics that came with it — A22.
    //
    // Pair it with verify_rows_persisted: when the commit failed, the rows
    // are missing *because of that*, and saying so is the difference between
    // a report that names the real fault and one that sends a driver author
    // to look at the parameter-binding code.
    CommitOutcome commit_now();
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

    // `val_column` names the value column — C4. It is "VAL" everywhere
    // except ArrayParamTests, whose table has always called it NAME; making
    // it a parameter is what let that third reimplementation go.
    RoundTripTableGuard(
        core::OdbcConnection& conn,
        std::string table_name,
        std::string val_ddl,
        const std::vector<std::string>& id_ddl_variants = default_id_ddl_variants(),
        std::string val_column = "VAL");

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
        const std::vector<std::string>& id_ddl_variants = default_id_ddl_variants(),
        std::string val_column = "VAL");

    bool ok() const { return ok_; }
    const std::string& name() const { return table_name_; }
    const std::string& last_error() const { return last_error_; }

    // True when the table already existed and was reused rather than created
    // — C4. The three helpers this guard replaces all probed for the table
    // first, so that a run against a database where the user has no CREATE
    // TABLE privilege can still work against one an earlier run left behind.
    // A reused table is emptied before it is handed over (**A15**).
    bool was_reused() const { return reused_; }
    // Which val-column DDL was accepted — useful when create_first_working()
    // picked a fallback and the report should say which (C10).
    const std::string& val_ddl() const { return val_ddl_; }
    const std::string& val_column() const { return val_column_; }

private:
    core::OdbcConnection& conn_;
    std::string table_name_;
    std::string val_ddl_;
    std::string val_column_;
    bool ok_ = false;
    bool reused_ = false;
    std::string last_error_;
};

// A26: end a sibling connection's transaction before a RoundTripTableGuard in
// the same scope drops the table that sibling touched.
//
// Destructors run in reverse declaration order, so a guard declared *after*
// the sibling is destroyed *first* — its `DROP TABLE` runs while the sibling
// still holds an open transaction on that table. Firebird's default lock wait
// is infinite (`MON$TRANSACTIONS.MON$LOCK_TIMEOUT = -1`), so that DROP never
// returns, and because crusher has no per-probe watchdog the wedge is not
// contained: the CI job is killed at its wall-clock cap and the report, which
// is block-buffered on a pipe, is lost entirely. A run that wedged and a run
// that produced nothing are then indistinguishable.
//
// That is not hypothetical. It cost two whole stress-test runs of the H17
// Firebird pair, on both driver builds, and it was only found by turning on
// the Windows driver-manager trace and reading the last call that never
// returned.
//
// Declare one immediately after the guard: being later in the scope it is
// destroyed earlier, which is the entire point. The rollback comes before the
// disconnect because `SQLDisconnect` is allowed to refuse with `25000` while a
// transaction is open — `test_disconnect_with_open_transaction` is the probe
// for exactly that — and a refused disconnect would leave the lock in place.
class SiblingRelease {
public:
    explicit SiblingRelease(std::unique_ptr<core::OdbcConnection>& sibling)
        : sibling_(sibling) {}

    SiblingRelease(const SiblingRelease&) = delete;
    SiblingRelease& operator=(const SiblingRelease&) = delete;

    ~SiblingRelease() {
        if (!sibling_) return;
        // Nothing here may throw: this runs during unwinding on the very
        // path that matters — a probe that threw before its own cleanup.
        SQLEndTran(SQL_HANDLE_DBC, sibling_->get_handle(), SQL_ROLLBACK);
        try {
            sibling_->disconnect();   // throws through check_odbc_result
        } catch (...) {
            // A sibling we cannot close is not worth failing a probe over;
            // the rollback above is what releases the table.
        }
    }

private:
    std::unique_ptr<core::OdbcConnection>& sibling_;
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
        case TestStatus::INFORMATIONAL: return "INFORMATIONAL";
        default: return "UNKNOWN";
    }
}

// C11: the string four probes across two files each spelled out. What it is
// matters — a statement no engine could parse — and one definition is how
// that stays true if it ever has to change.
inline constexpr const char* kInvalidSql = "THIS IS NOT VALID SQL !!! @#$%";

// C6: `is_skipped()` stood here with no callers. The two places that ask
// the question - main.cpp's tally and console_reporter's - are switches
// over TestStatus with the two SKIP_ labels falling together, where a
// predicate does not fit and -Wswitch is doing useful work.

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
