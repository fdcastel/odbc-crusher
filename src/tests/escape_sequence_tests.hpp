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
 *   {escape '...'} — LIKE escape character
 *   {INTERVAL ...} — Interval literals
 *
 * All tests are RDBMS-independent — they use ODBC escape syntax
 * and verify the driver processes it. The driver does the native
 * SQL translation.
 */
class EscapeSequenceTests : public TestBase {
public:
    explicit EscapeSequenceTests(core::OdbcConnection& conn)
        : TestBase(conn) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Escape Sequence Tests"; }

private:
    // Discovery (2 tests)
    TestResult test_scalar_function_capabilities();
    TestResult test_convert_function_capabilities();

    // SQLNativeSql translation (3 tests)
    TestResult test_native_sql_scalar_functions();
    TestResult test_native_sql_datetime_literals();
    TestResult test_native_sql_call_escape();

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
    TestResult test_call_escape_translation();
    TestResult test_call_escape_format_variants();

    // Helpers
    std::optional<SQLUINTEGER> get_info_uint(SQLUSMALLINT info_type);
    std::optional<std::string> call_native_sql(const std::string& sql);
    std::optional<std::string> exec_scalar(const std::string& sql);
};

} // namespace odbc_crusher::tests
