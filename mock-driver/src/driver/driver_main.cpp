// Mock ODBC Driver - Main Entry Point and Handle Management API
// This file contains the DLL entry point and SQLAllocHandle/SQLFreeHandle implementations

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include <cstring>
#include <string>
#include <variant>

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
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    if (!stmt->executed_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor is not open");
        return SQL_ERROR;
    }
    
    // For forward-only cursor, only SQL_FETCH_NEXT is supported
    if (stmt->cursor_type_ == SQL_CURSOR_FORWARD_ONLY && fFetchType != SQL_FETCH_NEXT) {
        stmt->add_diagnostic("HY106", 0,
                            "Fetch type out of range for forward-only cursor");
        return SQL_ERROR;
    }
    
    SQLLEN new_row = stmt->current_row_;
    SQLLEN total_rows = static_cast<SQLLEN>(stmt->result_data_.size());
    
    switch (fFetchType) {
        case SQL_FETCH_NEXT:
            new_row = stmt->current_row_ + 1;
            break;
        case SQL_FETCH_FIRST:
            new_row = 0;
            break;
        case SQL_FETCH_LAST:
            new_row = total_rows - 1;
            break;
        case SQL_FETCH_ABSOLUTE:
            if (iRow > 0) {
                new_row = iRow - 1;  // 1-based to 0-based
            } else if (iRow < 0) {
                new_row = total_rows + iRow;  // Negative = from end
            } else {
                // iRow == 0: before first row
                new_row = -1;
            }
            break;
        case SQL_FETCH_RELATIVE:
            new_row = stmt->current_row_ + iRow;
            break;
        default:
            stmt->add_diagnostic("HY106", 0, "Fetch type out of range");
            return SQL_ERROR;
    }
    
    // Check bounds
    if (new_row < 0 || new_row >= total_rows) {
        stmt->current_row_ = (new_row < 0) ? -1 : total_rows;
        return SQL_NO_DATA;
    }
    
    stmt->current_row_ = new_row;
    stmt->cursor_open_ = true;
    
    // Transfer data to bound columns (same logic as SQLFetch)
    const auto& row = stmt->result_data_[stmt->current_row_];
    
    for (const auto& [col_num, binding] : stmt->column_bindings_) {
        if (col_num < 1 || col_num > static_cast<SQLUSMALLINT>(row.size())) {
            continue;
        }
        
        const auto& cell = row[col_num - 1];
        
        // Handle NULL
        if (std::holds_alternative<std::monostate>(cell)) {
            if (binding.str_len_or_ind) {
                *binding.str_len_or_ind = SQL_NULL_DATA;
            }
            continue;
        }
        
        // Convert and copy data based on target type
        if (std::holds_alternative<long long>(cell)) {
            long long value = std::get<long long>(cell);
            switch (binding.target_type) {
                case SQL_C_SLONG:
                case SQL_C_LONG:
                    if (binding.target_value)
                        *static_cast<SQLINTEGER*>(binding.target_value) = static_cast<SQLINTEGER>(value);
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = sizeof(SQLINTEGER);
                    break;
                case SQL_C_SBIGINT:
                    if (binding.target_value) *static_cast<SQLBIGINT*>(binding.target_value) = value;
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = sizeof(SQLBIGINT);
                    break;
                case SQL_C_SSHORT:
                    if (binding.target_value)
                        *static_cast<SQLSMALLINT*>(binding.target_value) = static_cast<SQLSMALLINT>(value);
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = sizeof(SQLSMALLINT);
                    break;
                case SQL_C_CHAR:
                default: {
                    std::string str = std::to_string(value);
                    if (binding.target_value && binding.buffer_length > 0) {
                        size_t copy_len = std::min(str.length(), static_cast<size_t>(binding.buffer_length - 1));
                        std::memcpy(binding.target_value, str.c_str(), copy_len);
                        static_cast<char*>(binding.target_value)[copy_len] = '\0';
                    }
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = static_cast<SQLLEN>(str.length());
                    break;
                }
            }
        } else if (std::holds_alternative<double>(cell)) {
            double value = std::get<double>(cell);
            switch (binding.target_type) {
                case SQL_C_DOUBLE:
                    if (binding.target_value) *static_cast<SQLDOUBLE*>(binding.target_value) = value;
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = sizeof(SQLDOUBLE);
                    break;
                case SQL_C_FLOAT:
                    if (binding.target_value) *static_cast<SQLREAL*>(binding.target_value) = static_cast<SQLREAL>(value);
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = sizeof(SQLREAL);
                    break;
                case SQL_C_CHAR:
                default: {
                    std::string str = std::to_string(value);
                    if (binding.target_value && binding.buffer_length > 0) {
                        size_t copy_len = std::min(str.length(), static_cast<size_t>(binding.buffer_length - 1));
                        std::memcpy(binding.target_value, str.c_str(), copy_len);
                        static_cast<char*>(binding.target_value)[copy_len] = '\0';
                    }
                    if (binding.str_len_or_ind) *binding.str_len_or_ind = static_cast<SQLLEN>(str.length());
                    break;
                }
            }
        } else if (std::holds_alternative<std::string>(cell)) {
            const std::string& value = std::get<std::string>(cell);
            if (binding.target_value && binding.buffer_length > 0) {
                size_t copy_len = std::min(value.length(), static_cast<size_t>(binding.buffer_length - 1));
                std::memcpy(binding.target_value, value.c_str(), copy_len);
                static_cast<char*>(binding.target_value)[copy_len] = '\0';
            }
            if (binding.str_len_or_ind) *binding.str_len_or_ind = static_cast<SQLLEN>(value.length());
        }
    }
    
    return SQL_SUCCESS;
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
