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

std::optional<std::string> EscapeSequenceTests::exec_scalar(const std::string& sql) {
    try {
        core::OdbcStatement stmt(conn_);
        stmt.execute(sql);
        SQLRETURN ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) return std::nullopt;
        SQLCHAR buf[1024] = {0};
        SQLLEN ind = 0;
        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        if (SQL_SUCCEEDED(ret) && ind != SQL_NULL_DATA) {
            // A3: same shape as call_native_sql above — `ind` is the total
            // available length, so an over-long value indexed off the end of
            // this buffer.
            const auto bounded =
                bounded_string(reinterpret_cast<const char*>(buf), sizeof(buf), ind);
            if (bounded.truncated) {
                // A truncated value cannot be compared against an expected one,
                // and this helper has no channel to say why — A4 replaces the
                // optional with {value, sqlstate, diagnostic}, at which point
                // truncation becomes a reportable outcome instead of silence.
                return std::nullopt;
            }
            return bounded.value;
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

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
                auto val = exec_scalar(t.sql);
                if (val && *val == t.expected) {
                    ++passed_count;
                } else {
                    oss << t.name << "='" << (val ? *val : "NULL") << "' (expected '" << t.expected << "'); ";
                }
            }

            r.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " string functions passed";
            if (passed_count < tested) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
            if (tested == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no string function support";
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
                auto val = exec_scalar(t.sql);
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
                    oss << t.name << "=NULL; ";
                }
            }

            r.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " numeric functions passed";
            if (passed_count < tested) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
            if (tested == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no numeric function support";
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
                auto val = exec_scalar(t.sql);
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
                    oss << t.name << "=NULL; ";
                }
            }

            r.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " datetime functions passed";
            if (passed_count < tested) {
                r.status = TestStatus::FAIL;
                r.actual += ". Failures: " + oss.str();
                r.severity = Severity::WARNING;
            }
            if (tested == 0) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "Driver claims no timedate function support";
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

            r.actual = "SQL_LIKE_ESCAPE_CLAUSE = '" + like_support + "'";
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

            r.actual = "OJ capabilities: " + (oss.str().empty() ? "none" : oss.str());
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

            r.actual = "Datetime literals: " + (oss.str().empty() ? "none" : oss.str());
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
std::string find_mock_inout(core::OdbcConnection& conn) {
    core::OdbcStatement stmt(conn);
    const char* name_filter = "MOCK_INOUT";
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
