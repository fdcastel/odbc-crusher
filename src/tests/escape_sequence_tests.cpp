#include "escape_sequence_tests.hpp"
#include "core/guarded_buffer.hpp"
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

namespace {

// R3 — the first procedure the catalog will admit to, or "" when there is none.
//
// `test_call_escape_format_variants` used to ask SQLNativeSql to translate
// `{CALL proc}`, and `proc` exists in no database. A driver that resolves the
// procedure while translating — Firebird does, because it must choose between
// `execute procedure p` and `select * from p` — then fails every variant, and
// the probe read that as a missing CALL translator. Asking the catalog for a
// name that exists is what makes the question answerable.
//
// `find_named_procedure` further down this file does the same walk with a name
// filter; this is the no-filter form, and it is declared here because the probe
// that needs it comes first.
std::string find_any_procedure(core::OdbcConnection& conn) {
    try {
        core::OdbcStatement stmt(conn);
        // Null ProcName means "every procedure", which is the question here.
        SQLRETURN rc = SQLProcedures(stmt.get_handle(),
                                     nullptr, 0, nullptr, 0, nullptr, 0);
        if (!SQL_SUCCEEDED(rc)) return {};
        while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
            core::GuardedBuffer<char> buf(128, 0);   // D62
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 3, SQL_C_CHAR,
                                         buf.data(), buf.declared_bytes(), &ind)) &&
                ind != SQL_NULL_DATA) {
                auto name = core::bounded_string(buf.data(),
                                                 buf.declared_elements(), ind).value;
                if (!name.empty()) return name;
            }
        }
    } catch (const core::OdbcError&) {
        // A driver that cannot enumerate procedures is not this probe's
        // finding; the caller falls back to the placeholder and says so.
    }
    return {};
}

}  // namespace

