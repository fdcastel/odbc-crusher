#include "numeric_struct_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstring>
#include <cmath>
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> NumericStructTests::run() {
    std::vector<TestResult> results;

    results.push_back(test_numeric_struct_binding());
    results.push_back(test_numeric_struct_precision_scale());
    results.push_back(test_numeric_positive_negative());
    results.push_back(test_numeric_zero_and_extremes());

    return results;
}

// Helper: convert SQL_NUMERIC_STRUCT val[] to double
static double numeric_struct_to_double(const SQL_NUMERIC_STRUCT& ns) {
    unsigned long long int_val = 0;
    for (int i = SQL_MAX_NUMERIC_LEN - 1; i >= 0; --i) {
        int_val = (int_val << 8) | ns.val[i];
    }
    double result = static_cast<double>(int_val) / std::pow(10.0, ns.scale);
    if (ns.sign == 0) result = -result;
    return result;
}

// Helper: set ARD descriptor precision/scale for SQL_C_NUMERIC retrieval
// The ODBC spec requires this before SQLGetData with SQL_C_NUMERIC
static bool set_numeric_descriptor(SQLHSTMT hstmt, SQLSMALLINT col,
                                    SQLSMALLINT precision, SQLSMALLINT scale) {
    SQLHDESC ard = SQL_NULL_HDESC;
    SQLRETURN ret = SQLGetStmtAttr(hstmt, SQL_ATTR_APP_ROW_DESC, &ard, 0, nullptr);
    if (!SQL_SUCCEEDED(ret) || ard == SQL_NULL_HDESC) return false;

    ret = SQLSetDescField(ard, col, SQL_DESC_TYPE, reinterpret_cast<SQLPOINTER>(SQL_C_NUMERIC), 0);
    if (!SQL_SUCCEEDED(ret)) return false;

    ret = SQLSetDescField(ard, col, SQL_DESC_PRECISION, reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(precision)), 0);
    if (!SQL_SUCCEEDED(ret)) return false;

    ret = SQLSetDescField(ard, col, SQL_DESC_SCALE, reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(scale)), 0);
    if (!SQL_SUCCEEDED(ret)) return false;

    return true;
}

