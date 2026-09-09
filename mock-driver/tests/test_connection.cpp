// Phase 2: Connection Management Tests
#include <gtest/gtest.h>
#include "driver/handles.hpp"
#include "mock/behaviors.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

#include <chrono>
#include <cstring>
#include <string>

using namespace mock_odbc;

extern "C" {
    SQLRETURN SQL_API SQLAllocHandle(SQLSMALLINT, SQLHANDLE, SQLHANDLE*);
    SQLRETURN SQL_API SQLFreeHandle(SQLSMALLINT, SQLHANDLE);
    SQLRETURN SQL_API SQLSetEnvAttr(SQLHENV, SQLINTEGER, SQLPOINTER, SQLINTEGER);
    SQLRETURN SQL_API SQLConnect(SQLHDBC, SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
    SQLRETURN SQL_API SQLDriverConnect(SQLHDBC, SQLHWND, SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*, SQLUSMALLINT);
    SQLRETURN SQL_API SQLBrowseConnect(SQLHDBC, SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*);
    SQLRETURN SQL_API SQLDisconnect(SQLHDBC);
    SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER, SQLINTEGER*);
    SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER);
    SQLRETURN SQL_API SQLGetInfo(SQLHDBC, SQLUSMALLINT, SQLPOINTER, SQLSMALLINT, SQLSMALLINT*);
    SQLRETURN SQL_API SQLNativeSql(SQLHDBC, SQLCHAR*, SQLINTEGER, SQLCHAR*, SQLINTEGER, SQLINTEGER*);
    SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT, SQLHANDLE, SQLSMALLINT, SQLCHAR*,
                                    SQLINTEGER*, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*);
}

class ConnectionTest : public ::testing::Test {
protected:
    SQLHENV henv = SQL_NULL_HENV;
    SQLHDBC hdbc = SQL_NULL_HDBC;
    
    void SetUp() override {
        SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        ret = SQLSetEnvAttr(henv, SQL_ATTR_ODBC_VERSION,
                            reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_OV_ODBC3)), 0);
        ASSERT_EQ(ret, SQL_SUCCESS);
        
        ret = SQLAllocHandle(SQL_HANDLE_DBC, henv, &hdbc);
        ASSERT_EQ(ret, SQL_SUCCESS);
    }
    
    void TearDown() override {
        if (hdbc != SQL_NULL_HDBC) {
            SQLDisconnect(hdbc);
            SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
        }
        if (henv != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, henv);
        }
        // Reset behavior controller. There is no reset() — the singleton is
        // reset by pushing a default-constructed config back into it.
        BehaviorController::instance().set_config(DriverConfig{});
    }
};

// ===== SQLConnect Tests =====

TEST_F(ConnectionTest, SQLConnect_Basic) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass")), SQL_NTS);
    
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    auto* conn = validate_dbc_handle(hdbc);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->is_connected());
}

TEST_F(ConnectionTest, SQLConnect_InvalidHandle) {
    SQLRETURN ret = SQLConnect(SQL_NULL_HDBC,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass")), SQL_NTS);
    
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}

TEST_F(ConnectionTest, SQLConnect_AlreadyConnected) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass")), SQL_NTS);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    // Try to connect again
    ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN2")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser2")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass2")), SQL_NTS);
    
    EXPECT_EQ(ret, SQL_ERROR);  // Already connected
}

// ===== SQLDriverConnect Tests =====

TEST_F(ConnectionTest, SQLDriverConnect_Basic) {
    SQLCHAR connStrOut[256] = {0};
    SQLSMALLINT outLen = 0;
    
    SQLRETURN ret = SQLDriverConnect(hdbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("DSN=TestDSN;UID=user;PWD=pass")), SQL_NTS,
        connStrOut, sizeof(connStrOut), &outLen, SQL_DRIVER_NOPROMPT);
    
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    auto* conn = validate_dbc_handle(hdbc);
    ASSERT_NE(conn, nullptr);
    EXPECT_TRUE(conn->is_connected());
}