std::vector<TestResult> EscapeSequenceTests::run() {
    return {
        // Discovery
        test_scalar_function_capabilities(),
        test_convert_function_capabilities(),

        // SQLNativeSql
        test_native_sql_scalar_functions(),
        test_native_sql_datetime_literals(),
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
    core::GuardedBuffer<SQLCHAR> out(4096, 0);  // D62
    SQLINTEGER out_len = 0;
    SQLRETURN ret = SQLNativeSql(
        conn_.get_handle(),
        const_cast<SQLCHAR*>(reinterpret_cast<const SQLCHAR*>(sql.c_str())),
        static_cast<SQLINTEGER>(sql.length()),
        out.data(), out.declared_bytes(), &out_len);
    if (SQL_SUCCEEDED(ret)) {
        // A3: out_len is the *total available* length, not the amount written,
        // and SQL_SUCCEEDED accepts the 01004 that accompanies truncation — so
        // std::string(out, out_len) read past the end of this stack buffer.
        // SQL_NO_TOTAL (-4) was worse: as a size_t it is SIZE_MAX - 3.
        return bounded_string(reinterpret_cast<const char*>(out.data()),
                              out.declared_elements(), out_len).value;
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

        core::GuardedBuffer<SQLCHAR> buf(1024, 0);  // D62
        SQLLEN ind = 0;
        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR, buf.data(), buf.declared_bytes(), &ind);
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
            bounded_string(reinterpret_cast<const char*>(buf.data()),
                           buf.declared_elements(), ind);
        out.truncated = bounded.truncated;
        if (bounded.truncated) {
            // A4: truncation is now a reportable outcome rather than silence.
            out.message = "value truncated at " + std::to_string(buf.declared_elements() - 1) +
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

            // C12: four copies of "read a bitmask, count the flags set in it,
            // append '<Label>: <n> funcs'". The separator was hand-written
            // into three of them as a leading ", ", which is why the order
            // could not change without editing strings.
            struct FunctionCategory {
                SQLUSMALLINT info_type;
                const char* label;
                std::initializer_list<SQLUINTEGER> flags;
            };
            static const FunctionCategory kCategories[] = {
                {SQL_STRING_FUNCTIONS, "String",
                 {SQL_FN_STR_CONCAT, SQL_FN_STR_LENGTH, SQL_FN_STR_LTRIM,
                  SQL_FN_STR_RTRIM, SQL_FN_STR_SUBSTRING, SQL_FN_STR_UCASE,
                  SQL_FN_STR_LCASE}},
                {SQL_NUMERIC_FUNCTIONS, "Numeric",
                 {SQL_FN_NUM_ABS, SQL_FN_NUM_CEILING, SQL_FN_NUM_FLOOR,
                  SQL_FN_NUM_ROUND, SQL_FN_NUM_SQRT, SQL_FN_NUM_MOD}},
                {SQL_TIMEDATE_FUNCTIONS, "Timedate",
                 {SQL_FN_TD_NOW, SQL_FN_TD_CURDATE, SQL_FN_TD_CURTIME,
                  SQL_FN_TD_YEAR, SQL_FN_TD_MONTH, SQL_FN_TD_DAYOFWEEK}},
                {SQL_SYSTEM_FUNCTIONS, "System",
                 {SQL_FN_SYS_DBNAME, SQL_FN_SYS_USERNAME, SQL_FN_SYS_IFNULL}},
            };

            for (const auto& cat : kCategories) {
                auto bits = get_info_uint(cat.info_type);
                if (!bits) continue;
                ++categories_found;
                int count = 0;
                for (SQLUINTEGER flag : cat.flags) {
                    if (*bits & flag) ++count;
                }
                if (categories_found > 1) oss << ", ";
                oss << cat.label << ": " << count << " funcs";
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

// C12: the epilogue three of the scalar-function probes share verbatim.
//
// test_{string,numeric,datetime}_scalar_functions ended with the same 22
// lines differing in one noun. The row proposed folding all five probes into
// one matrix; they use four different comparison semantics and one of them
// is not table-driven at all, so a matrix would need a mode enum and
// per-mode message handling - the same code with a dispatch in front. The
// epilogue is what actually repeats, so the epilogue is what moves.
//
// `noun` is the word in "Driver claims no <noun> function support".
// `count_noun` is the word in "<n>/<m> <count_noun> functions passed";
// `claim_noun` the one in "Driver claims no <claim_noun> function support".
// They differ in exactly one probe - datetime counts "datetime" and claims
// "timedate" - and collapsing them to one parameter would have silently
// rewritten that probe's output.
void finish_scalar_suite(TestResult& r, const char* count_noun,
                         const char* claim_noun, int tested,
                         int passed_count, int unsupported,
                         const std::string& failures) {
    r.actual = std::to_string(passed_count) + "/" + std::to_string(tested) +
               " " + count_noun + " functions passed";
    if (unsupported) {
        r.actual += " (" + std::to_string(unsupported) +
                    " claimed but not implemented)";
    }
    if (tested == 0) {
        // The bitmask claimed nothing, so there was nothing to run.
        r.status = TestStatus::SKIP_UNSUPPORTED;
        r.actual = std::string("Driver claims no ") + claim_noun +
                   " function support";
    } else if (unsupported == tested) {
        // A4: it claimed them all and implemented none. A driver being honest
        // about being incomplete is a skip, not a failure — but the report
        // names the SQLSTATE it used to say so, instead of printing 'NULL'.
        r.status = TestStatus::SKIP_UNSUPPORTED;
        r.actual = std::string("Driver claims ") + claim_noun +
                   " functions but implements none: " + failures;
    } else if (passed_count < tested - unsupported) {
        r.status = TestStatus::FAIL;
        r.actual += ". Failures: " + failures;
        r.severity = Severity::WARNING;
    }
}

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

            finish_scalar_suite(r, "string", "string", tested, passed_count,
                                unsupported, oss.str());
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

            // C6/E5: a `match` lambda stood here, accepting several spellings of
            // the same number ("5" vs "5.000000"). Nothing called it: the loop
            // below compares numerically with a 0.01 tolerance, which is the
            // same idea done directly. -Werror is what pointed at it.

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

            finish_scalar_suite(r, "numeric", "numeric", tested, passed_count,
                                unsupported, oss.str());
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

            finish_scalar_suite(r, "datetime", "timedate", tested,
                                passed_count, unsupported, oss.str());
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
            // D61: guarded, like every other SQLGetInfo string path.
            constexpr size_t kCapacity = 16;
            core::GuardedBuffer<char> buf(kCapacity, '\0');
            SQLSMALLINT len = 0;
            SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_LIKE_ESCAPE_CLAUSE,
                                       buf.data(),
                                       static_cast<SQLSMALLINT>(kCapacity), &len);
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQL_LIKE_ESCAPE_CLAUSE not supported";
                return;
            }

            // D61: the driver's length is an upper bound, not a promise.
            const std::string like_support =
                bounded_string(buf.data(), kCapacity, len).value;
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

TestResult EscapeSequenceTests::test_call_escape_format_variants() {
    return run_test(
        "test_call_escape_format_variants", "SQLNativeSql",
        "Every CALL escape format from the ODBC spec is translated",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence",
        [&](TestResult& r) {
            // C11: seven, not five. `test_native_sql_call_escape` and
            // `test_call_escape_translation` were the same check as this one
            // with different identifier names - same API, spec reference,
            // severity and assertion - and between them added only the
            // single-parameter forms. Folding those in and deleting both
            // leaves one probe with more coverage than the three had.
            //
            // R3 (IMPROVEMENT_PLAN_V2): the identifiers used to be the literal
            // `proc` and `func`, which exist in no database, and *any*
            // non-translation counted as a failure - including an error return.
            // Firebird has two native spellings for a procedure call,
            // `execute procedure p` for a non-selectable one and
            // `select * from p` for a selectable one, so its getNativeSql must
            // look the procedure up before it can choose and throws
            // `Unknown procedure 'PROC'` when it cannot. A driver with complete
            // and correct CALL-escape handling therefore scored 0/7, and that
            // verdict reached a published report. Ask the catalog for a name
            // that exists, and separate "refused to resolve" from "silently
            // left the braces in place" - only the second is a translation
            // defect.
            const std::string discovered = find_any_procedure(conn_);
            const std::string proc = discovered.empty() ? "proc" : discovered;

            const std::string variants[] = {
                "{CALL " + proc + "}",
                "{CALL " + proc + "()}",
                "{CALL " + proc + "(?)}",
                "{CALL " + proc + "(?,?)}",
                "{?=CALL " + proc + "}",
                "{?=CALL " + proc + "(?)}",
                "{?=CALL " + proc + "(?,?)}",
            };
            const int kVariantCount =
                static_cast<int>(sizeof(variants) / sizeof(variants[0]));

            int translated_count = 0;
            int refused_count = 0;
            std::ostringstream untranslated;   // success, braces still there
            std::ostringstream refused;        // SQLNativeSql returned an error
            std::string first_refusal_state;

            for (const auto& v : variants) {
                auto translated = call_native_sql(v);
                if (!translated) {
                    ++refused_count;
                    const std::string state = first_sqlstate(
                        SQL_HANDLE_DBC, conn_.get_handle(), "no SQLSTATE");
                    if (first_refusal_state.empty()) first_refusal_state = state;
                    refused << "'" << v << "' (" << state << "); ";
                } else if (translated->empty() ||
                           translated->find('{') != std::string::npos) {
                    untranslated << "'" << v << "' -> '" << *translated << "'; ";
                } else {
                    ++translated_count;
                }
            }

            std::ostringstream actual;
            actual << translated_count << "/" << kVariantCount
                   << " CALL variants translated";
            if (discovered.empty()) {
                actual << " (no procedure found via SQLProcedures, so the "
                          "placeholder name `proc` was used)";
            } else {
                actual << " using the procedure `" << discovered
                       << "` found via SQLProcedures";
            }

            const int untranslated_count =
                kVariantCount - translated_count - refused_count;

            if (untranslated_count > 0) {
                // The driver said yes and handed back an untranslated escape.
                // That is a translation defect however the name resolves.
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                actual << ". Returned success but left the escape in place: "
                       << untranslated.str();
                r.suggestion =
                    "SQLNativeSql must return native SQL with no ODBC escape "
                    "sequences left in it. Returning the input unchanged, with "
                    "SQL_SUCCESS, tells the application the translation "
                    "happened when it did not.";
            } else if (refused_count > 0) {
                // Cannot tell "will not translate CALL" from "was asked about a
                // name or arity this driver resolves at translation time and
                // could not find". Not a driver finding either way.
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                actual << ". " << refused_count
                       << " variant(s) returned an error rather than a "
                          "translation: " << refused.str();
                r.suggestion =
                    "This driver resolves the procedure while translating, so "
                    "SQLNativeSql fails for a name or parameter count it cannot "
                    "find (" + first_refusal_state + ") and the probe cannot "
                    "separate that from a missing CALL translator. Register a "
                    "procedure with one and two parameters, or a selectable "
                    "function for the {?=CALL} forms, to make this conclusive.";
            }
            r.actual = actual.str();
        });
}

namespace {

// ── R2 — the CALL-escape parameter-direction probes ─────────────────────
//
// These used to hard-code `{CALL MOCK_INOUT(?, ?, ?)}` and bind
// (IN, OUT, INOUT): a procedure name that existed only in this repo's mock
// driver, and a parameter shape only the mock has. Their own header admitted
// it — "Real drivers rarely have a procedure of that exact name" — so against
// every real driver they skipped, and the only SQL_PARAM_OUTPUT and
// SQL_PARAM_INPUT_OUTPUT coverage in the suite was coverage of the mock.
//
// Firebird makes the point concretely. Ask its driver about a two-in/two-out
// procedure and SQLProcedureColumns answers (IN, IN, OUT, OUT): inputs first,
// then outputs, and SQL_PARAM_INPUT_OUTPUT never appears, because Firebird's
// procedure model has no INOUT. No fixture can give it the shape the probes
// assumed.
//
// So the probes now *ask*. `describe_procedure` reads the parameter list the
// driver declares, `call_by_contract` binds exactly those in exactly those
// directions, and each probe asserts the part of the ODBC contract that
// applies to what was found — skipping, with the reason, when the engine
// declares no parameter of the direction it is about. Nothing here names an
// engine or a dialect.
//
// The procedure itself comes from the fixture contract, docs/FIXTURE_CONTRACT.md:
// CRUSHER_PROC takes at least one integer input and at least one integer
// output, and sets the first integer output to twice the first integer input;
// where the engine has character parameters it appends "-out" to the first one.
// CRUSHER_FUNC returns a*10 + b, chosen so that an argument bound into the
// wrong slot gives a wrong answer rather than a coincidentally right one.

// The fixture contract's names and values — docs/FIXTURE_CONTRACT.md.
//
// Names, not shapes: what parameters these have is read from the driver at run
// time. The values are fixed by the contract so a probe can assert an answer
// rather than merely "something was written". 47 for the function is chosen so
// that an argument bound into the wrong slot is visibly wrong: with a=4, b=7
// the answer is 47, and the off-by-one reading (the return-value slot taken as
// `a`) gives 0*10 + 4 = 4.
constexpr const char* kContractProcedure = "CRUSHER_PROC";
constexpr const char* kContractFunction  = "CRUSHER_FUNC";
constexpr SQLINTEGER  kContractIntIn     = 42;
constexpr const char* kContractTextIn    = "hello";
constexpr SQLINTEGER  kContractFuncA     = 4;
constexpr SQLINTEGER  kContractFuncB     = 7;
constexpr SQLINTEGER  kContractFuncResult = kContractFuncA * 10 + kContractFuncB;

// Defined below; declared here because the contract helpers use it.
std::string find_named_procedure(core::OdbcConnection& conn,
                                 const char* name_filter);

// One parameter as the driver describes it.
struct ProcParam {
    SQLSMALLINT ordinal = 0;      // 1-based bind position
    std::string name;
    SQLSMALLINT direction = 0;    // SQL_PARAM_INPUT / _OUTPUT / _INPUT_OUTPUT / SQL_RETURN_VALUE
    SQLSMALLINT sql_type = 0;
    SQLULEN     size = 0;

    bool writes_back() const {
        return direction == SQL_PARAM_OUTPUT ||
               direction == SQL_PARAM_INPUT_OUTPUT ||
               direction == SQL_RETURN_VALUE;
    }
    bool reads_in() const {
        return direction == SQL_PARAM_INPUT ||
               direction == SQL_PARAM_INPUT_OUTPUT;
    }
};

const char* direction_name(SQLSMALLINT d) {
    switch (d) {
        case SQL_PARAM_INPUT:        return "IN";
        case SQL_PARAM_OUTPUT:       return "OUT";
        case SQL_PARAM_INPUT_OUTPUT: return "INOUT";
        case SQL_RETURN_VALUE:       return "RETURN";
        case SQL_RESULT_COL:         return "RESULT_COL";
        default:                     return "UNKNOWN";
    }
}

// Is this SQL type one we can bind as an integer? Anything else this helper
// treats as character, which is what the contract's other parameter is.
bool is_integral_sql_type(SQLSMALLINT t) {
    return t == SQL_INTEGER || t == SQL_SMALLINT || t == SQL_TINYINT ||
           t == SQL_BIGINT  || t == SQL_NUMERIC  || t == SQL_DECIMAL;
}

// SQLProcedureColumns, as a parameter list. SQL_RESULT_COL rows are dropped:
// they describe a result set the procedure returns, not something to bind.
std::vector<ProcParam> describe_procedure(core::OdbcConnection& conn,
                                          const std::string& proc_name) {
    std::vector<ProcParam> params;
    try {
        core::OdbcStatement stmt(conn);
        SQLRETURN rc = SQLProcedureColumns(
            stmt.get_handle(), nullptr, 0, nullptr, 0,
            reinterpret_cast<SQLCHAR*>(const_cast<char*>(proc_name.c_str())), SQL_NTS,
            nullptr, 0);
        if (!SQL_SUCCEEDED(rc)) return params;

        SQLSMALLINT ordinal = 0;
        while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
            ProcParam p;
            core::GuardedBuffer<char> name(128, 0);   // D62
            SQLLEN ind = 0;
            if (SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 4, SQL_C_CHAR,
                                         name.data(), name.declared_bytes(), &ind)) &&
                ind != SQL_NULL_DATA) {
                p.name = core::bounded_string(name.data(), name.declared_elements(), ind).value;
            }
            SQLSMALLINT column_type = 0;
            SQLLEN ct_ind = 0;
            if (!SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 5, SQL_C_SSHORT,
                                          &column_type, 0, &ct_ind))) {
                continue;
            }
            if (column_type == SQL_RESULT_COL) continue;

            SQLSMALLINT data_type = 0;
            SQLLEN dt_ind = 0;
            SQLGetData(stmt.get_handle(), 6, SQL_C_SSHORT, &data_type, 0, &dt_ind);
            SQLULEN column_size = 0;
            SQLLEN cs_ind = 0;
            SQLGetData(stmt.get_handle(), 8, SQL_C_ULONG, &column_size, 0, &cs_ind);

            p.direction = column_type;
            p.sql_type  = data_type;
            p.size      = (cs_ind == SQL_NULL_DATA || column_size == 0) ? 64 : column_size;
            p.ordinal   = ++ordinal;
            params.push_back(std::move(p));
        }
    } catch (const core::OdbcError&) {
        // A driver that cannot describe its own procedures is reported by the
        // caller as an inconclusive skip, not as a direction defect.
        params.clear();
    }
    return params;
}

