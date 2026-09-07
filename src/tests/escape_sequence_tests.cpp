#include "escape_sequence_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace odbc_crusher::tests {

std::vector<TestResult> EscapeSequenceTests::run() {
    return {
        // Discovery
        test_scalar_function_capabilities(),
        test_convert_function_capabilities(),

        // SQLNativeSql
        test_native_sql_scalar_functions(),
        test_native_sql_datetime_literals(),
        test_native_sql_call_escape(),
        test_native_sql_outer_join_escape(),

        // Scalar function execution
        test_string_scalar_functions(),
        test_numeric_scalar_functions(),
        test_datetime_scalar_functions(),
        test_system_scalar_functions(),
        test_datetime_literal_escapes(),
        test_like_escape_sequence(),

        // Outer join & interval
        test_outer_join_escape(),
        test_interval_literal_escape(),

        // Procedure call escape
        test_call_escape_translation(),
        test_call_escape_format_variants(),
        test_call_escape_in_parameter(),
        test_call_escape_out_parameter(),
        test_function_call_escape_return_value(),
        test_call_escape_inout_parameter(),

        // PORT plan §4.12 — claim-vs-execute matrix
        test_scalar_function_claim_vs_execute()
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::optional<SQLUINTEGER> EscapeSequenceTests::get_info_uint(SQLUSMALLINT info_type) {
    SQLUINTEGER value = 0;
    SQLRETURN ret = SQLGetInfo(conn_.get_handle(), info_type, &value, sizeof(value), nullptr);
    if (SQL_SUCCEEDED(ret)) return value;
    return std::nullopt;
}

std::optional<std::string> EscapeSequenceTests::call_native_sql(const std::string& sql) {
    SQLCHAR out[4096] = {0};
    SQLINTEGER out_len = 0;
    SQLRETURN ret = SQLNativeSql(
        conn_.get_handle(),
        const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())),
        static_cast<SQLINTEGER>(sql.length()),
        out, sizeof(out), &out_len);
    if (SQL_SUCCEEDED(ret)) {
        // A3: out_len is the *total available* length, not the amount written,
        // and SQL_SUCCEEDED accepts the 01004 that accompanies truncation — so
        // std::string(out, out_len) read past the end of this stack buffer.
        // SQL_NO_TOTAL (-4) was worse: as a size_t it is SIZE_MAX - 3.
        return bounded_string(reinterpret_cast<const char*>(out), sizeof(out),
                              out_len).value;
    }
    return std::nullopt;
}

EscapeSequenceTests::ScalarResult EscapeSequenceTests::exec_scalar_ex(
    const std::string& sql) {
    ScalarResult out;
    out.query = sql;
    try {
        core::OdbcStatement stmt(conn_);
        // A2: every caller passes a bare `SELECT <expr>`, which Firebird
        // rejects — it requires a FROM clause. All 46 call sites in this file
        // are fixed here rather than in 46 string literals.
        auto attempt = execute_first_working(stmt, literal_select_variants(sql));
        if (!attempt) {
            // A4: the query never ran. Report which SQLSTATE each variant gave
            // rather than letting the caller print 'NULL'.
            if (!attempt.failures.empty()) {
                out.sqlstate = attempt.failures.front().sqlstate;
                out.message = attempt.format_failures();
            }
            return out;
        }
        out.query = attempt.query;

        SQLRETURN ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) {
            out.sqlstate = first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
            out.message = "SQLFetch returned " + std::to_string(ret);
            return out;
        }

        SQLCHAR buf[1024] = {0};
        SQLLEN ind = 0;
        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        if (!SQL_SUCCEEDED(ret)) {
            out.sqlstate = first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
            out.message = "SQLGetData returned " + std::to_string(ret);
            return out;
        }
        if (ind == SQL_NULL_DATA) {
            out.message = "value is NULL";
            return out;
        }

        // A3: `ind` is the total available length, not the amount written, so
        // an over-long value would index off the end of this buffer.
        const auto bounded =
            bounded_string(reinterpret_cast<const char*>(buf), sizeof(buf), ind);
        out.truncated = bounded.truncated;
        if (bounded.truncated) {
            // A4: truncation is now a reportable outcome rather than silence.
            out.message = "value truncated at " + std::to_string(sizeof(buf) - 1) +
                          " bytes; driver reported " + std::to_string(ind);
            return out;
        }
        out.value = bounded.value;
        return out;
    } catch (const core::OdbcError& e) {
        out.sqlstate = e.diagnostics().empty() ? std::string()
                                               : e.diagnostics()[0].sqlstate;
        out.message = e.what();
        return out;
    } catch (const std::exception& e) {
        out.message = e.what();
        return out;
    }
}

std::optional<std::string> EscapeSequenceTests::exec_scalar(const std::string& sql) {
    return exec_scalar_ex(sql).value;
}

namespace {

// A4: describe why a scalar function did not produce the expected value, using
// the SQLSTATE when there is one. "UCASE='NULL'" told a reader nothing.
std::string describe_scalar_failure(const std::string& name,
                                    const EscapeSequenceTests::ScalarResult& res,
                                    const std::string& expected) {
    std::string out = name;
    if (res) {
        out += "='" + *res.value + "' (expected '" + expected + "')";
    } else if (!res.sqlstate.empty()) {
        out += " failed with " + res.sqlstate;
        if (!res.message.empty()) out += " (" + res.message + ")";
    } else if (!res.message.empty()) {
        out += " produced no value: " + res.message;
    } else {
        out += " produced no value";
    }
    return out + "; ";
}

// True when the driver said the function is not implemented, as opposed to
// answering wrongly. Same states as TestBase::classify_failure.
bool scalar_unsupported(const EscapeSequenceTests::ScalarResult& res) {
    return res.sqlstate == "IM001" || res.sqlstate == "HYC00" ||
           res.sqlstate == "HY092" || res.sqlstate == "HY106";
}

}  // namespace

// ---------------------------------------------------------------------------
// Discovery Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_scalar_function_capabilities() {
    return run_test(
        "test_scalar_function_capabilities",
        "SQLGetInfo(SQL_STRING/NUMERIC/TIMEDATE/SYSTEM_FUNCTIONS)",
        "Driver reports scalar function capabilities via bitmask",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Scalar Functions",
        [&](TestResult& r) {
            std::ostringstream oss;
            int categories_found = 0;

            auto str_funcs = get_info_uint(SQL_STRING_FUNCTIONS);
            if (str_funcs) {
                ++categories_found;
                int count = 0;
                SQLUINTEGER v = *str_funcs;
                if (v & SQL_FN_STR_CONCAT) ++count;
                if (v & SQL_FN_STR_LENGTH) ++count;
                if (v & SQL_FN_STR_LTRIM) ++count;
                if (v & SQL_FN_STR_RTRIM) ++count;
                if (v & SQL_FN_STR_SUBSTRING) ++count;
                if (v & SQL_FN_STR_UCASE) ++count;
                if (v & SQL_FN_STR_LCASE) ++count;
                oss << "String: " << count << " funcs";
            }

            auto num_funcs = get_info_uint(SQL_NUMERIC_FUNCTIONS);
            if (num_funcs) {
                ++categories_found;
                int count = 0;
                SQLUINTEGER v = *num_funcs;
                if (v & SQL_FN_NUM_ABS) ++count;
                if (v & SQL_FN_NUM_CEILING) ++count;
                if (v & SQL_FN_NUM_FLOOR) ++count;
                if (v & SQL_FN_NUM_ROUND) ++count;
                if (v & SQL_FN_NUM_SQRT) ++count;
                if (v & SQL_FN_NUM_MOD) ++count;
                oss << ", Numeric: " << count << " funcs";
            }

            auto td_funcs = get_info_uint(SQL_TIMEDATE_FUNCTIONS);
            if (td_funcs) {
                ++categories_found;
                int count = 0;
                SQLUINTEGER v = *td_funcs;
                if (v & SQL_FN_TD_NOW) ++count;
                if (v & SQL_FN_TD_CURDATE) ++count;
                if (v & SQL_FN_TD_CURTIME) ++count;
                if (v & SQL_FN_TD_YEAR) ++count;
                if (v & SQL_FN_TD_MONTH) ++count;
                if (v & SQL_FN_TD_DAYOFWEEK) ++count;
                oss << ", Timedate: " << count << " funcs";
            }

            auto sys_funcs = get_info_uint(SQL_SYSTEM_FUNCTIONS);
            if (sys_funcs) {
                ++categories_found;
                int count = 0;
                SQLUINTEGER v = *sys_funcs;
                if (v & SQL_FN_SYS_DBNAME) ++count;
                if (v & SQL_FN_SYS_USERNAME) ++count;
                if (v & SQL_FN_SYS_IFNULL) ++count;
                oss << ", System: " << count << " funcs";
            }

            if (categories_found == 0) {
                r.status = TestStatus::FAIL;
                r.actual = "No scalar function capability info returned";
                r.severity = Severity::WARNING;
            } else {
                r.actual = oss.str();
            }
        });
}