TEST_F(ConnectionTest, SQLDriverConnect_NoOutput) {
    SQLRETURN ret = SQLDriverConnect(hdbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("DSN=TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
    
    EXPECT_EQ(ret, SQL_SUCCESS);
}

// P10: `DATABASE=` is an accepted spelling of `Catalog=`, so it names one of
// this driver's presets. This case used to pass `DATABASE=testdb` and expect
// success - the keyword was parsed by nobody and the value went nowhere.
TEST_F(ConnectionTest, SQLDriverConnect_WithDatabase) {
    SQLCHAR connStrOut[256] = {0};
    SQLSMALLINT outLen = 0;

    SQLRETURN ret = SQLDriverConnect(hdbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("DSN=TestDSN;DATABASE=Default;UID=admin")), SQL_NTS,
        connStrOut, sizeof(connStrOut), &outLen, SQL_DRIVER_NOPROMPT);

    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_GT(outLen, 0);
}

// P10: and a database this driver does not have is refused - the one connect
// failure a caller can ask for without knowing `FailOn`, which is what
// test_failed_connect_diagnostics_are_wellformed needs.
TEST_F(ConnectionTest, SQLDriverConnect_RefusesAnUnknownDatabase) {
    SQLCHAR connStrOut[256] = {0};
    SQLSMALLINT outLen = 0;

    SQLRETURN ret = SQLDriverConnect(hdbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("DSN=TestDSN;DATABASE=no_such_database;UID=admin")), SQL_NTS,
        connStrOut, sizeof(connStrOut), &outLen, SQL_DRIVER_NOPROMPT);

    ASSERT_EQ(ret, SQL_ERROR);

    SQLCHAR state[16] = {0};
    SQLCHAR message[512] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT msg_len = 0;
    ASSERT_EQ(SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state, &native, message,
                            sizeof(message), &msg_len),
              SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<const char*>(state), "08001");
    // The record describes itself: the length it reports is the length it
    // wrote. `ConnectDiagnostics=Garbled` is what breaks that, deliberately.
    EXPECT_EQ(static_cast<size_t>(msg_len),
              std::strlen(reinterpret_cast<const char*>(message)));
    EXPECT_NE(std::string(reinterpret_cast<const char*>(message))
                  .find("no_such_database"),
              std::string::npos);
}

// P10: the same refusal, described by a driver that cannot describe it. One
// byte of the message with the length of all of it, and a native code that
// moves - the Firebird ODBC PR #298 shape.
TEST_F(ConnectionTest, SQLDriverConnect_GarbledDiagnosticsAreGarbled) {
    const char* conn_str =
        "DSN=TestDSN;DATABASE=no_such_database;ConnectDiagnostics=Garbled";

    auto refuse = [&](SQLHDBC dbc, SQLINTEGER* native_out,
                      SQLSMALLINT* msg_len_out, std::string* message_out) {
        SQLCHAR connStrOut[256] = {0};
        SQLSMALLINT outLen = 0;
        ASSERT_EQ(SQLDriverConnect(dbc, nullptr,
                                   reinterpret_cast<SQLCHAR*>(
                                       const_cast<char*>(conn_str)),
                                   SQL_NTS, connStrOut, sizeof(connStrOut),
                                   &outLen, SQL_DRIVER_NOPROMPT),
                  SQL_ERROR);
        SQLCHAR state[16] = {0};
        SQLCHAR message[512] = {0};
        ASSERT_EQ(SQLGetDiagRec(SQL_HANDLE_DBC, dbc, 1, state, native_out,
                                message, sizeof(message), msg_len_out),
                  SQL_SUCCESS);
        // No SQLSTATE at all. Read straight off the driver there is no driver
        // manager in the way to substitute one.
        EXPECT_STREQ(reinterpret_cast<const char*>(state), "");
        *message_out = reinterpret_cast<const char*>(message);
    };

    SQLINTEGER native_a = 0;
    SQLSMALLINT len_a = 0;
    std::string message_a;
    refuse(hdbc, &native_a, &len_a, &message_a);
    EXPECT_EQ(message_a.size(), 1u);
    EXPECT_GT(len_a, 1) << "the reported length has to disagree with the "
                           "single byte that was written";

    SQLHDBC second = SQL_NULL_HDBC;
    ASSERT_EQ(SQLAllocHandle(SQL_HANDLE_DBC, henv, &second), SQL_SUCCESS);
    SQLINTEGER native_b = 0;
    SQLSMALLINT len_b = 0;
    std::string message_b;
    refuse(second, &native_b, &len_b, &message_b);
    SQLFreeHandle(SQL_HANDLE_DBC, second);

    EXPECT_NE(native_a, native_b)
        << "two identical refusals reported the same native code, so the "
           "drift this mode exists to reproduce is not happening";
}

