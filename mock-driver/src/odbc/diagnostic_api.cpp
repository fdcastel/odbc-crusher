// Diagnostic API - SQLGetDiagRec, SQLGetDiagField

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "utils/string_utils.hpp"
#include <cstring>

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLGetDiagRec(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT iRecord,
    SQLCHAR* szSqlState,
    SQLINTEGER* pfNativeError,
    SQLCHAR* szErrorMsg,
    SQLSMALLINT cbErrorMsgMax,
    SQLSMALLINT* pcbErrorMsg) {
    
    OdbcHandle* handle = nullptr;
    
    switch (fHandleType) {
        case SQL_HANDLE_ENV:
            handle = validate_env_handle(hHandle);
            break;
        case SQL_HANDLE_DBC:
            handle = validate_dbc_handle(hHandle);
            break;
        case SQL_HANDLE_STMT:
            handle = validate_stmt_handle(hHandle);
            break;
        case SQL_HANDLE_DESC:
            handle = validate_desc_handle(hHandle);
            break;
        default:
            return SQL_INVALID_HANDLE;
    }
    
    if (!handle) return SQL_INVALID_HANDLE;
    
    const DiagnosticRecord* rec = handle->get_diagnostic(iRecord);
    if (!rec) {
        return SQL_NO_DATA;
    }
    
    // Copy SQLSTATE
    if (szSqlState) {
        std::memcpy(szSqlState, rec->sqlstate.c_str(), 5);
        szSqlState[5] = '\0';
    }
    
    // Copy native error
    if (pfNativeError) {
        *pfNativeError = rec->native_error;
    }
    
    // Copy message
    SQLRETURN ret = SQL_SUCCESS;
    if (szErrorMsg && cbErrorMsgMax > 0) {
        ret = copy_string_to_buffer(rec->message, szErrorMsg, cbErrorMsgMax, pcbErrorMsg);
    } else if (pcbErrorMsg) {
        *pcbErrorMsg = static_cast<SQLSMALLINT>(rec->message.length());
    }
    
    return ret;
}

SQLRETURN SQL_API SQLGetDiagField(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT iRecord,
    SQLSMALLINT fDiagField,
    SQLPOINTER rgbDiagInfo,
    SQLSMALLINT cbDiagInfoMax,
    SQLSMALLINT* pcbDiagInfo) {
    
    OdbcHandle* handle = nullptr;
    
    switch (fHandleType) {
        case SQL_HANDLE_ENV:
            handle = validate_env_handle(hHandle);
            break;
        case SQL_HANDLE_DBC:
            handle = validate_dbc_handle(hHandle);
            break;
        case SQL_HANDLE_STMT:
            handle = validate_stmt_handle(hHandle);
            break;
        case SQL_HANDLE_DESC:
            handle = validate_desc_handle(hHandle);
            break;
        default:
            return SQL_INVALID_HANDLE;
    }
    
    if (!handle) return SQL_INVALID_HANDLE;
    
    // Header fields (iRecord = 0)
    if (iRecord == 0) {
        switch (fDiagField) {
            case SQL_DIAG_NUMBER:
                if (rgbDiagInfo) {
                    *static_cast<SQLINTEGER*>(rgbDiagInfo) = 
                        static_cast<SQLINTEGER>(handle->diagnostic_count());
                }
                if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLINTEGER);
                return SQL_SUCCESS;
                
            case SQL_DIAG_RETURNCODE:
                if (rgbDiagInfo) {
                    *static_cast<SQLRETURN*>(rgbDiagInfo) = handle->return_code_;
                }
                if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLRETURN);
                return SQL_SUCCESS;
                
            case SQL_DIAG_CURSOR_ROW_COUNT:
                if (rgbDiagInfo) {
                    *static_cast<SQLLEN*>(rgbDiagInfo) = handle->cursor_row_count_;
                }
                if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLLEN);
                return SQL_SUCCESS;
                
            case SQL_DIAG_ROW_COUNT:
                if (rgbDiagInfo) {
                    *static_cast<SQLLEN*>(rgbDiagInfo) = handle->row_count_;
                }
                if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLLEN);
                return SQL_SUCCESS;
                
            case SQL_DIAG_DYNAMIC_FUNCTION:
                return copy_string_to_buffer(handle->dynamic_function_,
                                            static_cast<SQLCHAR*>(rgbDiagInfo),
                                            cbDiagInfoMax, pcbDiagInfo);
                
            case SQL_DIAG_DYNAMIC_FUNCTION_CODE:
                if (rgbDiagInfo) {
                    *static_cast<SQLINTEGER*>(rgbDiagInfo) = handle->dynamic_function_code_;
                }
                if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLINTEGER);
                return SQL_SUCCESS;
                
            default:
                return SQL_ERROR;
        }
    }
    
    // Record fields
    const DiagnosticRecord* rec = handle->get_diagnostic(iRecord);
    if (!rec) {
        return SQL_NO_DATA;
    }
    
    switch (fDiagField) {
        case SQL_DIAG_SQLSTATE:
            return copy_string_to_buffer(rec->sqlstate,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_NATIVE:
            if (rgbDiagInfo) {
                *static_cast<SQLINTEGER*>(rgbDiagInfo) = rec->native_error;
            }
            if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLINTEGER);
            return SQL_SUCCESS;
            
        case SQL_DIAG_MESSAGE_TEXT:
            return copy_string_to_buffer(rec->message,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_CLASS_ORIGIN:
            return copy_string_to_buffer(rec->class_origin,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_SUBCLASS_ORIGIN:
            return copy_string_to_buffer(rec->subclass_origin,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_CONNECTION_NAME:
            return copy_string_to_buffer(rec->connection_name,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_SERVER_NAME:
            return copy_string_to_buffer(rec->server_name,
                                        static_cast<SQLCHAR*>(rgbDiagInfo),
                                        cbDiagInfoMax, pcbDiagInfo);
            
        case SQL_DIAG_COLUMN_NUMBER:
            if (rgbDiagInfo) {
                *static_cast<SQLINTEGER*>(rgbDiagInfo) = rec->column_number;
            }
            if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLINTEGER);
            return SQL_SUCCESS;
            
        case SQL_DIAG_ROW_NUMBER:
            if (rgbDiagInfo) {
                *static_cast<SQLLEN*>(rgbDiagInfo) = rec->row_number;
            }
            if (pcbDiagInfo) *pcbDiagInfo = sizeof(SQLLEN);
            return SQL_SUCCESS;
            
        default:
            return SQL_ERROR;
    }
}

} // extern "C"