TestResult EscapeSequenceTests::test_convert_function_capabilities() {
    return run_test(
        "test_convert_function_capabilities", "SQLGetInfo(SQL_CONVERT_*)",
        "Driver reports data type conversion capabilities",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, SQLGetInfo SQL_CONVERT_*",
        [&](TestResult& r) {
            // Query SQL_CONVERT_FUNCTIONS first
            auto conv_funcs = get_info_uint(SQL_CONVERT_FUNCTIONS);

            // Check a representative set of SQL_CONVERT_xxx types
            struct ConvertType {
                SQLUSMALLINT info_type;
                const char* name;
            };
            static const ConvertType convert_types[] = {
                {SQL_CONVERT_CHAR, "CHAR"},
                {SQL_CONVERT_VARCHAR, "VARCHAR"},
                {SQL_CONVERT_INTEGER, "INTEGER"},
                {SQL_CONVERT_DOUBLE, "DOUBLE"},
                {SQL_CONVERT_DATE, "DATE"},
                {SQL_CONVERT_TIME, "TIME"},
                {SQL_CONVERT_TIMESTAMP, "TIMESTAMP"},
                {SQL_CONVERT_DECIMAL, "DECIMAL"},
                {SQL_CONVERT_NUMERIC, "NUMERIC"},
                {SQL_CONVERT_BIT, "BIT"},
            };

            int types_with_conversions = 0;
            for (const auto& ct : convert_types) {
                auto mask = get_info_uint(ct.info_type);
                if (mask && *mask != 0) ++types_with_conversions;
            }

            std::ostringstream oss;
            oss << types_with_conversions << " of " << (sizeof(convert_types) / sizeof(convert_types[0]))
                << " types have conversion support";
            if (conv_funcs) {
                oss << "; CONVERT_FUNCTIONS=0x" << std::hex << *conv_funcs
                    << std::dec;
            }

            // B1/B2: a census of what the driver advertises. Converting
            // everything and converting nothing are both legal, so there is
            // no right answer to grade. The probes that *use* these
            // conversions - the {fn CONVERT} cases in this file, and
            // test_scalar_function_claim_vs_execute - are where a wrong
            // bitmask shows up as a failure.
            r.status = TestStatus::INFORMATIONAL;
            r.actual = oss.str();
        });
}

// ---------------------------------------------------------------------------
// SQLNativeSql Translation Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_native_sql_scalar_functions() {
    return run_test(
        "test_native_sql_scalar_functions", "SQLNativeSql",
        "SQLNativeSql translates {fn UCASE('hello')} to native SQL",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, SQLNativeSql",
        [&](TestResult& r) {
            const std::string input = "SELECT {fn UCASE('hello')}";
            auto translated = call_native_sql(input);
            if (!translated) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned error";
                r.severity = Severity::ERR;
                return;
            }
            if (translated->empty()) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned empty string";
                r.severity = Severity::ERR;
                return;
            }
            r.actual = "Translated to: " + *translated;
            // PORT plan port 4 — also catch drivers that return the input
            // verbatim (pass-through). Both checks must hold.
            if (translated->find("{fn") != std::string::npos) {
                r.status = TestStatus::FAIL;
                r.actual = "Escape sequence not translated (still contains {fn): "
                         + *translated;
                r.severity = Severity::WARNING;
                r.suggestion = "The driver should translate {fn UCASE(...)} to "
                               "the native equivalent (e.g. UPPER(...))";
                return;
            }
            if (*translated == input) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned the input verbatim — driver "
                           "is pass-through. Got: " + *translated;
                r.severity = Severity::WARNING;
                r.suggestion = "SQLNativeSql must perform escape translation "
                               "even if the native form happens to use the same "
                               "function names; non-trivial escapes (e.g. {d "
                               "'2026-01-01'}) require actual transformation.";
            }
        });
}

