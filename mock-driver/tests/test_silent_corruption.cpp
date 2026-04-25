// Silent-corruption mode tests — IMPROVEMENT_PLAN.md §5.2
//
// Each mode keeps SQLExecute returning SUCCESS while tampering with stored
// data. These tests prove the mock actually corrupts (the §5.1 E2E harness
// then verifies that crusher's verify_rows_persisted catches it).
#include <gtest/gtest.h>
#include <windows.h>
#include <sql.h>
#include <sqlext.h>
#include <string>

class SilentCorruptionTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv), SQL_SUCCESS);
        ASSERT_EQ(SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                                (SQLPOINTER)SQL_OV_ODBC3, 0), SQL_SUCCESS);
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc), SQL_SUCCESS);
    }

    void TearDown() override {
        if (hstmt != SQL_NULL_HSTMT) SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv);
    }

    void Connect(const std::string& extra_config) {
        std::string conn = "Driver={Mock ODBC Driver};Mode=Success;" + extra_config;
        SQLRETURN ret = SQLDriverConnect(hdbc, NULL, (SQLCHAR*)conn.c_str(),
                                         SQL_NTS, NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret));
        ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_STMT, hdbc, &hstmt), SQL_SUCCESS);
    }

    void Exec(const std::string& sql) {
        SQLRETURN ret = SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << "SQL failed: " << sql;
        SQLCloseCursor(hstmt);
    }

    long CountRows(const std::string& table) {
        std::string sql = "SELECT COUNT(*) FROM " + table;
        EXPECT_EQ(SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS), SQL_SUCCESS);
        long count = -1;
        if (SQLFetch(hstmt) == SQL_SUCCESS) {
            SQLLEN ind = 0;
            SQLGetData(hstmt, 1, SQL_C_LONG, &count, sizeof(count), &ind);
        }
        SQLCloseCursor(hstmt);
        return count;
    }

    std::string FetchFirstString(const std::string& sql) {
        EXPECT_EQ(SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS), SQL_SUCCESS);
        char buf[256] = {0};
        SQLLEN ind = 0;
        if (SQLFetch(hstmt) == SQL_SUCCESS) {
            SQLGetData(hstmt, 1, SQL_C_CHAR, buf, sizeof(buf), &ind);
        }
        SQLCloseCursor(hstmt);
        return buf;
    }

    double FetchFirstDouble(const std::string& sql) {
        EXPECT_EQ(SQLExecDirect(hstmt, (SQLCHAR*)sql.c_str(), SQL_NTS), SQL_SUCCESS);
        double d = 0.0;
        SQLLEN ind = 0;
        if (SQLFetch(hstmt) == SQL_SUCCESS) {
            SQLGetData(hstmt, 1, SQL_C_DOUBLE, &d, sizeof(d), &ind);
        }
        SQLCloseCursor(hstmt);
        return d;
    }

    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    SQLHSTMT hstmt = SQL_NULL_HSTMT;
};

// Baseline — no corruption: rows land verbatim.
TEST_F(SilentCorruptionTest, NoneStoresRowsVerbatim) {
    Connect("");
    Exec("CREATE TABLE T_NONE (ID INTEGER, V VARCHAR(50))");
    Exec("INSERT INTO T_NONE (ID, V) VALUES (1, 'Hello')");

    EXPECT_EQ(CountRows("T_NONE"), 1);
    EXPECT_EQ(FetchFirstString("SELECT V FROM T_NONE"), "Hello");
}

// DropInserts — INSERT returns SUCCESS but the row is never stored.
// This is the exact shape of the Firebird ≤3.5.0 callout in §1.2.
TEST_F(SilentCorruptionTest, DropInsertsAcceptsButStoresNothing) {
    Connect("SilentCorruption=DropInserts;");
    Exec("CREATE TABLE T_DROP (ID INTEGER, V VARCHAR(50))");
    Exec("INSERT INTO T_DROP (ID, V) VALUES (1, 'Hello')");
    Exec("INSERT INTO T_DROP (ID, V) VALUES (2, 'World')");

    EXPECT_EQ(CountRows("T_DROP"), 0)
        << "DropInserts must keep the row count at zero";
}

// MangleVarchar — the stored string gets a sentinel char appended.
// Universal corruption (works on numeric-as-string values too — the §1.1
// roundtrip canaries insert "1","2","3"... which case-swap can't touch).
TEST_F(SilentCorruptionTest, MangleVarcharAppendsSentinelOnStore) {
    Connect("SilentCorruption=MangleVarchar;");
    Exec("CREATE TABLE T_MANGLE (ID INTEGER, V VARCHAR(50))");
    Exec("INSERT INTO T_MANGLE (ID, V) VALUES (1, 'Hello')");

    EXPECT_EQ(CountRows("T_MANGLE"), 1)
        << "MangleVarchar must still store the row";
    EXPECT_EQ(FetchFirstString("SELECT V FROM T_MANGLE"), "HelloX")
        << "MangleVarchar must append a sentinel to the stored string";
}

// MangleVarchar must trip on numeric-string values too — the canary case
// for §1.1 round-trip tests that insert integers as VARCHAR.
TEST_F(SilentCorruptionTest, MangleVarcharTripsOnNumericString) {
    Connect("SilentCorruption=MangleVarchar;");
    Exec("CREATE TABLE T_MANGLE_NUM (ID INTEGER, V VARCHAR(50))");
    Exec("INSERT INTO T_MANGLE_NUM (ID, V) VALUES (1, '5')");

    EXPECT_EQ(FetchFirstString("SELECT V FROM T_MANGLE_NUM"), "5X")
        << "MangleVarchar must alter numeric strings — otherwise the §1.1 "
           "round-trip canaries can't catch the corruption.";
}

// TruncateNumeric — stored doubles lose their fractional part silently.
TEST_F(SilentCorruptionTest, TruncateNumericDropsFractionalPart) {
    Connect("SilentCorruption=TruncateNumeric;");
    Exec("CREATE TABLE T_TRUNC (ID INTEGER, V DOUBLE)");
    Exec("INSERT INTO T_TRUNC (ID, V) VALUES (1, 3.75)");

    EXPECT_EQ(CountRows("T_TRUNC"), 1);
    EXPECT_DOUBLE_EQ(FetchFirstDouble("SELECT V FROM T_TRUNC"), 3.0);
}

// Default mode is None even when other knobs are set — sanity check that
// SilentCorruption isn't accidentally triggered by an unrelated config.
TEST_F(SilentCorruptionTest, DefaultIsNoneWithUnrelatedKnobs) {
    Connect("Catalog=Default;ResultSetSize=10;");
    Exec("CREATE TABLE T_DEFAULT (ID INTEGER, V VARCHAR(50))");
    Exec("INSERT INTO T_DEFAULT (ID, V) VALUES (1, 'Verbatim')");

    EXPECT_EQ(CountRows("T_DEFAULT"), 1);
    EXPECT_EQ(FetchFirstString("SELECT V FROM T_DEFAULT"), "Verbatim");
}
