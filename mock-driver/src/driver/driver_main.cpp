// Mock ODBC Driver - Main Entry Point and Handle Management API
// This file contains the DLL entry point and SQLAllocHandle/SQLFreeHandle implementations

#include "driver/entry_guard.hpp"
#include "driver/handles.hpp"
#include "utils/buffer_copy.hpp"
#include "utils/string_utils.hpp"
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
    SQLHANDLE* phOutput) MOCK_ENTRY_TRY {
    
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

            // Per the ODBC state table, the connection need only be in state
            // C2 (Allocated) for SQLAllocHandle(SQL_HANDLE_STMT) — the
            // connection does not have to be open yet. Previously we
            // returned 08003 here, which caused drivers that legitimately
            // pre-allocate statements to fail conformance.
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
            // D8(b): a descriptor the application asked for is
            // SQL_DESC_ALLOC_USER by definition, and that is what tells
            // SQLFreeHandle it may free it and ~StatementHandle that it must
            // not. It was left at the SQL_DESC_ALLOC_AUTO default, so the
            // driver could not tell an explicit descriptor from one of the
            // four it allocates for every statement - which is why freeing
            // either of them did the same thing.
            desc->alloc_type_ = SQL_DESC_ALLOC_USER;
            *phOutput = static_cast<SQLHANDLE>(desc);
            return SQL_SUCCESS;
        }
        
        default:
            return SQL_ERROR;
    }
}
MOCK_ENTRY_CATCH(hInput)

// SQLFreeHandle - Free a handle
SQLRETURN SQL_API SQLFreeHandle(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle) MOCK_ENTRY_TRY {
    
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

            // D8(b): this deleted any valid descriptor unconditionally,
            // including the four implicit ones every statement allocates.
            // An application that obtained one from
            // SQLGetStmtAttr(SQL_ATTR_APP_ROW_DESC) and then freed it left
            // `stmt->app_row_desc_` dangling, and ~StatementHandle deleted
            // it a second time.
            //
            // The spec is explicit: freeing an implicitly allocated
            // descriptor is HY017. `alloc_type_` is what tells them apart,
            // and it is the same field ~StatementHandle already consults.
            if (desc->alloc_type_ != SQL_DESC_ALLOC_USER) {
                desc->add_diagnostic(sqlstate::INVALID_USE_OF_AUTO_DESC, 0,
                                     "Invalid use of an automatically "
                                     "allocated descriptor handle");
                return SQL_ERROR;
            }

            delete desc;
            return SQL_SUCCESS;
        }
        
        default:
            return SQL_INVALID_HANDLE;
    }
}
MOCK_ENTRY_CATCH(hHandle)

