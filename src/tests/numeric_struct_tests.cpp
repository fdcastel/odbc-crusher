#include "numeric_struct_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> NumericStructTests::run() {
    return {
        test_numeric_struct_binding(),
        test_numeric_struct_precision_scale(),
        test_numeric_positive_negative(),
        test_numeric_zero_and_extremes(),
        test_numeric_struct_roundtrip_byte_equality()
    };
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
    return run_test(
        "test_numeric_struct_binding", "SQLGetData(SQL_C_NUMERIC)",
        "Can retrieve a numeric value as SQL_C_NUMERIC struct",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, SQL_C_NUMERIC",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT 12345");

            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLFetch failed";
                return;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;

            // Set ARD descriptor precision/scale (required by ODBC spec for SQL_C_NUMERIC)
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);

            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLGetData with SQL_C_NUMERIC not supported";
                r.suggestion = "Driver does not support SQL_C_NUMERIC target type";
            } else {
                double val = numeric_struct_to_double(ns);
                if (std::abs(val - 12345.0) < 0.01) {
                    r.actual = "Retrieved 12345 as SQL_NUMERIC_STRUCT: precision="
                                  + std::to_string(ns.precision)
                                  + ", scale=" + std::to_string(ns.scale)
                                  + ", sign=" + std::to_string(ns.sign);
                } else {
                    r.status = TestStatus::FAIL;
                    r.actual = "Expected 12345, got " + std::to_string(val);
                    r.severity = Severity::WARNING;
                }
            }
        });
}

TestResult NumericStructTests::test_numeric_struct_precision_scale() {
    return run_test(
        "test_numeric_struct_precision_scale", "SQLGetData(SQL_C_NUMERIC)",
        "SQL_NUMERIC_STRUCT precision and scale are correct for decimal values",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, SQL_C_NUMERIC",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            stmt.execute("SELECT 123.45");

            SQLRETURN ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLFetch failed";
                return;
            }

            SQL_NUMERIC_STRUCT ns;
            std::memset(&ns, 0, sizeof(ns));
            SQLLEN ind = 0;
            set_numeric_descriptor(stmt.get_handle(), 1, 18, 2);
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLGetData with SQL_C_NUMERIC not supported for decimal values";
            } else {
                double val = numeric_struct_to_double(ns);
                std::ostringstream oss;
                oss << "Value=" << val << ", precision=" << static_cast<int>(ns.precision)
                    << ", scale=" << static_cast<int>(ns.scale)
                    << ", sign=" << static_cast<int>(ns.sign);
                r.actual = oss.str();

                if (std::abs(val - 123.45) > 0.01) {
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::WARNING;
                    r.suggestion = "SQL_NUMERIC_STRUCT val[] encoding or scale may be incorrect";
                }
            }
        });
}

TestResult NumericStructTests::test_numeric_positive_negative() {
    return run_test(
        "test_numeric_positive_negative", "SQLGetData(SQL_C_NUMERIC)",
        "Positive and negative values round-trip correctly via SQL_NUMERIC_STRUCT",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, SQL_C_NUMERIC",
        [&](TestResult& r) {
            // Test positive value
            {
                core::OdbcStatement stmt(conn_);
                stmt.execute("SELECT 42");
                SQLRETURN ret = SQLFetch(stmt.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed for positive value";
                    return;
                }

                SQL_NUMERIC_STRUCT ns;
                std::memset(&ns, 0, sizeof(ns));
                SQLLEN ind = 0;
                set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
                ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQL_C_NUMERIC not supported";
                    return;
                }

                if (ns.sign != 1) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Positive 42: sign=" + std::to_string(ns.sign) + " (expected 1)";
                    r.severity = Severity::WARNING;
                    return;
                }
            }

            // Test negative value
            {
                core::OdbcStatement stmt(conn_);
                stmt.execute("SELECT -42");
                SQLRETURN ret = SQLFetch(stmt.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed for negative value";
                    return;
                }

                SQL_NUMERIC_STRUCT ns;
                std::memset(&ns, 0, sizeof(ns));
                SQLLEN ind = 0;
                set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
                ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQL_C_NUMERIC not supported for negative values";
                    return;
                }

                double val = numeric_struct_to_double(ns);
                if (ns.sign != 0 || std::abs(val - (-42.0)) > 0.01) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Negative -42: val=" + std::to_string(val)
                                  + ", sign=" + std::to_string(ns.sign);
                    r.severity = Severity::WARNING;
                    return;
                }
            }

            r.actual = "Positive (sign=1) and negative (sign=0) values round-trip correctly";
        });
}

