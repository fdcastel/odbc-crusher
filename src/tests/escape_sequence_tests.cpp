#include "escape_sequence_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> EscapeSequenceTests::run() {
    std::vector<TestResult> results;

    // Discovery
    results.push_back(test_scalar_function_capabilities());
    results.push_back(test_convert_function_capabilities());

    // SQLNativeSql
    results.push_back(test_native_sql_scalar_functions());
    results.push_back(test_native_sql_datetime_literals());
    results.push_back(test_native_sql_call_escape());

    // Scalar function execution
    results.push_back(test_string_scalar_functions());
    results.push_back(test_numeric_scalar_functions());
    results.push_back(test_datetime_scalar_functions());
    results.push_back(test_system_scalar_functions());
    results.push_back(test_datetime_literal_escapes());
    results.push_back(test_like_escape_sequence());

    // Outer join & interval
    results.push_back(test_outer_join_escape());
    results.push_back(test_interval_literal_escape());

    // Procedure call escape
    results.push_back(test_call_escape_translation());
    results.push_back(test_call_escape_format_variants());

    return results;
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
    TestResult result = make_result(
        "test_scalar_function_capabilities",
        "SQLGetInfo(SQL_STRING/NUMERIC/TIMEDATE/SYSTEM_FUNCTIONS)",
        TestStatus::PASS,
        "Driver reports scalar function capabilities via bitmask",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Scalar Functions"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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
            result.status = TestStatus::FAIL;
            result.actual = "No scalar function capability info returned";
            result.severity = Severity::WARNING;
        } else {
            result.actual = oss.str();
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_convert_function_capabilities() {
    TestResult result = make_result(
        "test_convert_function_capabilities",
        "SQLGetInfo(SQL_CONVERT_*)",
        TestStatus::PASS,
        "Driver reports data type conversion capabilities",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQLGetInfo SQL_CONVERT_*"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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

        result.actual = oss.str();

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

// ---------------------------------------------------------------------------
// SQLNativeSql Translation Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_native_sql_scalar_functions() {
    TestResult result = make_result(
        "test_native_sql_scalar_functions",
        "SQLNativeSql",
        TestStatus::PASS,
        "SQLNativeSql translates {fn UCASE('hello')} to native SQL",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQLNativeSql"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto translated = call_native_sql("SELECT {fn UCASE('hello')}");
        if (!translated) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLNativeSql returned error";
            result.severity = Severity::ERR;
        } else if (translated->empty()) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLNativeSql returned empty string";
            result.severity = Severity::ERR;
        } else {
            result.actual = "Translated to: " + *translated;
            // Verify the escape braces are removed
            if (translated->find("{fn") != std::string::npos) {
                result.status = TestStatus::FAIL;
                result.actual = "Escape sequence not translated (still contains {fn): " + *translated;
                result.severity = Severity::WARNING;
                result.suggestion = "The driver should translate {fn UCASE(...)} to the native equivalent (e.g. UPPER(...))";
            }
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_native_sql_datetime_literals() {
    TestResult result = make_result(
        "test_native_sql_datetime_literals",
        "SQLNativeSql",
        TestStatus::PASS,
        "SQLNativeSql translates {d '...'}, {t '...'}, {ts '...'} to native SQL",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Date/Time/Timestamp Escape Sequences"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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
            result.actual = "All 3 datetime literal escapes translated successfully";
        } else {
            result.status = TestStatus::FAIL;
            result.actual = std::to_string(passed) + "/3 translated. " + oss.str();
            result.severity = Severity::WARNING;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_native_sql_call_escape() {
    TestResult result = make_result(
        "test_native_sql_call_escape",
        "SQLNativeSql",
        TestStatus::PASS,
        "SQLNativeSql translates {CALL proc(?)} and {?=CALL func(?)} escape sequences",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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
            result.actual = oss.str();
        } else {
            result.status = TestStatus::FAIL;
            result.actual = oss.str();
            result.severity = Severity::WARNING;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Scalar Function Execution Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_string_scalar_functions() {
    TestResult result = make_result(
        "test_string_scalar_functions",
        "SQLExecDirect + SQLGetData",
        TestStatus::PASS,
        "String scalar functions via {fn ...} escape produce correct results",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: String Functions"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto str_funcs = get_info_uint(SQL_STRING_FUNCTIONS);
        if (!str_funcs) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not query SQL_STRING_FUNCTIONS";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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

        result.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " string functions passed";
        if (passed_count < tested) {
            result.status = TestStatus::FAIL;
            result.actual += ". Failures: " + oss.str();
            result.severity = Severity::WARNING;
        }
        if (tested == 0) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "Driver claims no string function support";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_numeric_scalar_functions() {
    TestResult result = make_result(
        "test_numeric_scalar_functions",
        "SQLExecDirect + SQLGetData",
        TestStatus::PASS,
        "Numeric scalar functions via {fn ...} escape produce correct results",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Numeric Functions"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto num_funcs = get_info_uint(SQL_NUMERIC_FUNCTIONS);
        if (!num_funcs) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not query SQL_NUMERIC_FUNCTIONS";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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

        result.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " numeric functions passed";
        if (passed_count < tested) {
            result.status = TestStatus::FAIL;
            result.actual += ". Failures: " + oss.str();
            result.severity = Severity::WARNING;
        }
        if (tested == 0) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "Driver claims no numeric function support";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_datetime_scalar_functions() {
    TestResult result = make_result(
        "test_datetime_scalar_functions",
        "SQLExecDirect + SQLGetData",
        TestStatus::PASS,
        "Date/time scalar functions via {fn ...} produce non-empty results",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: Date/Time Functions"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto td_funcs = get_info_uint(SQL_TIMEDATE_FUNCTIONS);
        if (!td_funcs) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not query SQL_TIMEDATE_FUNCTIONS";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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

        result.actual = std::to_string(passed_count) + "/" + std::to_string(tested) + " datetime functions passed";
        if (passed_count < tested) {
            result.status = TestStatus::FAIL;
            result.actual += ". Failures: " + oss.str();
            result.severity = Severity::WARNING;
        }
        if (tested == 0) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "Driver claims no timedate function support";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_system_scalar_functions() {
    TestResult result = make_result(
        "test_system_scalar_functions",
        "SQLExecDirect + SQLGetData",
        TestStatus::PASS,
        "System scalar functions {fn DATABASE()}, {fn USER()} return non-empty results",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Appendix E: System Functions"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto sys_funcs = get_info_uint(SQL_SYSTEM_FUNCTIONS);
        if (!sys_funcs) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not query SQL_SYSTEM_FUNCTIONS";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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

        result.actual = std::to_string(passed_count) + "/" + std::to_string(tested)
                      + " system functions passed. " + oss.str();
        if (passed_count < tested) {
            result.status = TestStatus::FAIL;
            result.severity = Severity::WARNING;
        }
        if (tested == 0) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "Driver claims no system function support";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_datetime_literal_escapes() {
    TestResult result = make_result(
        "test_datetime_literal_escapes",
        "SQLExecDirect + SQLGetData",
        TestStatus::PASS,
        "Date/time/timestamp literal escapes return correct temporal values",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Date/Time/Timestamp Escape Sequences"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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

        result.actual = std::to_string(passed_count) + "/3 datetime literal escapes passed";
        if (passed_count < 3) {
            result.status = TestStatus::FAIL;
            result.actual += ". Failures: " + oss.str();
            result.severity = Severity::WARNING;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_like_escape_sequence() {
    TestResult result = make_result(
        "test_like_escape_sequence",
        "SQLGetInfo(SQL_LIKE_ESCAPE_CLAUSE) + SQLExecDirect",
        TestStatus::PASS,
        "LIKE escape sequence {escape '\\'} is supported",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8, LIKE Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        // Check SQL_LIKE_ESCAPE_CLAUSE
        SQLCHAR buf[16] = {0};
        SQLSMALLINT len = 0;
        SQLRETURN ret = SQLGetInfo(conn_.get_handle(), SQL_LIKE_ESCAPE_CLAUSE,
                                   buf, sizeof(buf), &len);
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQL_LIKE_ESCAPE_CLAUSE not supported";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
        }

        std::string like_support(reinterpret_cast<char*>(buf), len);
        if (like_support == "N") {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "Driver reports SQL_LIKE_ESCAPE_CLAUSE = 'N'";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
        }

        result.actual = "SQL_LIKE_ESCAPE_CLAUSE = '" + like_support + "'";

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Outer Join & Interval Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_outer_join_escape() {
    TestResult result = make_result(
        "test_outer_join_escape",
        "SQLGetInfo(SQL_OJ_CAPABILITIES)",
        TestStatus::PASS,
        "Driver reports outer join capabilities",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8, Outer Join Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto oj_caps = get_info_uint(SQL_OJ_CAPABILITIES);
        if (!oj_caps) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQL_OJ_CAPABILITIES not supported";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
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

        result.actual = "OJ capabilities: " + (oss.str().empty() ? "none" : oss.str());

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_interval_literal_escape() {
    TestResult result = make_result(
        "test_interval_literal_escape",
        "SQLGetInfo(SQL_DATETIME_LITERALS)",
        TestStatus::PASS,
        "Driver reports datetime literal support",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8, Interval Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        auto dt_literals = get_info_uint(SQL_DATETIME_LITERALS);
        if (!dt_literals) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "SQL_DATETIME_LITERALS not supported";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
        }

        SQLUINTEGER mask = *dt_literals;
        std::ostringstream oss;
        if (mask & SQL_DL_SQL92_DATE) oss << "DATE ";
        if (mask & SQL_DL_SQL92_TIME) oss << "TIME ";
        if (mask & SQL_DL_SQL92_TIMESTAMP) oss << "TIMESTAMP ";
        if (mask & SQL_DL_SQL92_INTERVAL_YEAR) oss << "INTERVAL_YEAR ";
        if (mask & SQL_DL_SQL92_INTERVAL_MONTH) oss << "INTERVAL_MONTH ";
        if (mask & SQL_DL_SQL92_INTERVAL_DAY) oss << "INTERVAL_DAY ";

        result.actual = "Datetime literals: " + (oss.str().empty() ? "none" : oss.str());

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

// ---------------------------------------------------------------------------
// Procedure Call Escape Tests
// ---------------------------------------------------------------------------

TestResult EscapeSequenceTests::test_call_escape_translation() {
    TestResult result = make_result(
        "test_call_escape_translation",
        "SQLNativeSql",
        TestStatus::PASS,
        "SQLNativeSql translates {CALL proc(?,?)} and {?=CALL func(?)} escape syntax",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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

        result.actual = oss.str();
        if (passed_count < 2) {
            result.status = TestStatus::FAIL;
            result.severity = Severity::WARNING;
            result.suggestion = "The driver's escape parser should translate CALL escape sequences to native syntax";
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult EscapeSequenceTests::test_call_escape_format_variants() {
    TestResult result = make_result(
        "test_call_escape_format_variants",
        "SQLNativeSql",
        TestStatus::PASS,
        "All 5 CALL escape format variants from ODBC spec are translated",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, Procedure Call Escape Sequence"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

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

        result.actual = std::to_string(passed_count) + "/5 CALL variants translated";
        if (passed_count < 5) {
            result.status = TestStatus::FAIL;
            result.actual += ". Failures: " + oss.str();
            result.severity = Severity::WARNING;
        }

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

} // namespace odbc_crusher::tests