// SQLGetEnvAttr - Get environment attribute
SQLRETURN SQL_API SQLGetEnvAttr(
    SQLHENV henv,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) MOCK_ENTRY_TRY {
    
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
MOCK_ENTRY_CATCH(henv)

// SQLSetEnvAttr - Set environment attribute
SQLRETURN SQL_API SQLSetEnvAttr(
    SQLHENV henv,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) MOCK_ENTRY_TRY {
    
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
MOCK_ENTRY_CATCH(henv)

// Legacy allocation functions (deprecated in ODBC 3.x but needed for compatibility)
SQLRETURN SQL_API SQLAllocEnv(SQLHENV* phenv) MOCK_ENTRY_TRY {
    return SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, phenv);
}
MOCK_ENTRY_CATCH(SQL_NULL_HANDLE)

SQLRETURN SQL_API SQLAllocConnect(SQLHENV henv, SQLHDBC* phdbc) MOCK_ENTRY_TRY {
    return SQLAllocHandle(SQL_HANDLE_DBC, henv, phdbc);
}
MOCK_ENTRY_CATCH(henv)

SQLRETURN SQL_API SQLAllocStmt(SQLHDBC hdbc, SQLHSTMT* phstmt) MOCK_ENTRY_TRY {
    return SQLAllocHandle(SQL_HANDLE_STMT, hdbc, phstmt);
}
MOCK_ENTRY_CATCH(hdbc)

SQLRETURN SQL_API SQLFreeEnv(SQLHENV henv) MOCK_ENTRY_TRY {
    return SQLFreeHandle(SQL_HANDLE_ENV, henv);
}
MOCK_ENTRY_CATCH(henv)

SQLRETURN SQL_API SQLFreeConnect(SQLHDBC hdbc) MOCK_ENTRY_TRY {
    return SQLFreeHandle(SQL_HANDLE_DBC, hdbc);
}
MOCK_ENTRY_CATCH(hdbc)

// Cursor name functions
SQLRETURN SQL_API SQLSetCursorName(
    SQLHSTMT hstmt,
    SQLCHAR* szCursor,
    SQLSMALLINT cbCursor) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    //
    // Measured on Windows: this guard *is* reached and *does* return
    // SQL_ERROR, and the application still sees SQL_SUCCESS - the driver
    // manager maintains cursor names itself and does not surface the
    // driver's refusal. The guard is kept for unixODBC, which forwards.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLSetCursorName")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLSetCursorName failure");
            return SQL_ERROR;
        }
    }

    // D29: the name was discarded, so set-then-get could not round-trip.
    stmt->cursor_name_ = sql_to_string(szCursor, cbCursor);
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetCursorName(
    SQLHSTMT hstmt,
    SQLCHAR* szCursor,
    SQLSMALLINT cbCursorMax,
    SQLSMALLINT* pcbCursor) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    // D29: this used to synthesise a name from the handle address, which
    // both failed to round-trip a name the application had set and leaked a
    // heap pointer into text applications put into SQL. Return what was set;
    // fall back to a stable per-statement name that reveals nothing.
    std::string cursor_name = stmt->cursor_name_;
    if (cursor_name.empty()) {
        cursor_name = "SQL_CUR" + std::to_string(stmt->cursor_ordinal_);
    }
    
    // D18/D24: a hand-rolled copy that signalled nothing on truncation.
    // Returning plain SQL_SUCCESS also made any diagnostic invisible: a
    // driver manager only surfaces the diagnostic stack when the call
    // returns SQL_SUCCESS_WITH_INFO or an error, so posting 01004 beside a
    // SQL_SUCCESS would have changed nothing an application could see.
    const BufferCopyResult cursor_copy = copy_chars(
        cursor_name, 0, szCursor, static_cast<SQLLEN>(cbCursorMax));

    if (pcbCursor) {
        *pcbCursor = static_cast<SQLSMALLINT>(cursor_copy.remaining);
    }

    if (cursor_copy.truncated) {
        stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                             "String data, right truncated");
        return SQL_SUCCESS_WITH_INFO;
    }
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

// Extended fetch and scroll (for advanced cursor support)
SQLRETURN SQL_API SQLFetchScroll(
    SQLHSTMT hstmt,
    SQLSMALLINT fFetchType,
    SQLLEN iRow) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLFetchScroll")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLFetchScroll failure");
            return SQL_ERROR;
        }
    }
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLGetCursorName")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLGetCursorName failure");
            return SQL_ERROR;
        }
    }
    
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
            // D34
            long long value = apply_numeric_skew(std::get<long long>(cell),
                                                 FetchPath::BoundColumn);
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
            // D34
            double value = apply_numeric_skew(std::get<double>(cell),
                                              FetchPath::BoundColumn);
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
MOCK_ENTRY_CATCH(hstmt)

// Bulk operations (stub)
SQLRETURN SQL_API SQLBulkOperations(
    SQLHSTMT hstmt,
    SQLSMALLINT Operation) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)Operation;
    
    stmt->add_diagnostic(sqlstate::OPTIONAL_FEATURE_NOT_IMPLEMENTED, 0,
                        "Bulk operations not supported");
    return SQL_ERROR;
}
MOCK_ENTRY_CATCH(hstmt)

// SetPos (stub)
SQLRETURN SQL_API SQLSetPos(
    SQLHSTMT hstmt,
    SQLSETPOSIROW iRow,
    SQLUSMALLINT fOption,
    SQLUSMALLINT fLock) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)iRow;
    (void)fOption;
    (void)fLock;
    
    stmt->add_diagnostic(sqlstate::OPTIONAL_FEATURE_NOT_IMPLEMENTED, 0,
                        "SQLSetPos not supported");
    return SQL_ERROR;
}
MOCK_ENTRY_CATCH(hstmt)

// ParamData/PutData for long data
SQLRETURN SQL_API SQLParamData(
    SQLHSTMT hstmt,
    SQLPOINTER* prgbValue) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)prgbValue;
    
    // Mock: no data-at-execution parameters
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLPutData(
    SQLHSTMT hstmt,
    SQLPOINTER rgbValue,
    SQLLEN cbValue) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)rgbValue;
    (void)cbValue;
    
    // Mock: accept but don't use
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

} // extern "C"