TestResult NumericStructTests::test_numeric_zero_and_extremes() {
    return run_test(
        "test_numeric_zero_and_extremes", "SQLGetData(SQL_C_NUMERIC)",
        "Zero and large values work with SQL_NUMERIC_STRUCT",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8, SQL_C_NUMERIC",
        [&](TestResult& r) {
            // Test zero
            {
                core::OdbcStatement stmt(conn_);
                stmt.execute("SELECT 0");
                SQLRETURN ret = SQLFetch(stmt.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed for zero";
                    return;
                }

                SQL_NUMERIC_STRUCT ns;
                std::memset(&ns, 0, sizeof(ns));
                SQLLEN ind = 0;
                set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
                ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQL_C_NUMERIC not supported";
                    return;
                }

                double val = numeric_struct_to_double(ns);
                if (std::abs(val) > 0.001) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Zero: got " + std::to_string(val);
                    r.severity = Severity::WARNING;
                    return;
                }
            }

            // Test large value
            {
                core::OdbcStatement stmt(conn_);
                stmt.execute("SELECT 999999999");
                SQLRETURN ret = SQLFetch(stmt.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed for large value";
                    return;
                }

                SQL_NUMERIC_STRUCT ns;
                std::memset(&ns, 0, sizeof(ns));
                SQLLEN ind = 0;
                set_numeric_descriptor(stmt.get_handle(), 1, 18, 0);
                ret = SQLGetData(stmt.get_handle(), 1, SQL_C_NUMERIC, &ns, sizeof(ns), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQL_C_NUMERIC not supported for large values";
                    return;
                }

                double val = numeric_struct_to_double(ns);
                if (std::abs(val - 999999999.0) > 1.0) {
                    r.status = TestStatus::FAIL;
                    r.actual = "Large value: expected 999999999, got " + std::to_string(val);
                    r.severity = Severity::WARNING;
                    return;
                }
            }

            r.actual = "Zero and 999999999 round-trip correctly via SQL_NUMERIC_STRUCT";
        });
}

// ── Byte-for-byte round-trip via SQL_NUMERIC_STRUCT ─────────────────────────
//
// Inspired by SQLComponents/TestSQL/TestNumeric.cpp. The unique add over the
// existing SQL_NUMERIC_STRUCT cells is byte-equality on val[]: a driver that
// silently re-encodes the mantissa, mis-orders the bytes, or sets the sign
// byte wrong returns a "close" double but a wrong struct.
//
// We INSERT a literal positive value into a DECIMAL(p, s) column and read it
// back as SQL_C_NUMERIC. The probe is scale-relative: we trust the driver's
// reported `got.scale` and compute the expected mantissa as
// round(literal_value × 10^got.scale). Then memcmp val[] against the
// little-endian encoding of that mantissa. Drivers legitimately differ on
// the *declared* scale they report (some return the column's, some the
// value's); but for any reported scale the mantissa is uniquely determined.
// Strict equality on sign byte (1 = positive, 0 = negative).
// Strict equality on val[] given the recomputed mantissa.
// `precision` is informational only — drivers may report either the column
// declaration or the value's actual precision, both are spec-legal.

namespace {

struct DecimalShape {
    SQLCHAR    precision;      // Column-declared precision, used in DDL only.
    SQLCHAR    scale;          // Column-declared scale, used in DDL + ARD setup.
    const char* literal;       // SQL literal to INSERT, e.g. "12345.67"
    double     literal_value;  // Same value as a double, for mantissa recomputation.
};

// Encode an unsigned 64-bit mantissa little-endian into a SQL_NUMERIC_STRUCT.val[].
void encode_val_le(SQLCHAR (&val)[SQL_MAX_NUMERIC_LEN], uint64_t mantissa) {
    std::memset(val, 0, SQL_MAX_NUMERIC_LEN);
    for (int i = 0; i < SQL_MAX_NUMERIC_LEN && mantissa != 0; ++i) {
        val[i] = static_cast<unsigned char>(mantissa & 0xFFu);
        mantissa >>= 8;
    }
}

std::string val_to_hex(const SQLCHAR* val) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (int i = 0; i < SQL_MAX_NUMERIC_LEN; ++i) {
        os << std::setw(2) << static_cast<int>(val[i]);
        if (i + 1 < SQL_MAX_NUMERIC_LEN) os << ' ';
    }
    return os.str();
}

} // namespace

