#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

/**
 * @brief ODBC Escape Sequence Tests (Phase 26)
 *
 * Tests all 6 ODBC escape sequence categories:
 *   {fn ...}      — Scalar functions
 *   {d '...'}     — Date literals
 *   {t '...'}     — Time literals
 *   {ts '...'}    — Timestamp literals
 *   {oj ...}      — Outer joins
 *   {CALL ...}    — Procedure calls
 *   {escape '...'} — LIKE escape character   (advertised only, see below)
 *   {INTERVAL ...} — Interval literals        (advertised only, see below)
 *
 * B8: the two lines above used to read like the rest of the list. No probe
 * in this file sends either escape - the three that are named for them read
 * a SQLGetInfo bitmask and stop, which is why B1 made them INFORMATIONAL and
 * D44 carries the redesign. Saying so here matters because this comment is
 * what a reader checks before assuming a category is covered.
 *
 * The rest are RDBMS-independent — they use ODBC escape syntax and verify
 * the driver processes it. The driver does the native SQL translation.
 */
class EscapeSequenceTests : public TestBase {
public:
    // What a scalar probe got back — A4.
    //
    // These helpers used to return std::optional<std::string> and swallow
    // everything else: `catch (...) { return std::nullopt; }`. A failed scalar
    // function was reported as UCASE='NULL', with no SQLSTATE and no failing
    // SQL, which is why the DuckDB triage had to be resolved by reading driver
    // source to find SetNotImplemented/HYC00. The caller can now tell
    // "unsupported" from "wrong answer" from "never ran".
    struct ScalarResult {
        std::optional<std::string> value;   // set only when the call succeeded
        std::string sqlstate;               // empty when the driver posted none
        std::string message;                // the driver's text
        std::string query;                  // the SQL that was actually sent
        bool truncated = false;             // value longer than the buffer

        explicit operator bool() const { return value.has_value(); }
        const std::string& operator*() const { return *value; }
    };

    explicit EscapeSequenceTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Escape Sequence Tests"; }

private:
    // Discovery (2 tests)
    TestResult test_scalar_function_capabilities();
    TestResult test_convert_function_capabilities();

    // SQLNativeSql translation (4 tests)
    TestResult test_native_sql_scalar_functions();
    TestResult test_native_sql_datetime_literals();
    TestResult test_native_sql_outer_join_escape();

    // Scalar function execution (6 tests)
    TestResult test_string_scalar_functions();
    TestResult test_numeric_scalar_functions();
    TestResult test_datetime_scalar_functions();
    TestResult test_system_scalar_functions();
    TestResult test_datetime_literal_escapes();
    TestResult test_like_escape_sequence();

    // Outer join & interval (2 tests)
    TestResult test_outer_join_escape();
    TestResult test_interval_literal_escape();

    // Procedure call escape (2 tests)
    TestResult test_call_escape_format_variants();

    // PORT plan §4.3 — {CALL …} / {?=CALL …} IN/OUT/INOUT parameter
    // direction probes. Verify the bound buffer is mutated post-execute
    // for OUT and INOUT directions. Many drivers accept the escape
    // syntactically but only honour IN.
    TestResult test_call_escape_in_parameter();
    TestResult test_call_escape_out_parameter();
    TestResult test_function_call_escape_return_value();
    TestResult test_call_escape_inout_parameter();

    // PORT plan §4.12 — close the loop on `SQLGetInfo(SQL_*_FUNCTIONS)`
    // by *executing* one representative query per claimed function and
    // reporting the matrix. Catches drivers that announce a function in
    // the bitmask but return 42000 when the query runs.
    TestResult test_scalar_function_claim_vs_execute();

    // Helpers
    std::optional<SQLUINTEGER> get_info_uint(SQLUSMALLINT info_type);
    std::optional<std::string> call_native_sql(const std::string& sql);

    // Execute a scalar SELECT and return the first column, with the SQLSTATE
    // when it fails. A4.
    ScalarResult exec_scalar_ex(const std::string& sql);

    // Backwards-compatible wrapper for the call sites that only need the
    // value. New code should prefer exec_scalar_ex() so that a failure can be
    // classified rather than reported as 'NULL'.
    std::optional<std::string> exec_scalar(const std::string& sql);
};

} // namespace odbc_crusher::tests
