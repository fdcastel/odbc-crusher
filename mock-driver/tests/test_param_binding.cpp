// SQLBindParameter C-type matrix — IMPROVEMENT_PLAN.md §7.11
//
// Locks in the §7.7 fix: every C-type that SQLBindParameter accepts must
// round-trip cleanly through INSERT + SELECT. Before §7.7, anything
// outside {SLONG/LONG, SBIGINT, SSHORT, FLOAT, DOUBLE, NUMERIC, WCHAR,
// CHAR} fell through to the SQL_C_CHAR default in read_param_value and
// silently corrupted the stored value.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>
#include <cstring>
#include <string>

class ParamBindingTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);

        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;";
        ASSERT_TRUE(SQL_SUCCEEDED(SQLDriverConnect(
            hdbc, NULL, (SQLCHAR*)conn.c_str(),
            SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT)));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);

        Exec("CREATE TABLE T (ID INTEGER, V VARCHAR(64))");
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    void Exec(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLCloseCursor(hstmt);
    }

    // Bind one row of (id=row_id, v=raw bytes of `value` interpreted as `c_type`),
    // execute, and read column V back as SQL_C_CHAR. Returns the stored string.
    template <typename T>
    std::string RoundTripVarchar(SQLSMALLINT c_type, T value, SQLINTEGER row_id) {
        SQLPrepare(hstmt, (SQLCHAR*)"INSERT INTO T (ID, V) VALUES (?, ?)", SQL_NTS);

        SQLINTEGER id = row_id;
        SQLLEN id_ind = 0;
        SQLLEN val_ind = 0;
        T val = value;

        EXPECT_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT,
                                   SQL_C_SLONG, SQL_INTEGER, 0, 0,
                                   &id, 0, &id_ind), SQL_SUCCESS);
        EXPECT_EQ(SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT,
                                   c_type, SQL_VARCHAR, 64, 0,
                                   &val, sizeof(val), &val_ind), SQL_SUCCESS);
        EXPECT_EQ(SQLExecute(hstmt), SQL_SUCCESS);
        SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
        SQLFreeStmt(hstmt, SQL_CLOSE);

        std::string sel = "SELECT V FROM T WHERE ID = " + std::to_string(row_id);
        EXPECT_EQ(SQLExecDirect(hstmt, (SQLCHAR*)sel.c_str(), SQL_NTS), SQL_SUCCESS);
        char buf[256] = {0};
        SQLLEN ind = 0;
        if (SQLFetch(hstmt) == SQL_SUCCESS) {
            SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        }
        SQLCloseCursor(hstmt);
        return buf;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// ── Integer C-types (the §7.7 hole) ───────────────────────────────────────

TEST_F(ParamBindingTest, STinyIntRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLSCHAR>(SQL_C_STINYINT, 42, 1), "42");
}

TEST_F(ParamBindingTest, UTinyIntRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLCHAR>(SQL_C_UTINYINT, 200, 2), "200");
}

TEST_F(ParamBindingTest, SShortRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLSMALLINT>(SQL_C_SSHORT, -12345, 3), "-12345");
}

TEST_F(ParamBindingTest, UShortRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLUSMALLINT>(SQL_C_USHORT, 65000, 4), "65000");
}

TEST_F(ParamBindingTest, SLongRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLINTEGER>(SQL_C_SLONG, -123456789, 5), "-123456789");
}

TEST_F(ParamBindingTest, ULongRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLUINTEGER>(SQL_C_ULONG, 4000000000U, 6), "4000000000");
}

TEST_F(ParamBindingTest, SBigIntRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLBIGINT>(SQL_C_SBIGINT,
                                          -9000000000000LL, 7), "-9000000000000");
}

TEST_F(ParamBindingTest, UBigIntRoundTrip) {
    EXPECT_EQ(RoundTripVarchar<SQLUBIGINT>(SQL_C_UBIGINT,
                                           9000000000000ULL, 8), "9000000000000");
}

TEST_F(ParamBindingTest, BitRoundTripTrue) {
    EXPECT_EQ(RoundTripVarchar<SQLCHAR>(SQL_C_BIT, 1, 9), "1");
}

TEST_F(ParamBindingTest, BitRoundTripFalse) {
    EXPECT_EQ(RoundTripVarchar<SQLCHAR>(SQL_C_BIT, 0, 10), "0");
}

// ── Float types ───────────────────────────────────────────────────────────
//
// SQL_C_FLOAT was already accepted by SQLBindParameter but never had a
// case in c_type_element_size for column-wise param-set arrays — single-
// row binds worked, but a probe doing array-bind would have miscomputed
// strides. §7.7 closed that.

TEST_F(ParamBindingTest, FloatRoundTrip) {
    auto s = RoundTripVarchar<SQLREAL>(SQL_C_FLOAT, 1.5f, 11);
    // std::to_string(1.5) → "1.500000" — exact textual match would be
    // brittle. Just assert the leading "1.5" prefix shows up so we know
    // the float was actually read as a float, not as raw bytes.
    EXPECT_NE(s.find("1.5"), std::string::npos)
        << "SQL_C_FLOAT round-trip produced '" << s << "'";
}