TestResult NumericStructTests::test_numeric_struct_roundtrip_byte_equality() {
    return run_test(
        "test_numeric_struct_roundtrip_byte_equality", "SQLGetData(SQL_C_NUMERIC)",
        "INSERT decimal literal, SELECT back as SQL_C_NUMERIC, val[] bytes match",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC: val[] is little-endian mantissa, sign 1=positive 0=negative",
        [&](TestResult& r) {
            // Three shapes — reports the first one that fails. Mantissas chosen
            // to fit in 32 bits so doubles can represent them exactly (avoids
            // false positives on drivers that store DECIMAL via double internally).
            // Literal values chosen so round(value * 10^scale) fits in 32 bits
            // for every reasonable scale a driver might report — keeps the
            // mantissa exactly representable in IEEE 754 double during the
            // recomputation, no false positives from FP rounding.
            const DecimalShape shapes[] = {
                { 10,  2, "12345.67",    12345.67    },
                { 19,  0, "1234567890",  1234567890.0 },
                { 38, 10, "123.4567890", 123.4567890 },
            };

            std::ostringstream summary;
            int variant_idx = 0;
            for (const auto& shape : shapes) {
                ++variant_idx;
                std::ostringstream val_ddl;
                val_ddl << "DECIMAL(" << static_cast<int>(shape.precision)
                        << ", " << static_cast<int>(shape.scale) << ")";
                const std::string table_name = "ODBC_TEST_NUMERIC_BYTES";

                RoundTripTableGuard tbl(conn_, table_name, val_ddl.str());
                if (!tbl.ok()) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not create round-trip table for variant "
                             + std::to_string(variant_idx) + " (" + val_ddl.str()
                             + "): " + tbl.last_error();
                    return;
                }

                // INSERT literal value.
                try {
                    core::OdbcStatement ins(conn_);
                    ins.execute("INSERT INTO " + table_name +
                                " (ID, VAL) VALUES (1, " + shape.literal + ")");
                } catch (const core::OdbcError& e) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "INSERT failed for variant " + std::to_string(variant_idx)
                             + " (" + val_ddl.str() + "): " + e.what();
                    return;
                }

                // SELECT back as SQL_C_NUMERIC.
                core::OdbcStatement sel(conn_);
                sel.execute("SELECT VAL FROM " + table_name);
                SQLRETURN ret = SQLFetch(sel.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SELECT/SQLFetch failed for variant "
                             + std::to_string(variant_idx);
                    return;
                }

                SQL_NUMERIC_STRUCT got;
                std::memset(&got, 0, sizeof(got));
                SQLLEN ind = 0;
                if (!set_numeric_descriptor(sel.get_handle(), 1,
                                            shape.precision, shape.scale)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "Driver rejects ARD descriptor configuration "
                               "for SQL_C_NUMERIC";
                    return;
                }
                ret = SQLGetData(sel.get_handle(), 1, SQL_C_NUMERIC,
                                 &got, sizeof(got), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLGetData(SQL_C_NUMERIC) returned " + std::to_string(ret)
                             + " for variant " + std::to_string(variant_idx);
                    return;
                }

                // Recompute expected mantissa using the scale the driver reports —
                // the spec lets a driver pick a different scale than the column
                // declares, so trust got.scale and verify val[] matches it.
                const uint64_t expected_mantissa = static_cast<uint64_t>(
                    std::llround(shape.literal_value *
                                 std::pow(10.0, static_cast<int>(got.scale))));
                SQL_NUMERIC_STRUCT exp;
                std::memset(&exp, 0, sizeof(exp));
                exp.sign      = 1;  // positive literal
                exp.scale     = got.scale;
                exp.precision = got.precision;
                encode_val_le(exp.val, expected_mantissa);

                const bool sign_ok = (got.sign == exp.sign);
                const bool val_ok  = (std::memcmp(got.val, exp.val,
                                                  SQL_MAX_NUMERIC_LEN) == 0);
                if (!sign_ok || !val_ok) {
                    std::ostringstream diff;
                    diff << "variant " << variant_idx << " "
                         << "DECIMAL(" << static_cast<int>(shape.precision)
                         << "," << static_cast<int>(shape.scale)
                         << ") literal=" << shape.literal
                         << ": got sign=" << static_cast<int>(got.sign)
                         << " scale=" << static_cast<int>(got.scale)
                         << " val=[" << val_to_hex(got.val) << "]"
                         << "; expected (for got.scale=" << static_cast<int>(got.scale)
                         << ") sign=" << static_cast<int>(exp.sign)
                         << " val=[" << val_to_hex(exp.val) << "]"
                         << " (mantissa=" << expected_mantissa << ")";
                    r.status = TestStatus::FAIL;
                    r.actual = diff.str();
                    r.suggestion = "Verify SQL_NUMERIC_STRUCT.val is little-endian "
                                   "mantissa = round(value * 10^got.scale), and "
                                   "sign=1 positive / sign=0 negative.";
                    return;
                }

                if (variant_idx > 1) summary << "; ";
                summary << "DECIMAL(" << static_cast<int>(shape.precision)
                        << "," << static_cast<int>(shape.scale)
                        << ") ok at scale=" << static_cast<int>(got.scale);
            }

            r.actual = "All three (precision, scale) variants byte-match: "
                     + summary.str();
        });
}

} // namespace odbc_crusher::tests
