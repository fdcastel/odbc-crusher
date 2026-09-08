#include "numeric_struct_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace odbc_crusher::tests {

namespace {

// First SQLSTATE on a handle, or "" when the driver posted nothing.
// Local copy; **C5** (Phase 3) replaces the several variants of this shape
// across the tree with one helper.
// C5/E5: the anonymous-namespace `first_sqlstate` that stood here is gone.
// Nothing called it - the call sites below resolve to TestBase's static
// member of the same name, because unqualified lookup inside a member
// function finds the member before the namespace. It survived C5's sweep and
// -Werror is what finally pointed at it.

}  // namespace

std::vector<TestResult> NumericStructTests::run() {
    return {
        test_numeric_struct_binding(),
        test_numeric_struct_precision_scale(),
        test_numeric_positive_negative(),
        test_numeric_zero_and_extremes(),
        test_numeric_struct_roundtrip_byte_equality(),
        test_decimal_sum_loop_precision()
    };
}

// Helper: convert SQL_NUMERIC_STRUCT val[] to double.
//
// A26: this used to shift all 16 val[] bytes into an `unsigned long long`,
// where every `<< 8` past the eighth byte silently discards the top one — so
// bytes 8-15 were dropped, in a file whose probes are named for extremes.
// Accumulating into the double this function already returns keeps every byte;
// the precision limit is then double's own, which is inherent to the return
// type rather than an accident of the loop.
static double numeric_struct_to_double(const SQL_NUMERIC_STRUCT& ns) {
    double int_val = 0.0;
    for (int i = SQL_MAX_NUMERIC_LEN - 1; i >= 0; --i) {
        int_val = int_val * 256.0 + static_cast<double>(ns.val[i]);
    }
    double result = int_val / std::pow(10.0, ns.scale);
    if (ns.sign == 0) result = -result;
    return result;
}