// ===== SQLBrowseConnect Tests =====

TEST_F(ConnectionTest, SQLBrowseConnect_Basic) {
    SQLCHAR outConnStr[256] = {0};
    SQLSMALLINT outLen = 0;
    
    SQLRETURN ret = SQLBrowseConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("DSN=TestDSN")), SQL_NTS,
        outConnStr, sizeof(outConnStr), &outLen);
    
    // BrowseConnect returns SQL_SUCCESS when connection is complete
    // or SQL_NEED_DATA when more info is needed
    EXPECT_TRUE(ret == SQL_SUCCESS || ret == SQL_NEED_DATA);
}

// ===== SQLDisconnect Tests =====

TEST_F(ConnectionTest, SQLDisconnect_Basic) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass")), SQL_NTS);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    ret = SQLDisconnect(hdbc);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    auto* conn = validate_dbc_handle(hdbc);
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_connected());
}

TEST_F(ConnectionTest, SQLDisconnect_NotConnected) {
    // The original assertion here was SQL_SUCCESS, with the comment
    // "Disconnecting when not connected is OK". That is not what the spec
    // says: SQLDisconnect on a connection that is not open returns SQL_ERROR
    // with SQLSTATE 08003 (Connection not open). The mock is right and this
    // test was wrong — it had never been compiled, so nobody found out.
    SQLRETURN ret = SQLDisconnect(hdbc);
    EXPECT_EQ(ret, SQL_ERROR);

    auto* conn = validate_dbc_handle(hdbc);
    ASSERT_NE(conn, nullptr);
    SQLCHAR state[6] = {0};
    SQLSMALLINT msg_len = 0;
    SQLRETURN diag_ret = SQLGetDiagRec(SQL_HANDLE_DBC, hdbc, 1, state,
                                       nullptr, nullptr, 0, &msg_len);
    EXPECT_TRUE(SQL_SUCCEEDED(diag_ret));
    EXPECT_STREQ(reinterpret_cast<const char*>(state), "08003");
}

TEST_F(ConnectionTest, SQLDisconnect_InvalidHandle) {
    SQLRETURN ret = SQLDisconnect(SQL_NULL_HDBC);
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}

// ===== SQLGetConnectAttr / SQLSetConnectAttr Tests =====

TEST_F(ConnectionTest, SetGetConnectAttr_AutoCommit) {
    SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT,
        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_AUTOCOMMIT_OFF)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLUINTEGER value = 0;
    ret = SQLGetConnectAttr(hdbc, SQL_ATTR_AUTOCOMMIT, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(value, static_cast<SQLUINTEGER>(SQL_AUTOCOMMIT_OFF));
}