TestResult EscapeSequenceTests::test_native_sql_datetime_literals() {
    return run_test(
        "test_native_sql_datetime_literals", "SQLNativeSql",
        "SQLNativeSql translates {d '...'}, {t '...'}, {ts '...'} to native SQL",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Date/Time/Timestamp Escape Sequences",
        [&](TestResult& r) {
            struct EscapeTest {
                const char* input;
                const char* description;
            };
            static const EscapeTest tests[] = {
                {"SELECT {d '2026-01-15'}", "date literal"},
                {"SELECT {t '14:30:00'}", "time literal"},
                {"SELECT {ts '2026-01-15 14:30:00'}", "timestamp literal"},
            };

            int passed = 0;
            std::ostringstream oss;
            for (const auto& t : tests) {
                auto translated = call_native_sql(t.input);
                if (translated && !translated->empty() && translated->find('{') == std::string::npos) {
                    ++passed;
                } else {
                    oss << t.description << " not translated; ";
                }
            }

            if (passed == 3) {
                r.actual = "All 3 datetime literal escapes translated successfully";
            } else {
                r.status = TestStatus::FAIL;
                r.actual = std::to_string(passed) + "/3 translated. " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

TestResult EscapeSequenceTests::test_native_sql_call_escape() {
    return run_test(
        "test_native_sql_call_escape", "SQLNativeSql",
        "SQLNativeSql translates {CALL proc(?)} and {?=CALL func(?)} escape sequences",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence",
        [&](TestResult& r) {
            auto t1 = call_native_sql("{CALL my_proc(?)}");
            auto t2 = call_native_sql("{?=CALL my_func(?)}");

            int passed = 0;
            std::ostringstream oss;

            if (t1 && !t1->empty() && t1->find('{') == std::string::npos) {
                ++passed;
                oss << "CALL->'" << *t1 << "'";
            } else {
                oss << "CALL escape not translated; ";
            }

            if (t2 && !t2->empty() && t2->find('{') == std::string::npos) {
                ++passed;
                oss << ", ?=CALL->'" << *t2 << "'";
            } else {
                oss << "?=CALL escape not translated; ";
            }

            if (passed == 2) {
                r.actual = oss.str();
            } else {
                r.status = TestStatus::FAIL;
                r.actual = oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

// PORT plan port 4.D — {oj …} outer-join escape translation. The literal
// `{oj` must be removed from the SQLNativeSql output; pass-through drivers
// fail this check. Distinct from test_outer_join_escape() further down,
// which verifies {oj …} *executes* successfully via SQLExecDirect.
TestResult EscapeSequenceTests::test_native_sql_outer_join_escape() {
    return run_test(
        "test_native_sql_outer_join_escape", "SQLNativeSql",
        "SQLNativeSql strips {oj …} wrapper from a LEFT OUTER JOIN clause",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Outer Join Escape Sequence",
        [&](TestResult& r) {
            const std::string input =
                "SELECT * FROM {oj T1 LEFT OUTER JOIN T2 ON T1.ID = T2.FID}";
            auto translated = call_native_sql(input);
            if (!translated || translated->empty()) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned " +
                           std::string(translated ? "empty" : "error");
                r.severity = Severity::ERR;
                return;
            }
            r.actual = "Translated to: " + *translated;
            const bool has_oj_marker = translated->find("{oj") != std::string::npos
                                    || translated->find("{OJ") != std::string::npos;
            if (has_oj_marker) {
                r.status = TestStatus::FAIL;
                r.actual = "Escape not translated (still contains {oj/{OJ): "
                         + *translated;
                r.severity = Severity::WARNING;
                r.suggestion = "Strip the `{oj … }` wrapper and emit the "
                               "interior LEFT/RIGHT/FULL OUTER JOIN clause "
                               "verbatim into the native SQL.";
                return;
            }
            if (*translated == input) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned the input verbatim";
                r.severity = Severity::WARNING;
                r.suggestion = "Driver did not transform the input — likely "
                               "pass-through implementation of SQLNativeSql.";
            }
        });
}

// ---------------------------------------------------------------------------
// Scalar Function Execution Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_string_scalar_functions() {
    return run_test(
        "test_string_scalar_functions", "SQLExecDirect + SQLGetData",
        "String scalar functions via {fn ...} escape produce correct results",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: String Functions",
        [&](TestResult& r) {
            auto str_funcs = get_info_uint(SQL_STRING_FUNCTIONS);
            if (!str_funcs) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not query SQL_STRING_FUNCTIONS";
                return;
            }

            int tested = 0, passed_count = 0;
            int unsupported = 0;   // claimed in the bitmask, HYC00 when run (A4)
            std::ostringstream oss;
            SQLUINTEGER mask = *str_funcs;

            struct FuncTest {
                SQLUINTEGER flag;
                const char* sql;
                const char* expected;
                const char* name;
            };
            static const FuncTest tests[] = {
                {SQL_FN_STR_UCASE, "SELECT {fn UCASE('hello')}", "HELLO", "UCASE"},
                {SQL_FN_STR_LCASE, "SELECT {fn LCASE('HELLO')}", "hello", "LCASE"},
                {SQL_FN_STR_LENGTH, "SELECT {fn LENGTH('test')}", "4", "LENGTH"},
                {SQL_FN_STR_LTRIM, "SELECT {fn LTRIM('  hi')}", "hi", "LTRIM"},
                {SQL_FN_STR_RTRIM, "SELECT {fn RTRIM('hi  ')}", "hi", "RTRIM"},
                {SQL_FN_STR_CONCAT, "SELECT {fn CONCAT('a','b')}", "ab", "CONCAT"},
            };

            for (const auto& t : tests) {
                if (!(mask & t.flag)) continue;
                ++tested;
                auto res = exec_scalar_ex(t.sql);
                if (res && *res == t.expected) {
                    ++passed_count;
                } else if (scalar_unsupported(res)) {
                    // A4: the bitmask claimed this function and the driver
                    // then reported "not implemented" when asked to run it.
                    // That is a different — and more interesting — finding
                    // than never claiming it, so it is counted, not deducted.
                    ++unsupported;
                    oss << t.name << " claimed but not implemented ("
                        << res.sqlstate << "); ";
                } else {
                    oss << describe_scalar_failure(t.name, res, t.expected);
                }
            }

            r.actual = std::to_string(passed_count) + "/" +
                       std::to_string(tested) + " string functions passed";
            if (unsupported) {
                r.actual += " (" + std::to_string(unsupported) +
                            " claimed but not implemented)";
            }
            if (tested == 0) {
                // The bitmask claimed nothing, so there was nothing to run.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no string function support";
            } else if (unsupported == tested) {
                // A4: it claimed them all and implemented none. A driver
                // being honest about being incomplete is a skip, not a
                // failure — but the report now names the SQLSTATE it used
                // to say so, instead of printing 'NULL'.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims string functions but implements none: " +
                           oss.str();
            } else if (passed_count < tested - unsupported) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

TestResult EscapeSequenceTests::test_numeric_scalar_functions() {
    return run_test(
        "test_numeric_scalar_functions", "SQLExecDirect + SQLGetData",
        "Numeric scalar functions via {fn ...} escape produce correct results",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Numeric Functions",
        [&](TestResult& r) {
            auto num_funcs = get_info_uint(SQL_NUMERIC_FUNCTIONS);
            if (!num_funcs) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not query SQL_NUMERIC_FUNCTIONS";
                return;
            }

            int tested = 0, passed_count = 0;
            int unsupported = 0;   // claimed in the bitmask, HYC00 when run (A4)
            std::ostringstream oss;
            SQLUINTEGER mask = *num_funcs;

            struct FuncTest {
                SQLUINTEGER flag;
                const char* sql;
                std::vector<std::string> expected;  // Multiple acceptable results
                const char* name;
            };

            // We accept multiple representations (e.g. "5" or "5.000000")
            auto match = [](const std::string& val, const std::vector<std::string>& expected) {
                for (const auto& e : expected) {
                    if (val == e) return true;
                    // Try numeric comparison
                    try {
                        double v1 = std::stod(val);
                        double v2 = std::stod(e);
                        if (std::abs(v1 - v2) < 0.001) return true;
                    } catch (...) {}
                }
                return false;
            };

            struct SimpleTest {
                SQLUINTEGER flag;
                const char* sql;
                const char* name;
                double expected_num;
            };
            static const SimpleTest tests[] = {
                {SQL_FN_NUM_ABS, "SELECT {fn ABS(-5)}", "ABS", 5.0},
                {SQL_FN_NUM_FLOOR, "SELECT {fn FLOOR(3.7)}", "FLOOR", 3.0},
                {SQL_FN_NUM_CEILING, "SELECT {fn CEILING(3.2)}", "CEILING", 4.0},
                {SQL_FN_NUM_SQRT, "SELECT {fn SQRT(9)}", "SQRT", 3.0},
                {SQL_FN_NUM_ROUND, "SELECT {fn ROUND(3.14159,2)}", "ROUND", 3.14},
            };

            for (const auto& t : tests) {
                if (!(mask & t.flag)) continue;
                ++tested;
                auto res = exec_scalar_ex(t.sql);
                if (scalar_unsupported(res)) {
                    // A4: see the string-function probe above.
                    ++unsupported;
                    oss << t.name << " claimed but not implemented ("
                        << res.sqlstate << "); ";
                    continue;
                }
                auto val = res.value;
                if (val) {
                    try {
                        double v = std::stod(*val);
                        if (std::abs(v - t.expected_num) < 0.01) {
                            ++passed_count;
                        } else {
                            oss << t.name << "=" << *val << " (expected " << t.expected_num << "); ";
                        }
                    } catch (...) {
                        oss << t.name << "='" << *val << "' (not numeric); ";
                    }
                } else {
                    // A4: was `oss << t.name << "=NULL; "`, the exact
                    // symptom this row is about.
                    oss << describe_scalar_failure(t.name, res, "a value");
                }
            }

            r.actual = std::to_string(passed_count) + "/" +
                       std::to_string(tested) + " numeric functions passed";
            if (unsupported) {
                r.actual += " (" + std::to_string(unsupported) +
                            " claimed but not implemented)";
            }
            if (tested == 0) {
                // The bitmask claimed nothing, so there was nothing to run.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no numeric function support";
            } else if (unsupported == tested) {
                // A4: it claimed them all and implemented none. A driver
                // being honest about being incomplete is a skip, not a
                // failure — but the report now names the SQLSTATE it used
                // to say so, instead of printing 'NULL'.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims numeric functions but implements none: " +
                           oss.str();
            } else if (passed_count < tested - unsupported) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

TestResult EscapeSequenceTests::test_datetime_scalar_functions() {
    return run_test(
        "test_datetime_scalar_functions", "SQLExecDirect + SQLGetData",
        "Date/time scalar functions via {fn ...} produce non-empty results",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Date/Time Functions",
        [&](TestResult& r) {
            auto td_funcs = get_info_uint(SQL_TIMEDATE_FUNCTIONS);
            if (!td_funcs) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not query SQL_TIMEDATE_FUNCTIONS";
                return;
            }

            int tested = 0, passed_count = 0;
            int unsupported = 0;   // claimed in the bitmask, HYC00 when run (A4)
            std::ostringstream oss;
            SQLUINTEGER mask = *td_funcs;

            struct FuncTest {
                SQLUINTEGER flag;
                const char* sql;
                const char* name;
                bool check_nonempty;  // true = just check non-empty, false = check specific
            };
            static const FuncTest tests[] = {
                {SQL_FN_TD_CURDATE, "SELECT {fn CURDATE()}", "CURDATE", true},
                {SQL_FN_TD_CURTIME, "SELECT {fn CURTIME()}", "CURTIME", true},
                {SQL_FN_TD_NOW, "SELECT {fn NOW()}", "NOW", true},
                {SQL_FN_TD_YEAR, "SELECT {fn YEAR({d '2026-01-15'})}", "YEAR", false},
                {SQL_FN_TD_MONTH, "SELECT {fn MONTH({d '2026-06-15'})}", "MONTH", false},
                {SQL_FN_TD_DAYOFWEEK, "SELECT {fn DAYOFWEEK({d '2026-01-15'})}", "DAYOFWEEK", false},
            };

            for (const auto& t : tests) {
                if (!(mask & t.flag)) continue;
                ++tested;
                auto res = exec_scalar_ex(t.sql);
                if (scalar_unsupported(res)) {
                    // A4: see the string-function probe above.
                    ++unsupported;
                    oss << t.name << " claimed but not implemented ("
                        << res.sqlstate << "); ";
                    continue;
                }
                auto val = res.value;
                if (val && !val->empty()) {
                    if (t.check_nonempty) {
                        ++passed_count;
                    } else {
                        // For YEAR/MONTH/DAYOFWEEK, check we get a number
                        try {
                            int num = std::stoi(*val);
                            if (num > 0) ++passed_count;
                            else oss << t.name << "=" << *val << " (expected >0); ";
                        } catch (...) {
                            oss << t.name << "='" << *val << "' (not a number); ";
                        }
                    }
                } else {
                    // A4: was `oss << t.name << "=NULL; "`, the exact
                    // symptom this row is about.
                    oss << describe_scalar_failure(t.name, res, "a value");
                }
            }

            r.actual = std::to_string(passed_count) + "/" +
                       std::to_string(tested) + " datetime functions passed";
            if (unsupported) {
                r.actual += " (" + std::to_string(unsupported) +
                            " claimed but not implemented)";
            }
            if (tested == 0) {
                // The bitmask claimed nothing, so there was nothing to run.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no timedate function support";
            } else if (unsupported == tested) {
                // A4: it claimed them all and implemented none. A driver
                // being honest about being incomplete is a skip, not a
                // failure — but the report now names the SQLSTATE it used
                // to say so, instead of printing 'NULL'.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims timedate functions but implements none: " +
                           oss.str();
            } else if (passed_count < tested - unsupported) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

TestResult EscapeSequenceTests::test_system_scalar_functions() {
    return run_test(
        "test_system_scalar_functions", "SQLExecDirect + SQLGetData",
        "System scalar functions {fn DATABASE()}, {fn USER()} return non-empty results",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: System Functions",
        [&](TestResult& r) {
            auto sys_funcs = get_info_uint(SQL_SYSTEM_FUNCTIONS);
            if (!sys_funcs) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not query SQL_SYSTEM_FUNCTIONS";
                return;
            }

            SQLUINTEGER mask = *sys_funcs;
            int tested = 0, passed_count = 0;
            std::ostringstream oss;

            if (mask & SQL_FN_SYS_DBNAME) {
                ++tested;
                auto val = exec_scalar("SELECT {fn DATABASE()}");
                if (val && !val->empty()) {
                    ++passed_count;
                    oss << "DATABASE='" << *val << "'";
                } else {
                    oss << "DATABASE returned empty; ";
                }
            }

            if (mask & SQL_FN_SYS_USERNAME) {
                ++tested;
                auto val = exec_scalar("SELECT {fn USER()}");
                if (val && !val->empty()) {
                    ++passed_count;
                    if (!oss.str().empty()) oss << ", ";
                    oss << "USER='" << *val << "'";
                } else {
                    oss << "USER returned empty; ";
                }
            }

            r.actual = std::to_string(passed_count) + "/" + std::to_string(tested)
                          + " system functions passed. " + oss.str();
            if (passed_count < tested) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
            }
            if (tested == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no system function support";
            }
        });
}

TestResult EscapeSequenceTests::test_datetime_literal_escapes() {
    return run_test(
        "test_datetime_literal_escapes", "SQLExecDirect + SQLGetData",
        "Date/time/timestamp literal escapes return correct temporal values",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Date/Time/Timestamp Escape Sequences",
        [&](TestResult& r) {
            int passed_count = 0;
            std::ostringstream oss;

            // Test {d 'YYYY-MM-DD'}
            auto date_val = exec_scalar("SELECT {d '2026-01-15'}");
            if (date_val && date_val->find("2026") != std::string::npos && date_val->find("01") != std::string::npos) {
                ++passed_count;
            } else {
                oss << "Date: '" << (date_val ? *date_val : "NULL") << "'; ";
            }

            // Test {t 'HH:MM:SS'}
            auto time_val = exec_scalar("SELECT {t '14:30:00'}");
            if (time_val && time_val->find("14") != std::string::npos && time_val->find("30") != std::string::npos) {
                ++passed_count;
            } else {
                oss << "Time: '" << (time_val ? *time_val : "NULL") << "'; ";
            }

            // Test {ts 'YYYY-MM-DD HH:MM:SS'}
            auto ts_val = exec_scalar("SELECT {ts '2026-01-15 14:30:00'}");
            if (ts_val && ts_val->find("2026") != std::string::npos && ts_val->find("14") != std::string::npos) {
                ++passed_count;
            } else {
                oss << "Timestamp: '" << (ts_val ? *ts_val : "NULL") << "'; ";
            }

            r.actual = std::to_string(passed_count) + "/3 datetime literal escapes passed";
            if (passed_count < 3) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

TestResult EscapeSequenceTests::test_like_escape_sequence() {
    return run_test(
        "test_like_escape_sequence",
        "SQLGetInfo(SQL_LIKE_ESCAPE_CLAUSE) + SQLExecDirect",
        "LIKE escape sequence {escape '\\'} is supported",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.8, LIKE Escape Sequence",
        [&](TestResult& r) {
            // Check SQL_LIKE_ESCAPE_CLAUSE
            SQLCHAR buf[16] = {0};
            SQLSMALLINT len = 0;
            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_LIKE_ESCAPE_CLAUSE,
                                       buf, sizeof(buf), &len);
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQL_LIKE_ESCAPE_CLAUSE not supported";
                return;
            }

            std::string like_support(reinterpret_cast<char*>(buf), len);
            if (like_support == "N") {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver reports SQL_LIKE_ESCAPE_CLAUSE = 'N'";
                return;
            }

            // D44: the probe used to read the string and stop, which gave
            // every driver a free point for a claim nobody checked. It sends
            // the escape now and holds the driver to it. D42 taught the
            // reference driver to evaluate LIKE, which is what made this
            // possible - before, the probe would have been failing the
            // fixture rather than testing a driver.
            //
            // With `!` named as the escape, `x!_y` matches the literal `x_y`
            // and nothing else. Two statements, because either answer alone
            // could be produced by a driver that ignores the clause: one that
            // ignores it matches both, one that rejects the pattern matches
            // neither.
            struct Case { const char* subject; bool should_match; };
            static const Case kCases[] = {
                {"x_y", true},    // the escaped underscore, matched literally
                {"xzy", false},   // would match only if `_` stayed a wildcard
            };

            std::ostringstream oss;
            oss << "SQL_LIKE_ESCAPE_CLAUSE = '" << like_support << "'";
            bool all_correct = true;
            bool ran_any = false;

            for (const auto& c : kCases) {
                const std::string predicate =
                    std::string(" WHERE '") + c.subject
                    + "' LIKE 'x!_y' ESCAPE '!'";
                // A2's dialect helper, with the predicate appended to each
                // variant - Firebird needs a FROM, Oracle wants DUAL.
                std::vector<std::string> variants;
                for (const auto& base : literal_select_variants("SELECT 1")) {
                    variants.push_back(base + predicate);
                }
                core::OdbcStatement stmt(conn_);
                auto attempt = execute_first_working(stmt, variants);
                if (!attempt) {
                    oss << "; '" << c.subject << "' did not execute";
                    continue;
                }
                int rows = 0;
                while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) ++rows;
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
                ran_any = true;
                const bool matched = rows > 0;
                oss << "; '" << c.subject << "' " << (matched ? "matched" : "did not match");
                if (matched != c.should_match) all_correct = false;
            }

            r.actual = oss.str();
            if (!ran_any) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion =
                    "No dialect variant of a literal SELECT with a WHERE "
                    "executed, so the escape could not be sent.";
                return;
            }
            if (!all_correct) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion =
                    "The driver advertises SQL_LIKE_ESCAPE_CLAUSE but does "
                    "not honour the ESCAPE clause: with '!' named as the "
                    "escape, 'x!_y' must match the literal 'x_y' and must not "
                    "match 'xzy'. A driver that matches both is ignoring the "
                    "clause; one that matches neither is rejecting the "
                    "pattern.";
            }
        });
}

// ---------------------------------------------------------------------------
// Outer Join & Interval Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_outer_join_escape() {
    return run_test(
        "test_outer_join_escape", "SQLGetInfo(SQL_OJ_CAPABILITIES)",
        "Driver reports outer join capabilities",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.8, Outer Join Escape Sequence",
        [&](TestResult& r) {
            auto oj_caps = get_info_uint(SQL_OJ_CAPABILITIES);
            if (!oj_caps) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQL_OJ_CAPABILITIES not supported";
                return;
            }

            SQLUINTEGER caps = *oj_caps;
            std::ostringstream oss;
            if (caps & SQL_OJ_LEFT) oss << "LEFT ";
            if (caps & SQL_OJ_RIGHT) oss << "RIGHT ";
            if (caps & SQL_OJ_FULL) oss << "FULL ";
            if (caps & SQL_OJ_NESTED) oss << "NESTED ";
            if (caps & SQL_OJ_NOT_ORDERED) oss << "NOT_ORDERED ";
            if (caps & SQL_OJ_INNER) oss << "INNER ";
            if (caps & SQL_OJ_ALL_COMPARISON_OPS) oss << "ALL_COMPARISON_OPS ";

            // D44: the probe used to read the bitmask and stop. It sends the
            // escape now, for the one capability it can construct portably: a
            // LEFT OUTER JOIN of a discovered table to itself, which needs no
            // second table and no derived table. D43 assumed the reference
            // driver could run neither and therefore could not be sent this;
            // re-measuring showed `FROM {oj T1 LEFT OUTER JOIN T2 ON …}` does
            // execute, so the blocker was not where the row thought it was.
            r.actual = "OJ capabilities: " +
                       (oss.str().empty() ? std::string("none") : oss.str());

            if (!(caps & SQL_OJ_LEFT)) {
                // Nothing claimed, nothing to hold the driver to.
                r.status = TestStatus::INFORMATIONAL;
                r.actual += " (LEFT not claimed, so nothing to send)";
                return;
            }

            const auto tables = discover_tables(1);
            if (tables.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual += "; SQLTables reported no table to join";
                r.suggestion =
                    "The probe joins one discovered table to itself, so it "
                    "needs SQLTables to report at least one.";
                return;
            }

            // A self-join needs no second table and no column list: `ON 1 = 1`
            // is a constant join condition every engine in the matrix accepts,
            // and the point is whether the {oj} escape is accepted at all.
            const std::string name = tables.front().qualified();
            const std::string sql =
                "SELECT * FROM {oj " + name + " T1 LEFT OUTER JOIN "
                + name + " T2 ON 1 = 1}";

            core::OdbcStatement stmt(conn_);
            const SQLRETURN rc = SQLExecDirect(
                stmt.get_handle(),
                reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())),
                SQL_NTS);
            r.actual += "; sent `" + sql + "` -> rc=" + std::to_string(rc);
            if (!SQL_SUCCEEDED(rc)) {
                r.actual += " " + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion =
                    "The driver advertises SQL_OJ_LEFT in "
                    "SQL_OJ_CAPABILITIES but rejected a LEFT OUTER JOIN sent "
                    "through the {oj} escape. Either support the escape or "
                    "stop claiming the capability.";
                return;
            }
            SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
        });
}

TestResult EscapeSequenceTests::test_interval_literal_escape() {
    return run_test(
        "test_interval_literal_escape", "SQLGetInfo(SQL_DATETIME_LITERALS)",
        "Driver reports datetime literal support",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8, Interval Escape Sequence",
        [&](TestResult& r) {
            auto dt_literals = get_info_uint(SQL_DATETIME_LITERALS);
            if (!dt_literals) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQL_DATETIME_LITERALS not supported";
                return;
            }

            SQLUINTEGER mask = *dt_literals;
            std::ostringstream oss;
            if (mask & SQL_DL_SQL92_DATE) oss << "DATE ";
            if (mask & SQL_DL_SQL92_TIME) oss << "TIME ";
            if (mask & SQL_DL_SQL92_TIMESTAMP) oss << "TIMESTAMP ";
            if (mask & SQL_DL_SQL92_INTERVAL_YEAR) oss << "INTERVAL_YEAR ";
            if (mask & SQL_DL_SQL92_INTERVAL_MONTH) oss << "INTERVAL_MONTH ";
            if (mask & SQL_DL_SQL92_INTERVAL_DAY) oss << "INTERVAL_DAY ";

            // D44: the probe used to read the bitmask and stop. It now sends
            // the escape for each literal kind the driver claims, and fails on
            // any claimed-but-rejected one - the same shape as
            // test_scalar_function_claim_vs_execute further up this file.
            struct Literal { SQLUINTEGER bit; const char* expr; const char* name; };
            static const Literal kLiterals[] = {
                {SQL_DL_SQL92_DATE,      "{d '2026-01-15'}",              "DATE"},
                {SQL_DL_SQL92_TIME,      "{t '14:30:00'}",                "TIME"},
                {SQL_DL_SQL92_TIMESTAMP, "{ts '2026-01-15 14:30:00'}",    "TIMESTAMP"},
            };

            std::vector<std::string> broken;
            int claimed = 0;
            for (const auto& lit : kLiterals) {
                if (!(mask & lit.bit)) continue;
                ++claimed;
                core::OdbcStatement stmt(conn_);
                auto attempt = execute_first_working(
                    stmt,
                    literal_select_variants(std::string("SELECT ") + lit.expr));
                if (attempt) {
                    SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
                } else {
                    broken.push_back(lit.name);
                }
            }

            std::ostringstream detail;
            detail << "Datetime literals: "
                   << (oss.str().empty() ? std::string("none") : oss.str())
                   << "- sent " << claimed << " claimed literal(s)";
            if (!broken.empty()) {
                detail << "; rejected:";
                for (const auto& b : broken) detail << " " << b;
            }
            r.actual = detail.str();

            if (claimed == 0) {
                // Nothing claimed, nothing to hold the driver to.
                r.status = TestStatus::INFORMATIONAL;
                return;
            }
            if (!broken.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion =
                    "SQL_DATETIME_LITERALS claims support for a literal the "
                    "driver then rejects. Either accept the escape or clear "
                    "the bit.";
            }
        });
}

// ---------------------------------------------------------------------------
// Procedure Call Escape Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_call_escape_translation() {
    return run_test(
        "test_call_escape_translation", "SQLNativeSql",
        "SQLNativeSql translates {CALL proc(?,?)} and {?=CALL func(?)} escape syntax",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence",
        [&](TestResult& r) {
            auto t1 = call_native_sql("{CALL my_procedure(?,?)}");
            auto t2 = call_native_sql("{?=CALL my_function(?)}");

            std::ostringstream oss;
            int passed_count = 0;

            if (t1 && !t1->empty() && t1->find('{') == std::string::npos) {
                ++passed_count;
                oss << "CALL->'" << *t1 << "'";
            } else {
                oss << "CALL not translated";
            }

            if (t2 && !t2->empty() && t2->find('{') == std::string::npos) {
                ++passed_count;
                oss << "; ?=CALL->'" << *t2 << "'";
            } else {
                oss << "; ?=CALL not translated";
            }

            r.actual = oss.str();
            if (passed_count < 2) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "The driver's escape parser should translate CALL escape sequences to native syntax";
            }
        });
}

