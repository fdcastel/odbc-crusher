#include "escape_sequence_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

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
        test_call_escape_format_variants()
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
        return std::string(reinterpret_cast<char*>(out), out_len);
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
            return std::string(reinterpret_cast<char*>(buf), ind);
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
                oss << "; CONVERT_FUNCTIONS=0x" << std::hex << *conv_funcs;
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
            auto translated = call_native_sql("SELECT {fn UCASE('hello')}");
            if (!translated) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned error";
                r.severity = Severity::ERR;
            } else if (translated->empty()) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLNativeSql returned empty string";
                r.severity = Severity::ERR;
            } else {
                r.actual = "Translated to: " + *translated;
                // Verify the escape braces are removed
                if (translated->find("{fn") != std::string::npos) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Escape sequence not translated (still contains {fn): " + *translated;
                    r.severity = Severity::WARNING;
                    r.suggestion = "The driver should translate {fn UCASE(...)} to the native equivalent (e.g. UPPER(...))";
                }
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

} // namespace odbc_crusher::tests