// Every slot's buffer, kept alive for the duration of the call.
struct CallSlot {
    ProcParam meta;
    SQLINTEGER int_value = 0;
    std::vector<char> text;      // sized from meta.size
    SQLLEN ind = 0;
    bool integral = false;
};

struct ContractCall {
    bool described = false;
    bool prepared  = false;
    bool bound     = false;
    bool executed  = false;
    SQLRETURN exec_rc = SQL_SUCCESS;
    std::string sql;
    std::string error;
    std::string sqlstate;
    std::vector<CallSlot> slots;

    // The values the contract says to send, so assertions can be written
    // against them rather than against a literal repeated in each probe.
    SQLINTEGER int_in = 0;
    std::string text_in;

    const CallSlot* first(SQLSMALLINT direction) const {
        for (const auto& s : slots) {
            if (s.meta.direction == direction) return &s;
        }
        return nullptr;
    }
    std::string shape() const {
        std::string out;
        for (size_t i = 0; i < slots.size(); ++i) {
            if (i) out += ", ";
            out += std::string(direction_name(slots[i].meta.direction)) + " " +
                   slots[i].meta.name;
        }
        return out.empty() ? "(no parameters declared)" : out;
    }
};

// The sentinel an OUT slot carries in: if it survives the execute, the driver
// accepted the binding and never wrote back.
constexpr SQLINTEGER kOutSentinel = static_cast<SQLINTEGER>(0xDEADBEEFu);
constexpr const char* kTextSentinel = "@@sentinel@@";