TEST_F(ParamBindingTest, DoubleRoundTrip) {
    auto s = RoundTripVarchar<SQLDOUBLE>(SQL_C_DOUBLE, -2.25, 12);
    EXPECT_NE(s.find("-2.25"), std::string::npos)
        << "SQL_C_DOUBLE round-trip produced '" << s << "'";
}

// ── Date / time / timestamp formatting (§7.7) ─────────────────────────────

TEST_F(ParamBindingTest, DateRoundTrip) {
    SQLPrepare(hstmt, (SQLCHAR*)"INSERT INTO T (ID, V) VALUES (?, ?)", SQL_NTS);
    SQLINTEGER id = 13;
    DATE_STRUCT d{};
    d.year = 2026; d.month = 1; d.day = 5;
    SQLLEN ind = 0, vind = sizeof(d);
    ASSERT_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT,
                               SQL_C_SLONG, SQL_INTEGER, 0, 0,
                               &id, 0, &ind), SQL_SUCCESS);
    ASSERT_EQ(SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT,
                               SQL_C_TYPE_DATE, SQL_VARCHAR, 64, 0,
                               &d, sizeof(d), &vind), SQL_SUCCESS);
    ASSERT_EQ(SQLExecute(hstmt), SQL_SUCCESS);
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    ASSERT_EQ(SQLExecDirect(hstmt,
        (SQLCHAR*)"SELECT V FROM T WHERE ID = 13", SQL_NTS), SQL_SUCCESS);
    char buf[256] = {0};
    SQLLEN gind = 0;
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &gind);
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(buf, "2026-01-05");
}

TEST_F(ParamBindingTest, TimestampRoundTripWithoutFraction) {
    SQLPrepare(hstmt, (SQLCHAR*)"INSERT INTO T (ID, V) VALUES (?, ?)", SQL_NTS);
    SQLINTEGER id = 14;
    TIMESTAMP_STRUCT ts{};
    ts.year = 2026; ts.month = 4; ts.day = 25;
    ts.hour = 10; ts.minute = 30; ts.second = 0;
    ts.fraction = 0;
    SQLLEN ind = 0, vind = sizeof(ts);
    ASSERT_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT,
                               SQL_C_SLONG, SQL_INTEGER, 0, 0,
                               &id, 0, &ind), SQL_SUCCESS);
    ASSERT_EQ(SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT,
                               SQL_C_TYPE_TIMESTAMP, SQL_VARCHAR, 64, 0,
                               &ts, sizeof(ts), &vind), SQL_SUCCESS);
    ASSERT_EQ(SQLExecute(hstmt), SQL_SUCCESS);
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    ASSERT_EQ(SQLExecDirect(hstmt,
        (SQLCHAR*)"SELECT V FROM T WHERE ID = 14", SQL_NTS), SQL_SUCCESS);
    char buf[256] = {0};
    SQLLEN gind = 0;
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &gind);
    SQLCloseCursor(hstmt);
    EXPECT_STREQ(buf, "2026-04-25 10:30:00");
}

// ── BINARY (§7.7) ─────────────────────────────────────────────────────────
//
// Pre-§7.7 the BINARY case fell through to SQL_C_CHAR's strnlen path,
// which would stop at the first 0x00 byte — so embedded NULs were
// silently truncated. The new BINARY case honours the indicator length
// instead. Use a 4-byte payload with a NUL in the middle and verify
// the stored size.

TEST_F(ParamBindingTest, BinaryHonoursIndicatorLength) {
    SQLPrepare(hstmt, (SQLCHAR*)"INSERT INTO T (ID, V) VALUES (?, ?)", SQL_NTS);
    SQLINTEGER id = 15;
    unsigned char payload[4] = {0xDE, 0x00, 0xBE, 0xEF};
    SQLLEN ind = 0, vind = sizeof(payload);
    ASSERT_EQ(SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT,
                               SQL_C_SLONG, SQL_INTEGER, 0, 0,
                               &id, 0, &ind), SQL_SUCCESS);
    ASSERT_EQ(SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT,
                               SQL_C_BINARY, SQL_VARBINARY, 4, 0,
                               payload, sizeof(payload), &vind), SQL_SUCCESS);
    ASSERT_EQ(SQLExecute(hstmt), SQL_SUCCESS);
    SQLFreeStmt(hstmt, SQL_RESET_PARAMS);
    SQLFreeStmt(hstmt, SQL_CLOSE);

    ASSERT_EQ(SQLExecDirect(hstmt,
        (SQLCHAR*)"SELECT V FROM T WHERE ID = 15", SQL_NTS), SQL_SUCCESS);
    unsigned char out[16] = {0};
    SQLLEN gind = 0;
    ASSERT_EQ(SQLFetch(hstmt), SQL_SUCCESS);
    SQLGetData(hstmt, 1, SQL_C_BINARY, out, sizeof(out), &gind);
    SQLCloseCursor(hstmt);
    EXPECT_EQ(gind, 4) << "Indicator should report all 4 bytes — including the embedded NUL.";
    EXPECT_EQ(out[0], 0xDE);
    EXPECT_EQ(out[1], 0x00);
    EXPECT_EQ(out[2], 0xBE);
    EXPECT_EQ(out[3], 0xEF);
}
