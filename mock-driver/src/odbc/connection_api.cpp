// Connection API - SQLConnect, SQLDriverConnect, SQLDisconnect, etc.

#include "driver/handles.hpp"
#include "driver/config.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLConnect(
    SQLHDBC hdbc,
    SQLCHAR* szDSN,
    SQLSMALLINT cbDSN,
    SQLCHAR* szUID,
    SQLSMALLINT cbUID,
    SQLCHAR* szPWD,
    SQLSMALLINT cbPWD) {
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    
    conn->clear_diagnostics();
    
    if (conn->connected_) {
        conn->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0, 
                            "Connection already open");
        return SQL_ERROR;
    }
    
    conn->dsn_ = sql_to_string(szDSN, cbDSN);
    conn->uid_ = sql_to_string(szUID, cbUID);
    conn->pwd_ = sql_to_string(szPWD, cbPWD);
    
    // Build a connection string for configuration
    conn->connection_string_ = "DSN=" + conn->dsn_ + ";UID=" + conn->uid_ + ";";
    
    // Parse configuration (use defaults for simple connect)
    DriverConfig config;
    BehaviorController::instance().set_config(config);
    MockCatalog::instance().initialize(config.catalog);
    
    conn->connected_ = true;
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDriverConnect(
    SQLHDBC hdbc,
    SQLHWND hwnd,
    SQLCHAR* szConnStrIn,
    SQLSMALLINT cbConnStrIn,
    SQLCHAR* szConnStrOut,
    SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut,
    SQLUSMALLINT fDriverCompletion) {
    
    (void)hwnd;
    (void)fDriverCompletion;
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    
    conn->clear_diagnostics();
    
    if (conn->connected_) {
        conn->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Connection already open");
        return SQL_ERROR;
    }
    
    conn->connection_string_ = sql_to_string(szConnStrIn, cbConnStrIn);
    
    // Parse configuration from connection string
    DriverConfig config = parse_connection_string(conn->connection_string_);
    
    // Check if we should fail
    if (config.should_fail("SQLDriverConnect")) {
        conn->add_diagnostic(config.error_code, 0, "Simulated connection failure");
        return SQL_ERROR;
    }
    
    // Apply latency
    config.apply_latency();
    
    // Check max connections
    if (config.max_connections > 0) {
        auto* env = conn->environment();
        if (env && static_cast<int>(env->connections_.size()) >= config.max_connections) {
            conn->add_diagnostic(sqlstate::CONNECTION_FAILURE, 0,
                                "Maximum connections exceeded");
            return SQL_ERROR;
        }
    }
    
    // Store configuration
    BehaviorController::instance().set_config(config);
    
    // Initialize catalog
    MockCatalog::instance().initialize(config.catalog);
    
    // Set up transaction mode
    if (config.transaction_mode == "ReadOnly") {
        conn->access_mode_ = SQL_MODE_READ_ONLY;
    }
    if (config.transaction_mode == "Manual") {
        conn->autocommit_ = SQL_AUTOCOMMIT_OFF;
    }
    conn->txn_isolation_ = config.isolation_level;
    
    conn->connected_ = true;
    
    // Output connection string
    if (szConnStrOut && cbConnStrOutMax > 0) {
        copy_string_to_buffer(conn->connection_string_, szConnStrOut, 
                              cbConnStrOutMax, pcbConnStrOut);
    } else if (pcbConnStrOut) {
        *pcbConnStrOut = static_cast<SQLSMALLINT>(conn->connection_string_.length());
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDisconnect(SQLHDBC hdbc) {
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    
    conn->clear_diagnostics();
    
    if (!conn->connected_) {
        conn->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    // Check for open statements with transactions
    // (simplified - just close all statements)
    for (auto* stmt : conn->statements_) {
        stmt->cursor_open_ = false;
        stmt->executed_ = false;
    }
    
    conn->connected_ = false;
    conn->connection_string_.clear();
    conn->dsn_.clear();
    conn->uid_.clear();
    conn->pwd_.clear();
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetConnectAttr(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) {
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    HandleLock lock(conn);
    
    conn->clear_diagnostics();
    
    switch (fAttribute) {
        case SQL_ATTR_ACCESS_MODE:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = conn->access_mode_;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_AUTOCOMMIT:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = conn->autocommit_;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_CONNECTION_TIMEOUT:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = conn->connection_timeout_;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_LOGIN_TIMEOUT:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = conn->login_timeout_;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_TXN_ISOLATION:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = conn->txn_isolation_;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_CONNECTION_DEAD:
            if (rgbValue) *static_cast<SQLUINTEGER*>(rgbValue) = 
                conn->is_connected() ? SQL_CD_FALSE : SQL_CD_TRUE;
            if (pcbValue) *pcbValue = sizeof(SQLUINTEGER);
            break;
            
        case SQL_ATTR_CURRENT_CATALOG:
            if (rgbValue && cbValueMax > 0) {
                copy_string_to_buffer(conn->current_catalog_name_,
                                      static_cast<SQLCHAR*>(rgbValue),
                                      static_cast<SQLSMALLINT>(cbValueMax), nullptr);
            }
            if (pcbValue) *pcbValue = static_cast<SQLINTEGER>(conn->current_catalog_name_.length());
            break;
            
        default:
            conn->add_diagnostic(sqlstate::INVALID_ATTRIBUTE_VALUE, 0,
                                "Unknown connection attribute");
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetConnectAttr(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) {
    
    (void)cbValue;
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    HandleLock lock(conn);
    
    conn->clear_diagnostics();
    
    switch (fAttribute) {
        case SQL_ATTR_ACCESS_MODE:
            conn->access_mode_ = reinterpret_cast<SQLUINTEGER>(rgbValue);
            break;
            
        case SQL_ATTR_AUTOCOMMIT:
            conn->autocommit_ = reinterpret_cast<SQLUINTEGER>(rgbValue);
            break;
            
        case SQL_ATTR_CONNECTION_TIMEOUT:
            conn->connection_timeout_ = reinterpret_cast<SQLUINTEGER>(rgbValue);
            break;
            
        case SQL_ATTR_LOGIN_TIMEOUT:
            conn->login_timeout_ = reinterpret_cast<SQLUINTEGER>(rgbValue);
            break;
            
        case SQL_ATTR_TXN_ISOLATION:
            conn->txn_isolation_ = reinterpret_cast<SQLUINTEGER>(rgbValue);
            break;
            
        case SQL_ATTR_CONNECTION_DEAD:
            // Read-only attribute
            conn->add_diagnostic(sqlstate::INVALID_ATTRIBUTE_VALUE, 0,
                                "SQL_ATTR_CONNECTION_DEAD is read-only");
            return SQL_ERROR;
            
        default:
            conn->add_diagnostic(sqlstate::INVALID_HANDLE_TYPE, 0,
                                "Invalid attribute/option identifier");
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLBrowseConnect(
    SQLHDBC hdbc,
    SQLCHAR* szConnStrIn,
    SQLSMALLINT cbConnStrIn,
    SQLCHAR* szConnStrOut,
    SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut) {
    
    // Simplified: just forward to SQLDriverConnect
    return SQLDriverConnect(hdbc, nullptr, szConnStrIn, cbConnStrIn,
                           szConnStrOut, cbConnStrOutMax, pcbConnStrOut,
                           SQL_DRIVER_NOPROMPT);
}

} // extern "C"