// Prepare `{CALL proc(?, …)}` (or `{?=CALL proc(?, …)}` when the driver
// declares a return value), bind every declared parameter in its declared
// direction, and execute.
ContractCall call_by_contract(core::OdbcConnection& conn,
                              const std::string& proc_name,
                              SQLINTEGER int_in,
                              const std::string& text_in) {
    ContractCall call;
    call.int_in  = int_in;
    call.text_in = text_in;
    call.slots.reserve(8);

    auto params = describe_procedure(conn, proc_name);
    if (params.empty()) return call;
    call.described = true;

    bool has_return = false;
    for (const auto& p : params) {
        if (p.direction == SQL_RETURN_VALUE) has_return = true;
    }

    // One marker per parameter; the return value's marker is the leading one.
    const size_t arg_count = params.size() - (has_return ? 1 : 0);
    std::string markers;
    for (size_t i = 0; i < arg_count; ++i) markers += (i ? ", ?" : "?");
    call.sql = has_return ? "{? = CALL " + proc_name + "(" + markers + ")}"
                          : "{CALL " + proc_name + "(" + markers + ")}";

    core::OdbcStatement stmt(conn);
    SQLRETURN rc = SQLPrepare(
        stmt.get_handle(),
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(call.sql.c_str())), SQL_NTS);
    if (!SQL_SUCCEEDED(rc)) {
        call.error = "SQLPrepare(" + call.sql + ") returned " + std::to_string(rc);
        call.sqlstate = TestBase::first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "none");
        return call;
    }
    call.prepared = true;

    // Buffers must outlive SQLExecute, so the vector is sized once up front —
    // a reallocation would move every address already handed to the driver.
    call.slots.resize(params.size());
    bool used_int_in = false;
    bool used_text_in = false;
    for (size_t i = 0; i < params.size(); ++i) {
        CallSlot& slot = call.slots[i];
        slot.meta = params[i];
        slot.integral = is_integral_sql_type(slot.meta.sql_type);
        if (slot.integral) {
            slot.int_value = slot.meta.reads_in() ? int_in : kOutSentinel;
            if (slot.meta.reads_in()) used_int_in = true;
            slot.ind = slot.meta.reads_in() ? 0 : sizeof(SQLINTEGER);
        } else {
            const size_t cap = static_cast<size_t>(slot.meta.size) + 1;
            slot.text.assign(cap > 8 ? cap : 8, '\0');
            const std::string initial =
                slot.meta.reads_in() ? text_in : std::string(kTextSentinel);
            const size_t n = std::min(slot.text.size() - 1, initial.size());
            std::memcpy(slot.text.data(), initial.data(), n);
            slot.text[n] = '\0';
            slot.ind = static_cast<SQLLEN>(n);
            if (slot.meta.reads_in()) used_text_in = true;
        }
    }
    (void)used_int_in;
    (void)used_text_in;

    for (auto& slot : call.slots) {
        SQLRETURN brc;
        if (slot.integral) {
            brc = SQLBindParameter(stmt.get_handle(), slot.meta.ordinal,
                                   slot.meta.direction, SQL_C_SLONG,
                                   slot.meta.sql_type ? slot.meta.sql_type : SQL_INTEGER,
                                   10, 0, &slot.int_value,
                                   sizeof(slot.int_value), &slot.ind);
        } else {
            brc = SQLBindParameter(stmt.get_handle(), slot.meta.ordinal,
                                   slot.meta.direction, SQL_C_CHAR,
                                   slot.meta.sql_type ? slot.meta.sql_type : SQL_VARCHAR,
                                   slot.text.size() - 1, 0, slot.text.data(),
                                   static_cast<SQLLEN>(slot.text.size()), &slot.ind);
        }
        if (!SQL_SUCCEEDED(brc)) {
            call.error = "SQLBindParameter(" + std::to_string(slot.meta.ordinal) +
                         ", " + direction_name(slot.meta.direction) + ") returned " +
                         std::to_string(brc);
            call.sqlstate = TestBase::first_sqlstate(SQL_HANDLE_STMT,
                                                     stmt.get_handle(), "none");
            return call;
        }
    }
    call.bound = true;

    call.exec_rc = SQLExecute(stmt.get_handle());
    if (!SQL_SUCCEEDED(call.exec_rc)) {
        call.error = "SQLExecute returned " + std::to_string(call.exec_rc);
        call.sqlstate = TestBase::first_sqlstate(SQL_HANDLE_STMT,
                                                 stmt.get_handle(), "none");
        return call;
    }
    call.executed = true;

    // A16: drain any result sets the procedure produced before reading the
    // output parameters. A driver may legally defer populating bound OUT and
    // INOUT buffers until every result set has been consumed, so a conformant
    // driver whose procedure body returns a result set would otherwise be
    // FAILed for "did not write to the bound buffer". Firebird's selectable
    // procedures make this a live concern, not a hypothetical one.
    int guard = 0;
    while (SQLMoreResults(stmt.get_handle()) == SQL_SUCCESS) {
        if (++guard > 100) break;   // a driver stuck on the same result set
    }
    return call;
}