TEST_F(ConnectionTest, SetGetConnectAttr_LoginTimeout) {
    SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_LOGIN_TIMEOUT,
        reinterpret_cast<SQLPOINTER>(30), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLUINTEGER value = 0;
    ret = SQLGetConnectAttr(hdbc, SQL_ATTR_LOGIN_TIMEOUT, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(value, 30u);
}

TEST_F(ConnectionTest, SetGetConnectAttr_ConnectionTimeout) {
    SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_CONNECTION_TIMEOUT,
        reinterpret_cast<SQLPOINTER>(60), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLUINTEGER value = 0;
    ret = SQLGetConnectAttr(hdbc, SQL_ATTR_CONNECTION_TIMEOUT, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(value, 60u);
}

TEST_F(ConnectionTest, SetGetConnectAttr_TxnIsolation) {
    // Connect first (some drivers require connection for isolation level)
    SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testuser")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("testpass")), SQL_NTS);
    
    SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_TXN_ISOLATION,
        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_TXN_SERIALIZABLE)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLUINTEGER value = 0;
    ret = SQLGetConnectAttr(hdbc, SQL_ATTR_TXN_ISOLATION, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(value, static_cast<SQLUINTEGER>(SQL_TXN_SERIALIZABLE));
}

TEST_F(ConnectionTest, SetGetConnectAttr_AccessMode) {
    SQLRETURN ret = SQLSetConnectAttr(hdbc, SQL_ATTR_ACCESS_MODE,
        reinterpret_cast<SQLPOINTER>(static_cast<intptr_t>(SQL_MODE_READ_ONLY)), 0);
    EXPECT_EQ(ret, SQL_SUCCESS);
    
    SQLUINTEGER value = 0;
    ret = SQLGetConnectAttr(hdbc, SQL_ATTR_ACCESS_MODE, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(value, static_cast<SQLUINTEGER>(SQL_MODE_READ_ONLY));
}

TEST_F(ConnectionTest, GetConnectAttr_InvalidHandle) {
    SQLUINTEGER value = 0;
    SQLRETURN ret = SQLGetConnectAttr(SQL_NULL_HDBC, SQL_ATTR_AUTOCOMMIT, &value, sizeof(value), nullptr);
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}

// ===== SQLGetInfo Tests =====

TEST_F(ConnectionTest, SQLGetInfo_DriverName) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLCHAR buffer[128] = {0};
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_DRIVER_NAME, buffer, sizeof(buffer), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_GT(len, 0);
    EXPECT_STREQ(reinterpret_cast<char*>(buffer), "mockodbc.dll");
}

TEST_F(ConnectionTest, SQLGetInfo_DBMSName) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLCHAR buffer[128] = {0};
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_DBMS_NAME, buffer, sizeof(buffer), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_GT(len, 0);
}

TEST_F(ConnectionTest, SQLGetInfo_MaxConnections) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLUSMALLINT maxConn = 0;
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_MAX_DRIVER_CONNECTIONS, &maxConn, sizeof(maxConn), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
}

TEST_F(ConnectionTest, SQLGetInfo_DataSourceName) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("MyDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLCHAR buffer[128] = {0};
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_DATA_SOURCE_NAME, buffer, sizeof(buffer), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(buffer), "MyDSN");
}

TEST_F(ConnectionTest, SQLGetInfo_UserName) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("admin")), SQL_NTS,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("secret")), SQL_NTS);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLCHAR buffer[128] = {0};
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_USER_NAME, buffer, sizeof(buffer), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(buffer), "admin");
}

TEST_F(ConnectionTest, SQLGetInfo_TxnCapable) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLUSMALLINT txnCap = 0;
    SQLSMALLINT len = 0;
    
    ret = SQLGetInfo(hdbc, SQL_TXN_CAPABLE, &txnCap, sizeof(txnCap), &len);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_EQ(txnCap, SQL_TC_ALL);
}

// ===== SQLNativeSql Tests =====

TEST_F(ConnectionTest, SQLNativeSql_Basic) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);
    
    SQLCHAR outSql[256] = {0};
    SQLINTEGER outLen = 0;
    
    ret = SQLNativeSql(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT * FROM users")), SQL_NTS,
        outSql, sizeof(outSql), &outLen);
    
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_GT(outLen, 0);
    EXPECT_STREQ(reinterpret_cast<char*>(outSql), "SELECT * FROM users");
}