// True when a SQL_NUMERIC_STRUCT carries more magnitude than a uint64 mantissa
// can hold — i.e. any of the high 8 val[] bytes is set. A26: probes that
// reconstruct an exact integer mantissa must check this rather than wrap.
static bool numeric_exceeds_64_bits(const SQL_NUMERIC_STRUCT& ns) {
    for (int i = 8; i < SQL_MAX_NUMERIC_LEN; ++i) {
        if (ns.val[i] != 0) return true;
    }
    return false;
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
            execute_literal_select(stmt, "SELECT 12345");

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
                // B3: SQL_C_NUMERIC is a *required Core* target type, so a
                // failure here is only a SKIP if the driver actually says
                // "not implemented". classify_failure reads the SQLSTATE and
                // FAILs anything else, instead of excusing every failure.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetData(SQL_C_NUMERIC)");
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
            execute_literal_select(stmt, "SELECT 123.45");

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
                // B3
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLGetData(SQL_C_NUMERIC) for a decimal");
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
                execute_literal_select(stmt, "SELECT 42");
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
                    // B3: SQL_C_NUMERIC is a *required Core* target type, so a
                    // failure here is only a SKIP if the driver actually says
                    // "not implemented". classify_failure reads the SQLSTATE and
                    // FAILs anything else, instead of excusing every failure.
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC)");
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
                execute_literal_select(stmt, "SELECT -42");
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
                    // B3
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC) for a negative");
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
                execute_literal_select(stmt, "SELECT 0");
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
                    // B3: SQL_C_NUMERIC is a *required Core* target type, so a
                    // failure here is only a SKIP if the driver actually says
                    // "not implemented". classify_failure reads the SQLSTATE and
                    // FAILs anything else, instead of excusing every failure.
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC)");
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
                execute_literal_select(stmt, "SELECT 999999999");
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
                    // B3
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC) for a large value");
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
// reported `got.scale` and rescale the decimal literal to that scale,
// truncating rather than rounding (A13). Then memcmp val[] against the
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
    // A13 removed a `double literal_value` field here: the expected mantissa
    // is rescaled from `literal` as text now, so no double is involved.
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
    // A13. The expected mantissa used to be std::llround(literal * 10^scale),
    // which is wrong twice over.
    //
    // First, the ODBC conversion rule for a narrowing scale is *truncate*, not
    // round: a driver may legally report a smaller got.scale than the column
    // declares (returning SQL_SUCCESS_WITH_INFO + 01S07, fractional
    // truncation), and for 12345.67 at scale 0 a compliant driver returns
    // 12345 while the probe demanded 12346.
    //
    // Second, going through a double at all is unnecessary. The literal is
    // already an exact decimal string, so rescaling it as text is exact for
    // every precision the spec allows, with no floating point in the path.
    struct Rescaled {
        uint64_t mantissa = 0;
        bool     dropped_nonzero = false;   // digits lost that were not zeros
    };

    const auto mantissa_for_scale = [](const std::string& literal,
                                       int scale) -> Rescaled {
        std::string digits;
        int frac_digits = 0;
        bool seen_point = false;
        for (char c : literal) {
            if (c == '.') { seen_point = true; continue; }
            if (c < '0' || c > '9') continue;      // sign or spacing
            digits.push_back(c);
            if (seen_point) ++frac_digits;
        }

        Rescaled out;
        if (scale >= frac_digits) {
            digits.append(static_cast<size_t>(scale - frac_digits), '0');
        } else {
            // Truncate, per the spec. Never round.
            const size_t drop = static_cast<size_t>(frac_digits - scale);
            const std::string lost = digits.substr(digits.size() - drop);
            out.dropped_nonzero =
                lost.find_first_not_of('0') != std::string::npos;
            digits.erase(digits.size() - drop);
        }
        for (char c : digits) {
            out.mantissa = out.mantissa * 10u + static_cast<uint64_t>(c - '0');
        }
        return out;
    };

    return run_test(
        "test_numeric_struct_roundtrip_byte_equality", "SQLGetData(SQL_C_NUMERIC)",
        "INSERT decimal literal, SELECT back as SQL_C_NUMERIC, val[] bytes match",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8, SQL_C_NUMERIC: val[] is little-endian mantissa, sign 1=positive 0=negative",
        [&](TestResult& r) {
            // Three shapes — reports the first one that fails.
            //
            // A13: the comment here used to claim these mantissas "fit in 32
            // bits so doubles can represent them exactly". That was false —
            // variant 3 at scale 10 is 1,234,567,890,000, which needs 41 bits
            // — and it no longer matters either way, because the expected
            // mantissa is now rescaled from the literal text rather than
            // computed through a double. Mantissas must still fit in the
            // uint64_t that encode_val_le() takes, which all three do with
            // room to spare.
            // A24: the last two used to be DECIMAL(19,0) and DECIMAL(38,10).
            // Firebird 3 caps DECIMAL precision at 18, so both CREATE TABLEs
            // failed there and the probe reported SKIP - on the driver family
            // this tool exists for. 18 is the largest precision every target
            // engine accepts, and it still exercises what the probe is about:
            // a mantissa wider than 32 bits, and a non-zero scale on a
            // precision beyond what a double can hold exactly.
            const DecimalShape shapes[] = {
                { 10,  2, "12345.67"    },
                { 18,  0, "1234567890"  },
                { 18, 10, "123.4567890" },
            };

            // B6: per-variant outcomes. The loop used to `return` on the
            // first problem, discarding every variant that had already
            // byte-matched - so on Firebird 3, where DECIMAL precision is
            // capped and one variant cannot be created, the two the driver
            // got exactly right were never reported.
            std::ostringstream summary;      // variants that byte-matched
            std::string failures;            // variants that came back wrong
            std::string unavailable;         // variants the engine declined
            int ok_count = 0;
            int variant_idx = 0;
            for (const auto& shape : shapes) {
                ++variant_idx;
                std::ostringstream val_ddl;
                val_ddl << "DECIMAL(" << static_cast<int>(shape.precision)
                        << ", " << static_cast<int>(shape.scale) << ")";
                const std::string table_name = "ODBC_TEST_NUMERIC_BYTES";

                RoundTripTableGuard tbl(conn_, table_name, val_ddl.str());
                if (!tbl.ok()) {
                    // B6: an engine that cannot declare this precision has not
                    // failed the probe - it has removed one variant from it.
                    if (!unavailable.empty()) unavailable += "; ";
                    unavailable += val_ddl.str() + " (" + tbl.last_error() + ")";
                    continue;
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
                    // B6: record and keep going; the verdict is assembled after the loop.
                    if (!failures.empty()) failures += "; ";
                    failures += r.actual;
                    r.actual.clear();
                    r.status = TestStatus::PASS;
                    continue;
                }

                // SELECT back as SQL_C_NUMERIC.
                core::OdbcStatement sel(conn_);
                sel.execute("SELECT VAL FROM " + table_name);
                SQLRETURN ret = SQLFetch(sel.get_handle());
                if (!SQL_SUCCEEDED(ret)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SELECT/SQLFetch failed for variant "
                             + std::to_string(variant_idx);
                    // B6: record and keep going; the verdict is assembled after the loop.
                    if (!failures.empty()) failures += "; ";
                    failures += r.actual;
                    r.actual.clear();
                    r.status = TestStatus::PASS;
                    continue;
                }

                SQL_NUMERIC_STRUCT got;
                std::memset(&got, 0, sizeof(got));
                SQLLEN ind = 0;
                if (!set_numeric_descriptor(sel.get_handle(), 1,
                                            shape.precision, shape.scale)) {
                    // B3: setting the ARD precision/scale is what the spec
                    // requires before SQLGetData(SQL_C_NUMERIC), so a driver
                    // rejecting it is only "unsupported" if it says so.
                    report_failure(r, SQL_HANDLE_STMT, sel.get_handle(),
                                   "ARD descriptor configuration for SQL_C_NUMERIC");
                    // B6: record and keep going; the verdict is assembled after the loop.
                    if (!failures.empty()) failures += "; ";
                    failures += r.actual;
                    r.actual.clear();
                    r.status = TestStatus::PASS;
                    continue;
                }
                ret = SQLGetData(sel.get_handle(), 1, SQL_C_NUMERIC,
                                 &got, sizeof(got), &ind);
                if (!SQL_SUCCEEDED(ret)) {
                    // B3
                    report_failure(r, SQL_HANDLE_STMT, sel.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC) for variant " +
                                       std::to_string(variant_idx));
                    // B6: record and keep going; the verdict is assembled after the loop.
                    if (!failures.empty()) failures += "; ";
                    failures += r.actual;
                    r.actual.clear();
                    r.status = TestStatus::PASS;
                    continue;
                }

                // Recompute the expected mantissa using the scale the driver
                // reports — the spec lets a driver pick a different scale than
                // the column declares, so trust got.scale and verify val[]
                // matches it. A13: rescaled from the literal *text* by
                // truncation, which is the spec's conversion rule, rather than
                // llround() through a double.
                const Rescaled expected =
                    mantissa_for_scale(shape.literal, static_cast<int>(got.scale));

                // A13, second half. Accepting the driver's scale unconditionally
                // would make this probe blind to the corruption it exists to
                // catch: a driver that stores 12345.67 as 12345 and then reports
                // scale=0 would match a truncated expectation exactly. Dropping
                // *trailing zeros* is lossless and normal — the mock reports
                // scale 6 for 123.4567890 — but dropping a non-zero digit is
                // data loss, and the spec requires the driver to say so with
                // SQL_SUCCESS_WITH_INFO and 01S07 (fractional truncation).
                if (expected.dropped_nonzero) {
                    const bool announced =
                        (ret == SQL_SUCCESS_WITH_INFO) &&
                        (first_sqlstate(SQL_HANDLE_STMT, sel.get_handle()) == "01S07");
                    if (!announced) {
                        r.status = TestStatus::FAIL;
                        r.severity = Severity::CRITICAL;
                        r.actual = std::string("variant ") +
                                   std::to_string(variant_idx) + " (" +
                                   shape.literal + "): driver reported scale=" +
                                   std::to_string(got.scale) +
                                   ", silently dropping significant digits";
                        r.suggestion =
                            "Reducing the scale below the value's significant "
                            "digits loses data. A driver may do it, but must "
                            "return SQL_SUCCESS_WITH_INFO with SQLSTATE 01S07 "
                            "so the application knows.";
                        // B6: record and keep going; the verdict is assembled after the loop.
                        if (!failures.empty()) failures += "; ";
                        failures += r.actual;
                        r.actual.clear();
                        r.status = TestStatus::PASS;
                        continue;
                    }
                }
                const uint64_t expected_mantissa = expected.mantissa;
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
                    // B6: record and keep going; the verdict is assembled after the loop.
                    if (!failures.empty()) failures += "; ";
                    failures += r.actual;
                    r.actual.clear();
                    r.status = TestStatus::PASS;
                    continue;
                }

                if (ok_count > 0) summary << "; ";
                ++ok_count;
                summary << "DECIMAL(" << static_cast<int>(shape.precision)
                        << "," << static_cast<int>(shape.scale)
                        << ") ok at scale=" << static_cast<int>(got.scale);
            }

            // B6: one verdict from three outcomes. A variant that came back
            // wrong is a failure however many others were right; a variant
            // the engine cannot declare is not.
            std::ostringstream final_actual;
            final_actual << ok_count << "/" << (sizeof(shapes) / sizeof(shapes[0]))
                         << " (precision, scale) variants byte-match";
            if (ok_count > 0) final_actual << ": " << summary.str();
            if (!unavailable.empty()) {
                final_actual << " | not declarable on this engine: " << unavailable;
            }
            if (!failures.empty()) {
                final_actual << " | wrong: " << failures;
            }
            r.actual = final_actual.str();

            if (!failures.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.suggestion = "Verify SQL_NUMERIC_STRUCT.val is little-endian "
                               "mantissa = round(value * 10^got.scale), and "
                               "sign=1 positive / sign=0 negative.";
            } else if (ok_count == 0) {
                // Nothing ran, so nothing was learned.
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "No DECIMAL precision this probe tries could be "
                               "declared. Firebird 3 caps precision at 18; "
                               "check what this engine allows.";
            }
        });
}

