// Mock ODBC Driver - Main Entry Point and Handle Management API
// This file contains the DLL entry point and SQLAllocHandle/SQLFreeHandle implementations

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            // Initialize default catalog
            mock_odbc::MockCatalog::instance().initialize("Default");
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}

#else
// Linux/macOS constructor/destructor
__attribute__((constructor))
static void driver_init() {
    mock_odbc::MockCatalog::instance().initialize("Default");
}

__attribute__((destructor))
static void driver_fini() {
}
#endif

using namespace mock_odbc;

extern "C" {

// SQLAllocHandle - Allocate a handle
SQLRETURN SQL_API SQLAllocHandle(
    SQLSMALLINT fHandleType,
    SQLHANDLE hInput,
    SQLHANDLE* phOutput) {
    
    if (!phOutput) {
        return SQL_ERROR;
    }
    
    *phOutput = SQL_NULL_HANDLE;
    
    switch (fHandleType) {
        case SQL_HANDLE_ENV: {
            // Input must be SQL_NULL_HANDLE for environment
            if (hInput != SQL_NULL_HANDLE) {
                return SQL_ERROR;
            }
            
            auto* env = new EnvironmentHandle();
            *phOutput = static_cast<SQLHANDLE>(env);
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_DBC: {
            auto* env = validate_env_handle(hInput);
            if (!env) {
                return SQL_INVALID_HANDLE;
            }
            
            auto* conn = new ConnectionHandle(env);
            *phOutput = static_cast<SQLHANDLE>(conn);
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_STMT: {
            auto* conn = validate_dbc_handle(hInput);
            if (!conn) {
                return SQL_INVALID_HANDLE;
            }
            
            if (!conn->is_connected()) {
                conn->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                                    "Connection not open");
                return SQL_ERROR;
            }
            
            auto* stmt = new StatementHandle(conn);
            *phOutput = static_cast<SQLHANDLE>(stmt);
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_DESC: {
            auto* conn = validate_dbc_handle(hInput);
            if (!conn) {
                return SQL_INVALID_HANDLE;
            }
            
            auto* desc = new DescriptorHandle(conn, true);
            *phOutput = static_cast<SQLHANDLE>(desc);
            return SQL_SUCCESS;
        }
        
        default:
            return SQL_ERROR;
    }
}

// SQLFreeHandle - Free a handle
SQLRETURN SQL_API SQLFreeHandle(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle) {
    
    switch (fHandleType) {
        case SQL_HANDLE_ENV: {
            auto* env = validate_env_handle(hHandle);
            if (!env) return SQL_INVALID_HANDLE;
            
            // Check for allocated connections
            if (!env->connections_.empty()) {
                env->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                                   "Connection handles still allocated");
                return SQL_ERROR;
            }
            
            delete env;
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_DBC: {
            auto* conn = validate_dbc_handle(hHandle);
            if (!conn) return SQL_INVALID_HANDLE;
            
            // Check for open connection
            if (conn->is_connected()) {
                conn->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                                    "Connection still open");
                return SQL_ERROR;
            }
            
            // Check for allocated statements
            if (!conn->statements_.empty()) {
                conn->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                                    "Statement handles still allocated");
                return SQL_ERROR;
            }
            
            delete conn;
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_STMT: {
            auto* stmt = validate_stmt_handle(hHandle);
            if (!stmt) return SQL_INVALID_HANDLE;
            
            delete stmt;
            return SQL_SUCCESS;
        }
        
        case SQL_HANDLE_DESC: {
            auto* desc = validate_desc_handle(hHandle);
            if (!desc) return SQL_INVALID_HANDLE;
            
            delete desc;
            return SQL_SUCCESS;
        }
        
        default:
            return SQL_INVALID_HANDLE;
    }
}

// SQLGetEnvAttr - Get environment attribute
SQLRETURN SQL_API SQLGetEnvAttr(
    SQLHENV henv,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) {
    
    (void)cbValueMax;
    
    auto* env = validate_env_handle(henv);
    if (!env) return SQL_INVALID_HANDLE;
    
    switch (fAttribute) {
        case SQL_ATTR_ODBC_VERSION:
            if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = env->odbc_version_;
            if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
            break;
            
        case SQL_ATTR_CONNECTION_POOLING:
            if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = env->connection_pooling_;
            if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
            break;
            
        case SQL_ATTR_CP_MATCH:
            if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = env->cp_match_;
            if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
            break;
            
        case SQL_ATTR_OUTPUT_NTS:
            if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = env->output_nts_;
            if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
            break;
            
        default:
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}

// SQLSetEnvAttr - Set environment attribute
SQLRETURN SQL_API SQLSetEnvAttr(
    SQLHENV henv,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) {
    
    (void)cbValue;
    
    auto* env = validate_env_handle(henv);
    if (!env) return SQL_INVALID_HANDLE;
    
    SQLINTEGER value = static_cast<SQLINTEGER>(reinterpret_cast<intptr_t>(rgbValue));
    
    switch (fAttribute) {
        case SQL_ATTR_ODBC_VERSION:
            env->odbc_version_ = value;
            break;
            
        case SQL_ATTR_CONNECTION_POOLING:
            env->connection_pooling_ = value;
            break;
            
        case SQL_ATTR_CP_MATCH:
            env->cp_match_ = value;
            break;
            
        case SQL_ATTR_OUTPUT_NTS:
            env->output_nts_ = value;
            break;
            
        default:
            // Ignore unknown attributes for compatibility
            break;
    }
    
    return SQL_SUCCESS;
}

// Legacy allocation functions (deprecated in ODBC 3.x but needed for compatibility)
SQLRETURN SQL_API SQLAllocEnv(SQLHENV* phenv) {
    return SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, phenv);
}

SQLRETURN SQL_API SQLAllocConnect(SQLHENV henv, SQLHDBC* phdbc) {
    return SQLAllocHandle(SQL_HANDLE_DBC, henv, phdbc);
}

SQLRETURN SQL_API SQLAllocStmt(SQLHDBC hdbc, SQLHSTMT* phstmt) {
    return SQLAllocHandle(SQL_HANDLE_STMT, hdbc, phstmt);
}

SQLRETURN SQL_API SQLFreeEnv(SQLHENV henv) {
    return SQLFreeHandle(SQL_HANDLE_ENV, henv);
}

SQLRETURN SQL_API SQLFreeConnect(SQLHDBC hdbc) {
    return SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
}

// Cursor name functions
SQLRETURN SQL_API SQLSetCursorName(
    SQLHSTMT hstmt,
    SQLCHAR* szCursor,
    SQLSMALLINT cbCursor) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)szCursor;
    (void)cbCursor;
    
    // Mock: accept but don't use cursor name
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetCursorName(
    SQLHSTMT hstmt,
    SQLCHAR* szCursor,
    SQLSMALLINT cbCursorMax,
    SQLSMALLINT* pcbCursor) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    // Return a generated cursor name
    std::string cursor_name = "SQL_CUR" + std::to_string(reinterpret_cast<uintptr_t>(stmt));
    
    if (szCursor && cbCursorMax > 0) {
        size_t copy_len = std::min(cursor_name.length(), 
                                   static_cast<size_t>(cbCursorMax - 1));
        std::memcpy(szCursor, cursor_name.c_str(), copy_len);
        szCursor[copy_len] = '\0';
    }
    
    if (pcbCursor) {
        *pcbCursor = static_cast<SQLSMALLINT>(cursor_name.length());
    }
    
    return SQL_SUCCESS;
}