// PORT plan port 4 — confirm SQLNativeSql actually translates ODBC escapes.
// Without translation, the input survives verbatim; with translation, the
// output differs and contains no `{fn` substring.
TEST_F(ConnectionTest, SQLNativeSql_TranslatesScalarFunctionEscape) {
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    ASSERT_EQ(ret, SQL_SUCCESS);

    SQLCHAR outSql[256] = {0};
    SQLINTEGER outLen = 0;
    const char* input = "SELECT {fn UCASE('hi')}";
    ret = SQLNativeSql(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(input)), SQL_NTS,
        outSql, sizeof(outSql), &outLen);
    EXPECT_EQ(ret, SQL_SUCCESS);
    std::string out(reinterpret_cast<char*>(outSql));
    EXPECT_NE(out.find("UPPER"), std::string::npos)
        << "Mock should translate {fn UCASE} → UPPER. got: " << out;
    EXPECT_EQ(out.find("{fn"), std::string::npos)
        << "Mock should remove the {fn brace. got: " << out;
}

// Same input, NativeSqlPassThrough=true → input survives verbatim.
TEST_F(ConnectionTest, SQLNativeSql_PassThroughKnobReturnsInputVerbatim) {
    SQLRETURN ret = SQLDriverConnect(hdbc, nullptr,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "Driver={Mock ODBC Driver};NativeSqlPassThrough=true;")),
        SQL_NTS, nullptr, 0, nullptr, SQL_DRIVER_NOPROMPT);
    ASSERT_TRUE(SQL_SUCCEEDED(ret));

    SQLCHAR outSql[256] = {0};
    SQLINTEGER outLen = 0;
    const char* input = "SELECT {fn UCASE('hi')}";
    ret = SQLNativeSql(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>(input)), SQL_NTS,
        outSql, sizeof(outSql), &outLen);
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_STREQ(reinterpret_cast<char*>(outSql), input)
        << "PassThrough must return input untouched (escape sequences and all).";
}

TEST_F(ConnectionTest, SQLNativeSql_InvalidHandle) {
    SQLCHAR outSql[256] = {0};
    SQLINTEGER outLen = 0;
    
    SQLRETURN ret = SQLNativeSql(SQL_NULL_HDBC,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("SELECT 1")), SQL_NTS,
        outSql, sizeof(outSql), &outLen);
    
    EXPECT_EQ(ret, SQL_INVALID_HANDLE);
}

// ===== Behavior Controller Tests =====

// D32 landed: SQLConnect consults should_fail() now, so this runs. It was
// DISABLED_ for exactly as long as the defect existed, with its assertions
// unchanged - which is what made dropping the prefix the regression test the
// row asked for rather than a new test written to match the fix.
TEST_F(ConnectionTest, SimulatedConnectionFailure) {
    // Configure to fail connections. There is no configure_failure() — the
    // real mechanism is Mode=Partial plus a FailOn list and an ErrorCode.
    DriverConfig cfg;
    cfg.mode = BehaviorMode::Partial;
    cfg.fail_on = {"SQLConnect"};
    cfg.error_code = "08001";
    BehaviorController::instance().set_config(cfg);
    
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    
    EXPECT_EQ(ret, SQL_ERROR);
    
    auto* conn = validate_dbc_handle(hdbc);
    ASSERT_NE(conn, nullptr);
    EXPECT_FALSE(conn->is_connected());
}

// D32 landed: SQLConnect calls apply_latency() and no longer overwrites the
// controller's config with a default-constructed one, so the Latency set here
// survives to be read.
TEST_F(ConnectionTest, SimulatedConnectionTimeout) {
    auto& ctrl = BehaviorController::instance();
    DriverConfig config;
    config.latency = std::chrono::milliseconds(100);  // 100ms delay
    ctrl.set_config(config);
    
    auto start = std::chrono::steady_clock::now();
    
    SQLRETURN ret = SQLConnect(hdbc,
        reinterpret_cast<SQLCHAR*>(const_cast<char*>("TestDSN")), SQL_NTS,
        nullptr, 0, nullptr, 0);
    
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    EXPECT_EQ(ret, SQL_SUCCESS);
    EXPECT_GE(elapsed, 90);  // Allow some timing variance
}