// The text a slot holds now, read to the length the driver reported — D68.
std::string slot_text(const CallSlot& slot) {
    if (slot.integral) return std::to_string(slot.int_value);
    return core::bounded_string(slot.text.data(), slot.text.size(), slot.ind).value;
}

// Shared preamble: find the contract procedure and describe it, or explain.
bool contract_procedure_ready(core::OdbcConnection& conn, const char* wanted,
                              TestResult& r, std::string& found) {
    found = find_named_procedure(conn, wanted);
    if (found.empty()) {
        r.status = TestStatus::SKIP_INCONCLUSIVE;
        r.actual = std::string("Fixture procedure ") + wanted +
                   " is not visible through SQLProcedures";
        r.suggestion =
            std::string("This probe needs the fixture described in "
                        "docs/FIXTURE_CONTRACT.md. Create ") + wanted +
            " in your database's own dialect and re-run; the probe reads its "
            "parameter list with SQLProcedureColumns and binds whatever the "
            "driver declares, so no particular parameter shape is assumed.";
        return false;
    }
    return true;
}

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
        core::GuardedBuffer<char> buf(128, 0);  // D62
        SQLLEN ind = 0;
        if (SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 3, SQL_C_CHAR,
                                      buf.data(), buf.declared_bytes(), &ind)) &&
            ind != SQL_NULL_DATA) {
            // D68: `ind`, not a terminator. This name is reported back to the
            // user as the procedure that was found.
            return core::bounded_string(buf.data(), buf.declared_elements(), ind).value;
        }
    }
    return {};
}

} // namespace

