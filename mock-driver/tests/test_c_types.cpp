// The shared C-type table and delivery — IMPROVEMENT_PLAN.md D11, D18.
//
// The bound-column fetch path handled SQL_C_SLONG, SQL_C_SBIGINT and
// SQL_C_SSHORT for integers and SQL_C_DOUBLE / SQL_C_FLOAT for doubles, and
// let every other C type fall through to `default:` — where it wrote an ANSI
// decimal string into the caller's buffer. An application binding a column as
// SQL_C_ULONG got the characters "42" laid over its four-byte integer and an
// indicator claiming a string length: SQL_SUCCESS returned, garbage
// delivered.
//
// These tests bind each of the types that used to fall through and check the
// bytes that arrive, which is the only way to tell a value from its decimal
// spelling.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include "utils/c_types.hpp"

#include <cstring>
#include <string>

using namespace mock_odbc;

// ── the table itself ──────────────────────────────────────────────────────

TEST(CTypes, KnowsTheUnsignedTypesAreUnsigned) {
    EXPECT_EQ(c_type_info(SQL_C_ULONG).kind, CTypeKind::Integer);
    EXPECT_FALSE(c_type_info(SQL_C_ULONG).is_signed);
    EXPECT_TRUE(c_type_info(SQL_C_SLONG).is_signed);
    EXPECT_EQ(c_type_info(SQL_C_ULONG).size, sizeof(SQLUINTEGER));
    EXPECT_EQ(c_type_info(SQL_C_UBIGINT).size, sizeof(SQLUBIGINT));
    EXPECT_EQ(c_type_info(SQL_C_UTINYINT).size, 1u);
}

// The variable-length kinds have no intrinsic size, which is why
// c_type_element_size has to be told the caller's buffer: it is the array
// stride for a column-wise bound array.
TEST(CTypes, VariableLengthTypesStrideByTheCallersBuffer) {
    EXPECT_EQ(c_type_info(SQL_C_CHAR).size, 0u);
    EXPECT_EQ(c_type_element_size(SQL_C_CHAR, 64), 64);
    EXPECT_EQ(c_type_element_size(SQL_C_SLONG, 64),
              static_cast<SQLLEN>(sizeof(SQLINTEGER)))
        << "a fixed-size type strides by its own size, not the buffer";
}

// ── delivery: the types that used to get a decimal string ─────────────────

TEST(CTypes, UnsignedLongGetsAnIntegerNotItsDigits) {
    SQLUINTEGER slot = 0;
    SQLLEN ind = -99;

    const SQLRETURN rc = write_numeric_as(SQL_C_ULONG, 42, 0.0, false,
                                          &slot, sizeof(slot), &ind);

    EXPECT_EQ(rc, SQL_SUCCESS);
    EXPECT_EQ(slot, 42u) << "before D11 this held the bytes '4' and '2'";
    EXPECT_EQ(ind, static_cast<SQLLEN>(sizeof(SQLUINTEGER)))
        << "the indicator is the size written, not a string length";
}

TEST(CTypes, UnsignedBigIntAndTinyIntAndBit) {
    SQLUBIGINT big = 0;
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_UBIGINT, 5000000000LL, 0.0, false,
                               &big, sizeof(big), &ind), SQL_SUCCESS);
    EXPECT_EQ(big, 5000000000ULL);

    SQLCHAR tiny = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_UTINYINT, 200, 0.0, false,
                               &tiny, sizeof(tiny), &ind), SQL_SUCCESS);
    EXPECT_EQ(tiny, 200);
    EXPECT_EQ(ind, 1);

    SQLCHAR bit = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_BIT, 1, 0.0, false,
                               &bit, sizeof(bit), &ind), SQL_SUCCESS);
    EXPECT_EQ(bit, 1);
}

// An integer cell bound as a floating C type, and the reverse. Both are
// defined conversions and both used to depend on which switch arm existed.
TEST(CTypes, ConvertsBetweenIntegerAndFloatingTargets) {
    SQLDOUBLE d = 0;
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_DOUBLE, 7, 0.0, false,
                               &d, sizeof(d), &ind), SQL_SUCCESS);
    EXPECT_DOUBLE_EQ(d, 7.0);

    SQLINTEGER i = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_SLONG, 0, 2.6, true,
                               &i, sizeof(i), &ind), SQL_SUCCESS);
    EXPECT_EQ(i, 3) << "rounds rather than truncating toward zero";
}

// SQL_C_CHAR still gets the digits — that is the one target for which the
// decimal spelling is the right answer.
TEST(CTypes, CharTargetStillGetsTheDigits) {
    char buf[16] = {0};
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_CHAR, 42, 0.0, false,
                               buf, sizeof(buf), &ind), SQL_SUCCESS);
    EXPECT_STREQ(buf, "42");
    EXPECT_EQ(ind, 2) << "for a character target the indicator is a length";
}

TEST(CTypes, CharTargetReportsTruncation) {
    char buf[2] = {0};
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_CHAR, 12345, 0.0, false,
                               buf, sizeof(buf), &ind), SQL_SUCCESS_WITH_INFO);
    EXPECT_STREQ(buf, "1");
    EXPECT_EQ(ind, 5) << "the indicator is what was available";
}

// SQL_C_WCHAR used to fall through to the ANSI branch, so a Unicode
// application binding a numeric column got single-byte characters in a
// UTF-16 buffer.
TEST(CTypes, WCharTargetGetsUtf16NotAnsi) {
    SQLWCHAR buf[8] = {0};
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_WCHAR, 42, 0.0, false,
                               buf, sizeof(buf), &ind), SQL_SUCCESS);
    EXPECT_EQ(buf[0], '4');
    EXPECT_EQ(buf[1], '2');
    EXPECT_EQ(buf[2], 0);
    EXPECT_EQ(ind, static_cast<SQLLEN>(2 * sizeof(SQLWCHAR)))
        << "SQL_C_WCHAR indicators are byte counts";
}

// A fixed-size target the caller sized too small is an error, not a partial
// write: half of an SQLINTEGER is a different number, not a smaller one. A
// zero length is the idiomatic ODBC way to say "the type's own size" and must
// not be mistaken for too small.
TEST(CTypes, UndersizedFixedTargetIsAnErrorButZeroMeansNaturalSize) {
    SQLSMALLINT small = 0;
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_SLONG, 1, 0.0, false,
                               &small, sizeof(small), &ind), SQL_ERROR);

    SQLINTEGER ok = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_SLONG, 1, 0.0, false, &ok, 0, &ind),
              SQL_SUCCESS);
    EXPECT_EQ(ok, 1);
}

// A numeric cell asked for as a date has no conversion the spec defines.
// Writing its digits — which is what `default:` did — is the worst answer.
TEST(CTypes, UndefinedConversionIsAnErrorRatherThanDigits) {
    DATE_STRUCT d{};
    SQLLEN ind = 0;
    EXPECT_EQ(write_numeric_as(SQL_C_TYPE_DATE, 20260907, 0.0, false,
                               &d, sizeof(d), &ind), SQL_ERROR);
    EXPECT_EQ(d.year, 0) << "left the caller's struct alone";
}