TestResult NumericStructTests::test_numeric_struct_binding() {
    TestResult result = make_result(
        "test_numeric_struct_binding",
        "SQLGetData(SQL_C_NUMERIC)",
        TestStatus::PASS,
        "Can retrieve a numeric value as SQL_C_NUMERIC struct",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        core::OdbcStatement stmt(conn_);
        stmt.execute("SELECT 12345");

        SQLRETURN ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLFetch failed";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
        }

        SQL_NUMERIC_STRUCT ns;
        std::memset(&ns, 0, sizeof(ns));
        SQLLEN ind = 0;

        // Set ARD descriptor precision/scale (required by ODBC spec for SQL_C_NUMERIC)
        set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);

        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);

        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLGetData with SQL_C_NUMERIC not supported";
            result.suggestion = "Driver does not support SQL_C_NUMERIC target type";
        } else {
            double val = numeric_struct_to_double(ns);
            if (std::abs(val - 12345.0) < 0.01) {
                result.actual = "Retrieved 12345 as SQL_NUMERIC_STRUCT: precision="
                              + std::to_string(ns.precision)
                              + ", scale=" + std::to_string(ns.scale)
                              + ", sign=" + std::to_string(ns.sign);
            } else {
                result.status = TestStatus::FAIL;
                result.actual = "Expected 12345, got " + std::to_string(val);
                result.severity = Severity::WARNING;
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

TestResult NumericStructTests::test_numeric_struct_precision_scale() {
    TestResult result = make_result(
        "test_numeric_struct_precision_scale",
        "SQLGetData(SQL_C_NUMERIC)",
        TestStatus::PASS,
        "SQL_NUMERIC_STRUCT precision and scale are correct for decimal values",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        core::OdbcStatement stmt(conn_);
        stmt.execute("SELECT 123.45");

        SQLRETURN ret = SQLFetch(stmt.get_handle());
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::FAIL;
            result.actual = "SQLFetch failed";
            auto end = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            return result;
        }

        SQL_NUMERIC_STRUCT ns;
        std::memset(&ns, 0, sizeof(ns));
        SQLLEN ind = 0;
        set_numeric_descriptor(stmt.get_handle(), 1, 18, 2);
        ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);

        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.actual = "SQLGetData with SQL_C_NUMERIC not supported for decimal values";
        } else {
            double val = numeric_struct_to_double(ns);
            std::ostringstream oss;
            oss << "Value=" << val << ", precision=" << (int)ns.precision
                << ", scale=" << (int)ns.scale << ", sign=" << (int)ns.sign;
            result.actual = oss.str();

            if (std::abs(val - 123.45) > 0.01) {
                result.status = TestStatus::FAIL;
                result.severity = Severity::WARNING;
                result.suggestion = "SQL_NUMERIC_STRUCT val[] encoding or scale may be incorrect";
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

TestResult NumericStructTests::test_numeric_positive_negative() {
    TestResult result = make_result(
        "test_numeric_positive_negative",
        "SQLGetData(SQL_C_NUMERIC)",
        TestStatus::PASS,
        "Positive and negative values round-trip correctly via SQL_NUMERIC_STRUCT",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        // Test positive value
        {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT 42");
            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::FAIL;
                result.actual = "SQLFetch failed for positive value";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "SQL_C_NUMERIC not supported";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            if (ns.sign != 1) {
                result.status = TestStatus::FAIL;
                result.actual = "Positive 42: sign=" + std::to_string(ns.sign) + " (expected 1)";
                result.severity = Severity::WARNING;
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }
        }

        // Test negative value
        {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT -42");
            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::FAIL;
                result.actual = "SQLFetch failed for negative value";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "SQL_C_NUMERIC not supported for negative values";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            double val = numeric_struct_to_double(ns);
            if (ns.sign != 0 || std::abs(val - (-42.0)) > 0.01) {
                result.status = TestStatus::FAIL;
                result.actual = "Negative -42: val=" + std::to_string(val)
                              + ", sign=" + std::to_string(ns.sign);
                result.severity = Severity::WARNING;
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }
        }

        result.actual = "Positive (sign=1) and negative (sign=0) values round-trip correctly";

        auto end = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }

    return result;
}

TestResult NumericStructTests::test_numeric_zero_and_extremes() {
    TestResult result = make_result(
        "test_numeric_zero_and_extremes",
        "SQLGetData(SQL_C_NUMERIC)",
        TestStatus::PASS,
        "Zero and large values work with SQL_NUMERIC_STRUCT",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC"
    );

    try {
        auto start = std::chrono::high_resolution_clock::now();

        // Test zero
        {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT 0");
            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::FAIL;
                result.actual = "SQLFetch failed for zero";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "SQL_C_NUMERIC not supported";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            double val = numeric_struct_to_double(ns);
            if (std::abs(val) > 0.001) {
                result.status = TestStatus::FAIL;
                result.actual = "Zero: got " + std::to_string(val);
                result.severity = Severity::WARNING;
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }
        }

        // Test large value
        {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT 999999999");
            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::FAIL;
                result.actual = "SQLFetch failed for large value";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
            if (!SQL_SUCCEEDED(ret)) {
                result.status = TestStatus::SKIP_UNSUPPORTED;
                result.actual = "SQL_C_NUMERIC not supported for large values";
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }

            double val = numeric_struct_to_double(ns);
            if (std::abs(val - 999999999.0) > 1.0) {
                result.status = TestStatus::FAIL;
                result.actual = "Large value: expected 999999999, got " + std::to_string(val);
                result.severity = Severity::WARNING;
                auto end = std::chrono::high_resolution_clock::now();
                result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                return result;
            }
        }

        result.actual = "Zero and 999999999 round-trip correctly via SQL_NUMERIC_STRUCT";

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
