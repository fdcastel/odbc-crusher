#include <gtest/gtest.h>
#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include <cstdlib>

using namespace odbc_crusher::core;

class OdbcConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<OdbcEnvironment>();
    }
    
    std::unique_ptr<OdbcEnvironment> env;
};

TEST_F(OdbcConnectionTest, ConstructorDoesNotThrow) {
    EXPECT_NO_THROW({
        OdbcConnection conn(*env);
    });
}

TEST_F(OdbcConnectionTest, GetHandleReturnsNonNull) {
    OdbcConnection conn(*env);
    EXPECT_NE(conn.get_handle(), static_cast<SQLHDBC>(SQL_NULL_HDBC));
}

TEST_F(OdbcConnectionTest, InitiallyNotConnected) {
    OdbcConnection conn(*env);
    EXPECT_FALSE(conn.is_connected());
}

TEST_F(OdbcConnectionTest, ConnectWithFirebird) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "FIREBIRD_ODBC_CONNECTION not set";
    }
    
    OdbcConnection conn(*env);
    EXPECT_NO_THROW(conn.connect(conn_str));
    EXPECT_TRUE(conn.is_connected());
}

TEST_F(OdbcConnectionTest, ConnectWithMySQL) {
    const char* conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    if (!conn_str) {
        GTEST_SKIP() << "MYSQL_ODBC_CONNECTION not set";
    }
    
    OdbcConnection conn(*env);
    EXPECT_NO_THROW(conn.connect(conn_str));
    EXPECT_TRUE(conn.is_connected());
}

TEST_F(OdbcConnectionTest, Disconnect) {
    const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
    if (!conn_str) {
        conn_str = std::getenv("MYSQL_ODBC_CONNECTION");
    }
    if (!conn_str) {
        GTEST_SKIP() << "No ODBC connection string available";
    }
    
    OdbcConnection conn(*env);
    conn.connect(conn_str);
    EXPECT_TRUE(conn.is_connected());
    
    EXPECT_NO_THROW(conn.disconnect());
    EXPECT_FALSE(conn.is_connected());
}