TestResult EscapeSequenceTests::test_call_escape_format_variants() {
    return run_test(
        "test_call_escape_format_variants", "SQLNativeSql",
        "All 5 CALL escape format variants from ODBC spec are translated",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence",
        [&](TestResult& r) {
            static const char* variants[] = {
                "{CALL proc}",
                "{CALL proc()}",
                "{CALL proc(?,?)}",
                "{?=CALL func(?,?)}",
                "{?=CALL func}",
            };

            int passed_count = 0;
            std::ostringstream oss;

            for (const auto& v : variants) {
                auto translated = call_native_sql(v);
                if (translated && !translated->empty() && translated->find('{') == std::string::npos) {
                    ++passed_count;
                } else {
                    oss << "'" << v << "' not translated; ";
                }
            }

            r.actual = std::to_string(passed_count) + "/5 CALL variants translated";
            if (passed_count < 5) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
        });
}

// ── PORT plan §4.3 — {CALL …} IN/OUT/INOUT parameter direction probes ────
//
// All three probes target a known stored procedure, MOCK_INOUT, which the
// mock-driver registers in every catalog preset (params = IN INTEGER,
// OUT INTEGER, INOUT VARCHAR). Real drivers rarely have a procedure of
// that exact name; the probes SKIP_INCONCLUSIVE when SQLProcedures
// reports MOCK_INOUT is absent — the suggestion explains how to register
// an equivalent. The mock-driver path is what the e2e canary asserts.