TestResult EscapeSequenceTests::test_call_escape_in_parameter() {
    return run_test(
        "test_call_escape_in_parameter", "SQLPrepare/SQLBindParameter/SQLExecute",
        "{CALL …(?)} prepares, binds every parameter the driver declares in "
        "the direction it declares, and executes",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 Procedure Call Escape — SQL_PARAM_INPUT direction",
        [&](TestResult& r) {
            std::string proc;
            if (!contract_procedure_ready(conn_, kContractProcedure, r, proc)) return;

            auto call = call_by_contract(conn_, proc, kContractIntIn, kContractTextIn);
            if (!call.described) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLProcedures found " + proc +
                           " but SQLProcedureColumns described no parameters for it";
                r.suggestion =
                    "A procedure the catalog lists must also be describable. An "
                    "application cannot call what it cannot describe.";
                return;
            }
            if (call.first(SQL_PARAM_INPUT) == nullptr &&
                call.first(SQL_PARAM_INPUT_OUTPUT) == nullptr) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "The driver declares no input parameter for " + proc +
                           ": " + call.shape();
                return;
            }
            if (!call.executed) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = call.error + " (SQLSTATE=" + call.sqlstate + ") for `" +
                           call.sql + "`, declared as " + call.shape();
                return;
            }
            r.actual = "Prepared, bound and executed `" + call.sql +
                       "` with parameters declared as " + call.shape();
        });
}