// ── DECIMAL sum-loop precision (PORT plan §4.11) ───────────────────────────
//
// SQLComponents/TestSQL/TestSelections.cpp accumulates many DECIMAL rows
// and asserts master.total == sum(detail.amount). The unique add for
// odbc-crusher: do the accumulation via SQL_C_NUMERIC mantissa arithmetic
// (decimal-exact) — drivers that lose 1 ULP per row when the read path
// goes through `double` show up as a non-zero absolute error. Tested at
// DECIMAL(10, 2) which is well within both 32-bit mantissa range and
// IEEE-754 exact-double range, so the only source of error is the driver.

TestResult NumericStructTests::test_decimal_sum_loop_precision() {
    return run_test(
        "test_decimal_sum_loop_precision", "SQLGetData(SQL_C_NUMERIC)",
        "Sum of N DECIMAL(10, 2) rows accumulated via SQL_C_NUMERIC equals "
        "the expected exact sum",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData with SQL_C_NUMERIC — mantissa arithmetic exact",
        [&](TestResult& r) {
            constexpr int kRows = 100;
            constexpr SQLCHAR kPrec = 10;
            constexpr SQLCHAR kScale = 2;
            constexpr uint64_t kPerRowMantissa = 1ull;  // 0.01 at scale=2
            constexpr uint64_t kExpectedTotal =
                kPerRowMantissa * static_cast<uint64_t>(kRows);

            const std::string table = "ODBC_TEST_DECIMAL_SUM";
            RoundTripTableGuard tbl(conn_, table, "DECIMAL(10, 2)");
            if (!tbl.ok()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not create round-trip table: " + tbl.last_error();
                return;
            }

            try {
                for (int i = 1; i <= kRows; ++i) {
                    core::OdbcStatement ins(conn_);
                    ins.execute("INSERT INTO " + table +
                                " (ID, VAL) VALUES (" + std::to_string(i) + ", 0.01)");
                }
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = std::string("INSERT loop failed: ") + e.what();
                return;
            }

            core::OdbcStatement sel(conn_);
            sel.execute("SELECT VAL FROM " + table);

            if (!set_numeric_descriptor(sel.get_handle(), 1, kPrec, kScale)) {
                // B3: see above.
                report_failure(r, SQL_HANDLE_STMT, sel.get_handle(),
                               "ARD descriptor configuration for SQL_C_NUMERIC");
                return;
            }

            uint64_t total_mantissa = 0;
            int rows_read = 0;
            while (true) {
                SQLRETURN rc = SQLFetch(sel.get_handle());
                if (rc == SQL_NO_DATA) break;
                if (!SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "SQLFetch failed at row " + std::to_string(rows_read);
                    return;
                }
                SQL_NUMERIC_STRUCT ns;
                std::memset(&ns, 0, sizeof(ns));
                SQLLEN ind = 0;
                rc = SQLGetData(sel.get_handle(), 1, SQL_C_NUMERIC,
                                &ns, sizeof(ns), &ind);
                if (!SQL_SUCCEEDED(rc)) {
                    // B3
                    report_failure(r, SQL_HANDLE_STMT, sel.get_handle(),
                                   "SQLGetData(SQL_C_NUMERIC)");
                    return;
                }
                // Reconstruct mantissa, normalising for the driver's reported
                // scale. A26: the loop used to run over all 16 val[] bytes into
                // a 64-bit accumulator, silently dropping bytes 8-15. Values
                // that large cannot be summed exactly here, so say so instead
                // of reporting a wrapped total.
                if (numeric_exceeds_64_bits(ns)) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Driver returned a mantissa wider than 64 bits; "
                               "this probe sums mantissas exactly and cannot "
                               "represent it";
                    return;
                }
                uint64_t mantissa = 0;
                for (int i = 7; i >= 0; --i) {
                    mantissa = (mantissa << 8) | ns.val[i];
                }
                int scale_diff = static_cast<int>(ns.scale) - static_cast<int>(kScale);
                if (scale_diff > 0) {
                    for (int s = 0; s < scale_diff; ++s) mantissa /= 10ull;
                } else if (scale_diff < 0) {
                    for (int s = 0; s < -scale_diff; ++s) mantissa *= 10ull;
                }
                total_mantissa += mantissa;
                ++rows_read;
            }

            std::ostringstream summary;
            summary << "rows_read=" << rows_read << "/" << kRows
                    << " total_mantissa=" << total_mantissa
                    << " expected=" << kExpectedTotal
                    << " (= " << kRows << " × 0.01 at scale=" << static_cast<int>(kScale)
                    << ")";

            if (rows_read != kRows) {
                r.status = TestStatus::FAIL;
                r.actual = summary.str();
                r.suggestion = "Driver returned a different number of rows than "
                               "were inserted — independent of precision, this "
                               "indicates row drop/duplication.";
                return;
            }
            if (total_mantissa != kExpectedTotal) {
                const long abs_err = static_cast<long>(total_mantissa) -
                                     static_cast<long>(kExpectedTotal);
                r.status = TestStatus::FAIL;
                r.actual = summary.str() + " absolute_error_in_units=" +
                           std::to_string(abs_err);
                r.suggestion = "Driver loses precision when reading DECIMAL via "
                               "SQL_C_NUMERIC — likely round-tripping through "
                               "double internally. SQL_NUMERIC_STRUCT is meant "
                               "to preserve exact mantissa.";
                return;
            }
            r.actual = summary.str();
        });
}

} // namespace odbc_crusher::tests