namespace {

// Discover whether MOCK_INOUT (or any user-registered equivalent the user
// may rename it to via env var) is visible via SQLProcedures. Returns the
// procedure name on success, empty string when the catalog has no
// matching entry.
std::string find_named_procedure(core::OdbcConnection& conn,
                                 const char* name_filter) {
    core::OdbcStatement stmt(conn);
    SQLRETURN rc = SQLProcedures(stmt.get_handle(),
                                 nullptr, 0,
                                 nullptr, 0,
                                 reinterpret_cast<SQLCHAR*>(
                                     const_cast<char*>(name_filter)),
                                 SQL_NTS);
    if (!SQL_SUCCEEDED(rc)) return {};
    while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
        char buf[128] = {0};
        SQLLEN ind = 0;
        if (SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 3, SQL_C_CHAR,
                                      buf, sizeof(buf), &ind)) &&
            ind != SQL_NULL_DATA) {
            return std::string(buf);
        }
    }
    return {};
}

std::string find_mock_inout(core::OdbcConnection& conn) {
    return find_named_procedure(conn, "MOCK_INOUT");
}

struct CallProbeOutcome {
    bool prepared    = false;
    bool bind_ok     = false;
    bool execute_ok  = false;
    SQLRETURN exec_rc = SQL_ERROR;
    std::string error;            // diagnostic text on first failure
    SQLINTEGER out_int   = 0;
    SQLLEN     out_int_ind = 0;
    std::string inout_text;       // post-execute buffer contents
    SQLLEN     inout_ind = 0;
};