TestResult EscapeSequenceTests::test_call_escape_out_parameter() {
    return run_test(
        "test_call_escape_out_parameter", "SQLBindParameter(SQL_PARAM_OUTPUT)",
        "A parameter the driver declares as OUT is written back after execute",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter — SQL_PARAM_OUTPUT direction",
        [&](TestResult& r) {
            std::string proc;
            if (!contract_procedure_ready(conn_, kContractProcedure, r, proc)) return;

            auto call = call_by_contract(conn_, proc, kContractIntIn, kContractTextIn);
            if (!call.described) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLProcedureColumns described no parameters for " + proc;
                return;
            }
            const CallSlot* out = call.first(SQL_PARAM_OUTPUT);
            if (out == nullptr) {
                // Not every engine has output parameters, and one that has none
                // is not failing this: it is answering a question it was not
                // asked. Say what it does declare so the reader can tell the
                // two apart.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "The driver declares no SQL_PARAM_OUTPUT parameter "
                           "for " + proc + ": " + call.shape();
                r.suggestion =
                    "docs/FIXTURE_CONTRACT.md asks for a procedure with at "
                    "least one output parameter. If this engine has none, that "
                    "is worth knowing and this cell stays unmeasured.";
                return;
            }
            if (!call.executed) {
                r.status = TestStatus::FAIL;
                r.actual = call.error + " (SQLSTATE=" + call.sqlstate + ")";
                return;
            }

            std::ostringstream oss;
            oss << "OUT parameter `" << out->meta.name << "` post-execute: "
                << slot_text(*out) << " (indicator " << out->ind << ")";

            const bool untouched =
                out->integral ? (out->int_value == kOutSentinel)
                              : (slot_text(*out) == kTextSentinel);
            if (untouched) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + " — the sentinel survived";
                r.suggestion =
                    "The driver accepted a SQL_PARAM_OUTPUT binding and never "
                    "wrote to the bound buffer. An application has no way to "
                    "tell that from an output parameter that legitimately "
                    "carries the sentinel's value.";
                return;
            }

            // The contract fixes the value, so the probe can say more than
            // "something was written": the first integer output is twice the
            // first integer input.
            if (out->integral && out->int_value != kContractIntIn * 2) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + "; expected " +
                           std::to_string(kContractIntIn * 2) +
                           " (contract: the first integer output is twice the "
                           "first integer input, and the input was " +
                           std::to_string(kContractIntIn) + ")";
                r.suggestion =
                    "A value was written back, but not the one the procedure "
                    "computed — the arguments or the output slots are being "
                    "matched up wrongly.";
                return;
            }
            r.actual = oss.str();
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
// The contract's CRUSHER_FUNC(a, b) returns a*10 + b, chosen so an argument in
// the wrong slot is visibly wrong rather than coincidentally right: with a=4,
// b=7 the answer is 47, and the off-by-one reading (the return-value slot
// taken as `a`) gives 0*10 + 4 = 4, which the probe names explicitly.
TestResult EscapeSequenceTests::test_function_call_escape_return_value() {
    return run_test(
        "test_function_call_escape_return_value",
        "SQLPrepare/SQLBindParameter/SQLExecute",
        "{?=CALL fn(?, ?)} executes and writes the function's return value "
        "to parameter 1, with the arguments numbered from 2",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 Procedure Call Escape — function return value",
        [&](TestResult& r) {
            std::string fn;
            if (!contract_procedure_ready(conn_, kContractFunction, r, fn)) return;

            // R2: ask before assuming. The `{?=CALL}` form binds the return
            // value as parameter 1, so it needs a driver that describes one.
            // A driver whose procedure model has no return value is not failing
            // this probe - it is answering a question it was not asked - and
            // saying what it *did* declare is what lets a reader tell the two
            // apart.
            const auto fn_params = describe_procedure(conn_, fn);
            bool declares_return = false;
            int declared_args = 0;
            std::string fn_shape;
            for (const auto& fp : fn_params) {
                if (!fn_shape.empty()) fn_shape += ", ";
                fn_shape += std::string(direction_name(fp.direction)) + " " + fp.name;
                if (fp.direction == SQL_RETURN_VALUE) declares_return = true;
                else if (fp.direction == SQL_PARAM_INPUT) ++declared_args;
            }
            if (!declares_return) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "The driver declares no SQL_RETURN_VALUE parameter "
                           "for " + fn + ", so the {?=CALL} form has nothing to "
                           "bind as parameter 1: " +
                           (fn_shape.empty() ? "(no parameters declared)" : fn_shape);
                r.suggestion =
                    "Engines differ on whether a callable returns a value or "
                    "writes an output parameter. Where yours has a return "
                    "value, docs/FIXTURE_CONTRACT.md asks the fixture to use "
                    "one, and this probe then checks the arguments are numbered "
                    "from parameter 2.";
                return;
            }
            if (declared_args != 2) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Fixture function " + fn + " declares " +
                           std::to_string(declared_args) + " input argument(s), "
                           "not the two the contract specifies: " + fn_shape;
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
                const std::string sql_text = "{? = CALL " + fn + "(?, ?)}";
                const char* sql = sql_text.c_str();
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

            // R2: the arguments and the answer come from the fixture
            // contract, not from literals repeated here. Clang's
            // -Wunused-const-variable caught the duplication - the constants
            // were declared for this probe and the probe had its own copies.
            const SQLINTEGER kSentinel = static_cast<SQLINTEGER>(0xDEADBEEFu);
            const SQLINTEGER kA = kContractFuncA, kB = kContractFuncB;
            const SQLINTEGER kExpected = kContractFuncResult;

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
            // C6/E5: `total_passed` was accumulated here and never read. It
            // sat between two counters that *are* read, which is what made it
            // look deliberate - the summary reports claimed and broken and
            // has never mentioned passed. Removed rather than reported,
            // because inventing a use for a number nothing asked for is how
            // dead code becomes permanent.
            int total_claimed = 0;
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
        "A parameter the driver declares as INOUT carries a value in and a "
        "value out of the same buffer",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter — SQL_PARAM_INPUT_OUTPUT direction",
        [&](TestResult& r) {
            std::string proc;
            if (!contract_procedure_ready(conn_, kContractProcedure, r, proc)) return;

            auto call = call_by_contract(conn_, proc, kContractIntIn, kContractTextIn);
            if (!call.described) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLProcedureColumns described no parameters for " + proc;
                return;
            }
            const CallSlot* inout = call.first(SQL_PARAM_INPUT_OUTPUT);
            if (inout == nullptr) {
                // Most engines have no INOUT at all — Firebird's procedure
                // model separates inputs from outputs entirely, and its driver
                // describes a two-in/two-out procedure as (IN, IN, OUT, OUT).
                // That is not a defect and must not be reported as one; it is a
                // capability the report should simply state.
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "The driver declares no SQL_PARAM_INPUT_OUTPUT "
                           "parameter for " + proc + ": " + call.shape();
                r.suggestion =
                    "Many engines separate input and output parameters and have "
                    "no INOUT to describe. Where the engine does have one, "
                    "docs/FIXTURE_CONTRACT.md asks the fixture to use it, and "
                    "this probe then checks the buffer carries a value in both "
                    "directions.";
                return;
            }
            if (!call.executed) {
                r.status = TestStatus::FAIL;
                r.actual = call.error + " (SQLSTATE=" + call.sqlstate + ")";
                return;
            }

            const std::string got = slot_text(*inout);
            std::ostringstream oss;
            oss << "INOUT parameter `" << inout->meta.name
                << "` post-execute: '" << got << "' (indicator " << inout->ind
                << "), sent '" << call.text_in << "'";

            if (got == kTextSentinel) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + " — the sentinel survived, so the input "
                                       "half never reached the procedure";
                return;
            }
            if (got == call.text_in) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + " — unchanged, so nothing was written back";
                r.suggestion =
                    "An INOUT parameter carries a value in *and* out. A buffer "
                    "that comes back exactly as it went in means the driver "
                    "accepted the binding and skipped the write-back, which is "
                    "indistinguishable to the application from a procedure that "
                    "chose not to change it.";
                return;
            }
            r.actual = oss.str();
        });
}

} // namespace odbc_crusher::tests