// Extended fetch and scroll (for advanced cursor support)
SQLRETURN SQL_API SQLFetchScroll(
    SQLHSTMT hstmt,
    SQLSMALLINT fFetchType,
    SQLLEN iRow) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)iRow;
    
    // For forward-only cursor, only SQL_FETCH_NEXT is supported
    if (fFetchType != SQL_FETCH_NEXT) {
        stmt->add_diagnostic(sqlstate::OPTIONAL_FEATURE_NOT_IMPLEMENTED, 0,
                            "Only SQL_FETCH_NEXT is supported");
        return SQL_ERROR;
    }
    
    // Forward to regular fetch
    return SQLFetch(hstmt);
}

// Bulk operations (stub)
SQLRETURN SQL_API SQLBulkOperations(
    SQLHSTMT hstmt,
    SQLSMALLINT Operation) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)Operation;
    
    stmt->add_diagnostic(sqlstate::OPTIONAL_FEATURE_NOT_IMPLEMENTED, 0,
                        "Bulk operations not supported");
    return SQL_ERROR;
}

// SetPos (stub)
SQLRETURN SQL_API SQLSetPos(
    SQLHSTMT hstmt,
    SQLSETPOSIROW iRow,
    SQLUSMALLINT fOption,
    SQLUSMALLINT fLock) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)iRow;
    (void)fOption;
    (void)fLock;
    
    stmt->add_diagnostic(sqlstate::OPTIONAL_FEATURE_NOT_IMPLEMENTED, 0,
                        "SQLSetPos not supported");
    return SQL_ERROR;
}

// ParamData/PutData for long data
SQLRETURN SQL_API SQLParamData(
    SQLHSTMT hstmt,
    SQLPOINTER* prgbValue) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)prgbValue;
    
    // Mock: no data-at-execution parameters
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPutData(
    SQLHSTMT hstmt,
    SQLPOINTER rgbValue,
    SQLLEN cbValue) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)rgbValue;
    (void)cbValue;
    
    // Mock: accept but don't use
    return SQL_SUCCESS;
}

} // extern "C"