// Executes `{CALL MOCK_INOUT(?, ?, ?)}` with the procedure's three params
// bound. Returns the post-execute state of OUT and INOUT slots. Probes
// inspect different parts of the outcome.
CallProbeOutcome run_mock_inout_call(core::OdbcConnection& conn,
                                     SQLINTEGER in_value,
                                     const char* inout_initial)
{
    CallProbeOutcome out;
    core::OdbcStatement stmt(conn);

    const char* sql = "{CALL MOCK_INOUT(?, ?, ?)}";
    SQLRETURN rc = SQLPrepare(stmt.get_handle(),
                              reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)),
                              SQL_NTS);
    if (!SQL_SUCCEEDED(rc)) {
        out.error = "SQLPrepare returned " + std::to_string(rc);
        return out;
    }
    out.prepared = true;

    SQLINTEGER in_n      = in_value;
    SQLLEN     in_n_ind  = 0;
    out.out_int     = static_cast<SQLINTEGER>(0xDEADBEEFu);  // sentinel
    out.out_int_ind = sizeof(SQLINTEGER);
    char inout_buf[64] = {0};
    const std::string initial(inout_initial);
    const size_t copy_len = std::min<size_t>(sizeof(inout_buf) - 1, initial.size());
    std::memcpy(inout_buf, initial.data(), copy_len);
    inout_buf[copy_len] = '\0';
    out.inout_ind = static_cast<SQLLEN>(copy_len);

    rc = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                          SQL_C_SLONG, SQL_INTEGER, 10, 0,
                          &in_n, sizeof(in_n), &in_n_ind);
    if (!SQL_SUCCEEDED(rc)) {
        out.error = "SQLBindParameter(1, IN) returned " + std::to_string(rc);
        return out;
    }
    rc = SQLBindParameter(stmt.get_handle(), 2, SQL_PARAM_OUTPUT,
                          SQL_C_SLONG, SQL_INTEGER, 10, 0,
                          &out.out_int, sizeof(out.out_int), &out.out_int_ind);
    if (!SQL_SUCCEEDED(rc)) {
        out.error = "SQLBindParameter(2, OUT) returned " + std::to_string(rc);
        return out;
    }
    rc = SQLBindParameter(stmt.get_handle(), 3, SQL_PARAM_INPUT_OUTPUT,
                          SQL_C_CHAR, SQL_VARCHAR, sizeof(inout_buf) - 1, 0,
                          inout_buf, sizeof(inout_buf), &out.inout_ind);
    if (!SQL_SUCCEEDED(rc)) {
        out.error = "SQLBindParameter(3, INOUT) returned " + std::to_string(rc);
        return out;
    }
    out.bind_ok = true;

    out.exec_rc = SQLExecute(stmt.get_handle());
    if (!SQL_SUCCEEDED(out.exec_rc)) {
        out.error = "SQLExecute returned " + std::to_string(out.exec_rc);
        return out;
    }
    out.execute_ok = true;

    // A16: drain any result sets the procedure produced before reading the
    // output parameters. A driver may legally defer populating bound OUT and
    // INOUT buffers until every result set has been consumed, so a conformant
    // driver whose SP body returns a result set was being FAILed at
    // Severity::ERR for "did not write to the bound buffer".
    //
    // SQL_NO_DATA ends the sequence; anything else means there are no more
    // result sets to wait for, and it is not this helper's job to judge that.
    int guard = 0;
    while (SQLMoreResults(stmt.get_handle()) == SQL_SUCCESS) {
        if (++guard > 100) break;   // a driver stuck on the same result set
    }

    out.inout_text = std::string(inout_buf);
    return out;
}

} // namespace

TestResult EscapeSequenceTests::test_call_escape_in_parameter() {
    return run_test(
        "test_call_escape_in_parameter", "SQLPrepare/SQLBindParameter/SQLExecute",
        "{CALL …(?)} prepares, binds an SQL_PARAM_INPUT parameter, and "
        "executes successfully against a registered procedure",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 Procedure Call Escape — SQL_PARAM_INPUT direction",
        [&](TestResult& r) {
            std::string proc = find_mock_inout(conn_);
            if (proc.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Test procedure MOCK_INOUT not visible via "
                           "SQLProcedures";
                r.suggestion = "Register a 3-parameter procedure named "
                               "MOCK_INOUT(IN n INTEGER, OUT m INTEGER, "
                               "INOUT s VARCHAR(64)) so this probe can run "
                               "against your DBMS.";
                return;
            }
            auto outcome = run_mock_inout_call(conn_, 42, "hello");
            if (!outcome.execute_ok) {
                r.status = TestStatus::FAIL;
                r.actual = outcome.error;
                r.severity = Severity::ERR;
                return;
            }
            r.actual = "Prepared + bound (IN, OUT, INOUT) + executed against "
                     + proc + " (SQL_PARAM_INPUT path verified)";
        });
}

TestResult EscapeSequenceTests::test_call_escape_out_parameter() {
    return run_test(
        "test_call_escape_out_parameter", "SQLBindParameter(SQL_PARAM_OUTPUT)",
        "{CALL …(?, ?, ?)} writes back to a SQL_PARAM_OUTPUT bound buffer",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter — SQL_PARAM_OUTPUT direction",
        [&](TestResult& r) {
            std::string proc = find_mock_inout(conn_);
            if (proc.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Test procedure MOCK_INOUT not visible via "
                           "SQLProcedures";
                r.suggestion = "Register a 3-parameter procedure named "
                               "MOCK_INOUT(IN n INTEGER, OUT m INTEGER, "
                               "INOUT s VARCHAR(64)) where m := n*2.";
                return;
            }
            const SQLINTEGER kIn = 42;
            const SQLINTEGER kExpectedOut = 84;
            auto outcome = run_mock_inout_call(conn_, kIn, "hello");
            if (!outcome.execute_ok) {
                r.status = TestStatus::FAIL;
                r.actual = outcome.error;
                return;
            }
            std::ostringstream oss;
            oss << "OUT buffer post-execute: " << outcome.out_int
                << " (expected " << kExpectedOut << " for n=" << kIn
                << ", n*2 contract); indicator=" << outcome.out_int_ind;
            r.actual = oss.str();
            if (outcome.out_int == static_cast<SQLINTEGER>(0xDEADBEEF)) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver accepted SQL_PARAM_OUTPUT binding but "
                               "did not write to the bound buffer (sentinel "
                               "value survived).";
                return;
            }
            if (outcome.out_int != kExpectedOut) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver wrote a value, but it doesn't match "
                               "the procedure contract m := n*2.";
            }
        });
}

// ── A17 / I7 — the `{?=CALL fn(…)}` return-value form, executed ──────────
//
// `{?=CALL …}` appeared in this file only as *input to SQLNativeSql*: the
// driver's translation was checked, its execution never was. The defect that
// matters is the parameter numbering. In the function-call escape the leading
// `?` is the return value and takes parameter 1, so the first argument is
// parameter 2 — and a driver that binds the first argument as parameter 1
// computes a wrong answer with no diagnostic at all.
//
// MOCK_FN(a, b) returns a*10 + b, chosen so an argument in the wrong slot is
// visibly wrong rather than coincidentally right: with a=4, b=7 the answer is
// 47, and the off-by-one reading (the return-value slot taken as `a`) gives
// 0*10 + 4 = 4, which the probe names explicitly.
TestResult EscapeSequenceTests::test_function_call_escape_return_value() {
    return run_test(
        "test_function_call_escape_return_value",
        "SQLPrepare/SQLBindParameter/SQLExecute",
        "{?=CALL fn(?, ?)} executes and writes the function's return value "
        "to parameter 1, with the arguments numbered from 2",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 Procedure Call Escape — function return value",
        [&](TestResult& r) {
            const std::string fn = find_named_procedure(conn_, "MOCK_FN");
            if (fn.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Test function MOCK_FN not visible via SQLProcedures";
                r.suggestion =
                    "Register a two-argument function named "
                    "MOCK_FN(a INTEGER, b INTEGER) RETURNS INTEGER that "
                    "returns a*10 + b, so this probe can check that your "
                    "driver numbers a function's arguments from parameter 2.";
                return;
            }

            // One call, parameterised by what the return-value buffer starts
            // as. A sentinel start detects a driver that binds parameter 1 and
            // never writes it; a zero start makes the off-by-one reading
            // (arguments taken one slot early) come out as exactly 0*10 + a,
            // which is a value the probe can name rather than guess at.
            struct CallOutcome {
                bool ok = false;
                std::string error;
                SQLINTEGER ret = 0;
                SQLLEN ret_ind = 0;
            };
            auto run_call = [&](SQLINTEGER seed, SQLINTEGER a, SQLINTEGER b)
                -> CallOutcome
            {
                CallOutcome oc;
                core::OdbcStatement stmt(conn_);
                const char* sql = "{?=CALL MOCK_FN(?, ?)}";
                SQLRETURN rc = SQLPrepare(
                    stmt.get_handle(),
                    reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)), SQL_NTS);
                if (!SQL_SUCCEEDED(rc)) {
                    oc.error = "SQLPrepare(\"" + std::string(sql)
                             + "\") returned " + std::to_string(rc) + "; "
                             + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
                    return oc;
                }

                oc.ret = seed;
                oc.ret_ind = sizeof(SQLINTEGER);
                SQLINTEGER arg_a = a, arg_b = b;
                SQLLEN a_ind = 0, b_ind = 0;

                struct Bind { SQLUSMALLINT n; SQLSMALLINT dir; SQLINTEGER* p; SQLLEN* ind; };
                const Bind binds[] = {
                    {1, SQL_PARAM_OUTPUT, &oc.ret, &oc.ret_ind},
                    {2, SQL_PARAM_INPUT,  &arg_a,  &a_ind},
                    {3, SQL_PARAM_INPUT,  &arg_b,  &b_ind},
                };
                for (const auto& bind : binds) {
                    rc = SQLBindParameter(stmt.get_handle(), bind.n, bind.dir,
                                          SQL_C_SLONG, SQL_INTEGER, 10, 0,
                                          bind.p, sizeof(SQLINTEGER), bind.ind);
                    if (!SQL_SUCCEEDED(rc)) {
                        oc.error = "SQLBindParameter(" + std::to_string(bind.n)
                                 + ") returned " + std::to_string(rc) + "; "
                                 + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
                        return oc;
                    }
                }

                rc = SQLExecute(stmt.get_handle());
                if (!SQL_SUCCEEDED(rc)) {
                    oc.error = "SQLExecute returned " + std::to_string(rc) + "; "
                             + first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle());
                    return oc;
                }

                // A16: a driver may defer writing the output buffers until
                // every result set has been consumed.
                int guard = 0;
                while (SQLMoreResults(stmt.get_handle()) == SQL_SUCCESS) {
                    if (++guard > 100) break;
                }
                oc.ok = true;
                return oc;
            };

            const SQLINTEGER kSentinel = static_cast<SQLINTEGER>(0xDEADBEEFu);
            const SQLINTEGER kA = 4, kB = 7;
            const SQLINTEGER kExpected = 47;      // 4*10 + 7

            const CallOutcome first = run_call(kSentinel, kA, kB);
            if (!first.ok) {
                r.status = TestStatus::FAIL;
                r.actual = first.error;
                r.suggestion =
                    "The function-call escape is Core-level. A driver that "
                    "translates {?=CALL ...} in SQLNativeSql but cannot "
                    "prepare or execute it is translating text it will not run.";
                return;
            }

            std::ostringstream oss;
            oss << "return-value buffer post-execute: " << first.ret
                << " (expected " << kExpected << " for a=" << kA
                << ", b=" << kB << ", a*10+b contract); indicator="
                << first.ret_ind;
            r.actual = oss.str();

            if (first.ret == kSentinel) {
                r.status = TestStatus::FAIL;
                r.suggestion =
                    "Driver accepted the parameter-1 return-value binding but "
                    "never wrote to it (the sentinel survived). In "
                    "{?=CALL fn(...)} parameter 1 is the function's return "
                    "value and must be written back.";
                return;
            }
            if (first.ret == kExpected) return;   // PASS

            r.status = TestStatus::FAIL;
            // Confirm the classic cause before naming it: with the return
            // buffer starting at 0, reading the arguments one slot early gives
            // exactly 0*10 + a.
            const CallOutcome confirm = run_call(0, kA, kB);
            if (confirm.ok && confirm.ret == kA) {
                oss << "; with the return buffer zeroed the answer was "
                    << confirm.ret << ", which is 0*10+a";
                r.actual = oss.str();
                r.suggestion =
                    "The answer is what you get by reading the arguments one "
                    "parameter early: the driver bound the first argument as "
                    "parameter 1 instead of 2. In {?=CALL fn(...)} parameter 1 "
                    "is the return value and the arguments start at 2.";
                return;
            }
            r.suggestion =
                "Driver wrote a return value, but it does not match the "
                "function contract a*10 + b. The usual cause is parameter "
                "numbering: in {?=CALL fn(...)} parameter 1 is the return "
                "value and the arguments start at 2.";
        });
}

// ── PORT plan §4.12 — claim-vs-execute matrix ─────────────────────────────
//
// SQLGetInfo(SQL_STRING_FUNCTIONS / SQL_NUMERIC_FUNCTIONS /
// SQL_TIMEDATE_FUNCTIONS / SQL_SYSTEM_FUNCTIONS) is the "what does this
// driver claim to support" surface. The existing per-category probes
// each test ~5 functions; this consolidated probe builds a single matrix
// across all four categories, runs a representative query per claimed
// function, and FAILs on any claimed-but-broken bit. Diagnostic shape:
//   "STRING:6/6 NUMERIC:5/5 TIMEDATE:3/3 SYSTEM:0/0 (broken: SUBSTRING)"

TestResult EscapeSequenceTests::test_scalar_function_claim_vs_execute() {
    return run_test(
        "test_scalar_function_claim_vs_execute",
        "SQLGetInfo(SQL_*_FUNCTIONS) + SQLExecDirect",
        "Every scalar function the driver claims in SQL_*_FUNCTIONS "
        "actually runs without error",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 Appendix E — scalar function bitmasks",
        [&](TestResult& r) {
            struct FnEntry {
                SQLUINTEGER flag;
                const char* sql;
                const char* name;
            };
            // String functions (subset that all engines should reject in
            // unison or accept in unison — keep representative coverage).
            static const FnEntry kString[] = {
                {SQL_FN_STR_UCASE,     "SELECT {fn UCASE('a')}",        "UCASE"},
                {SQL_FN_STR_LCASE,     "SELECT {fn LCASE('A')}",        "LCASE"},
                {SQL_FN_STR_LENGTH,    "SELECT {fn LENGTH('xy')}",      "LENGTH"},
                {SQL_FN_STR_LTRIM,     "SELECT {fn LTRIM(' x')}",       "LTRIM"},
                {SQL_FN_STR_RTRIM,     "SELECT {fn RTRIM('x ')}",       "RTRIM"},
                {SQL_FN_STR_CONCAT,    "SELECT {fn CONCAT('a','b')}",   "CONCAT"},
                {SQL_FN_STR_SUBSTRING, "SELECT {fn SUBSTRING('abc',2,1)}", "SUBSTRING"},
            };
            static const FnEntry kNumeric[] = {
                {SQL_FN_NUM_ABS,     "SELECT {fn ABS(-5)}",      "ABS"},
                {SQL_FN_NUM_FLOOR,   "SELECT {fn FLOOR(3.7)}",   "FLOOR"},
                {SQL_FN_NUM_CEILING, "SELECT {fn CEILING(3.2)}", "CEILING"},
                {SQL_FN_NUM_SQRT,    "SELECT {fn SQRT(9)}",      "SQRT"},
                {SQL_FN_NUM_ROUND,   "SELECT {fn ROUND(3.14,1)}","ROUND"},
            };
            static const FnEntry kTimedate[] = {
                {SQL_FN_TD_NOW,         "SELECT {fn NOW()}",          "NOW"},
                {SQL_FN_TD_CURDATE,     "SELECT {fn CURDATE()}",      "CURDATE"},
                {SQL_FN_TD_CURTIME,     "SELECT {fn CURTIME()}",      "CURTIME"},
                {SQL_FN_TD_YEAR,        "SELECT {fn YEAR({d '2026-01-01'})}",  "YEAR"},
                {SQL_FN_TD_MONTH,       "SELECT {fn MONTH({d '2026-01-01'})}", "MONTH"},
            };
            static const FnEntry kSystem[] = {
                {SQL_FN_SYS_USERNAME, "SELECT {fn USER()}",     "USER"},
                {SQL_FN_SYS_DBNAME,   "SELECT {fn DATABASE()}", "DATABASE"},
                {SQL_FN_SYS_IFNULL,   "SELECT {fn IFNULL(NULL, 1)}", "IFNULL"},
            };

            struct Section {
                const char* label;
                SQLUSMALLINT info_type;
                const FnEntry* entries;
                size_t count;
            };
            const Section sections[] = {
                {"STRING",   SQL_STRING_FUNCTIONS,   kString,
                    sizeof(kString) / sizeof(kString[0])},
                {"NUMERIC",  SQL_NUMERIC_FUNCTIONS,  kNumeric,
                    sizeof(kNumeric) / sizeof(kNumeric[0])},
                {"TIMEDATE", SQL_TIMEDATE_FUNCTIONS, kTimedate,
                    sizeof(kTimedate) / sizeof(kTimedate[0])},
                {"SYSTEM",   SQL_SYSTEM_FUNCTIONS,   kSystem,
                    sizeof(kSystem) / sizeof(kSystem[0])},
            };

            std::ostringstream summary;
            std::ostringstream broken_list;
            int total_claimed = 0;
            int total_passed  = 0;
            int total_broken  = 0;

            for (size_t s = 0; s < sizeof(sections) / sizeof(sections[0]); ++s) {
                const auto& sec = sections[s];
                auto bits = get_info_uint(sec.info_type);
                if (s > 0) summary << " ";
                if (!bits) {
                    summary << sec.label << ":?/?";
                    continue;
                }
                int claimed = 0;
                int passed_count = 0;
                for (size_t i = 0; i < sec.count; ++i) {
                    const auto& fn = sec.entries[i];
                    if (!(*bits & fn.flag)) continue;
                    ++claimed;
                    auto val = exec_scalar(fn.sql);
                    if (val.has_value()) {
                        ++passed_count;
                    } else {
                        if (total_broken > 0 || claimed > passed_count) {
                            // Track the first failures globally.
                        }
                        if (total_broken > 0) broken_list << ", ";
                        broken_list << fn.name;
                        ++total_broken;
                    }
                }
                summary << sec.label << ":" << passed_count << "/" << claimed;
                total_claimed += claimed;
                total_passed  += passed_count;
            }

            std::ostringstream actual;
            actual << summary.str();
            if (total_broken > 0) {
                actual << " (broken: " << broken_list.str() << ")";
            }
            r.actual = actual.str();

            if (total_claimed == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLGetInfo returned no scalar function bits — "
                           "either the driver doesn't support the bitmasks "
                           "or it claims zero scalar functions. " + r.actual;
                return;
            }
            if (total_broken > 0) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Each function listed in SQL_*_FUNCTIONS must "
                               "execute without error. The 'broken' list "
                               "shows functions claimed but rejected at "
                               "execute time — fix the bitmask, or fix the "
                               "function support.";
                return;
            }
            // total_claimed > 0 and 0 broken → PASS, leave default INFO.
        });
}

TestResult EscapeSequenceTests::test_call_escape_inout_parameter() {
    return run_test(
        "test_call_escape_inout_parameter", "SQLBindParameter(SQL_PARAM_INPUT_OUTPUT)",
        "{CALL …(?, ?, ?)} round-trips a value through a "
        "SQL_PARAM_INPUT_OUTPUT bound buffer",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter — SQL_PARAM_INPUT_OUTPUT direction",
        [&](TestResult& r) {
            std::string proc = find_mock_inout(conn_);
            if (proc.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Test procedure MOCK_INOUT not visible via "
                           "SQLProcedures";
                r.suggestion = "Register a 3-parameter procedure named "
                               "MOCK_INOUT(IN n INTEGER, OUT m INTEGER, "
                               "INOUT s VARCHAR(64)) where s := UPPER(s).";
                return;
            }
            auto outcome = run_mock_inout_call(conn_, 1, "hello");
            if (!outcome.execute_ok) {
                r.status = TestStatus::FAIL;
                r.actual = outcome.error;
                return;
            }
            r.actual = "INOUT buffer post-execute: '" + outcome.inout_text
                     + "' (input was 'hello', expected 'HELLO' per "
                       "UPPER contract); indicator=" + std::to_string(outcome.inout_ind);
            if (outcome.inout_text == "hello") {
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver accepted SQL_PARAM_INPUT_OUTPUT but did "
                               "not write back — common bug shape: driver "
                               "treats INOUT as IN-only.";
                return;
            }
            if (outcome.inout_text != "HELLO") {
                r.status = TestStatus::FAIL;
                r.suggestion = "Driver wrote back, but the value doesn't match "
                               "the procedure contract s := UPPER(s).";
            }
        });
}

} // namespace odbc_crusher::tests
