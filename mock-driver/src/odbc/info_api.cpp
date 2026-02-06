// Info API - SQLGetInfo, SQLGetTypeInfo, SQLGetFunctions

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_types.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include <cstring>

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLGetInfo(
    SQLHDBC hdbc,
    SQLUSMALLINT fInfoType,
    SQLPOINTER rgbInfoValue,
    SQLSMALLINT cbInfoValueMax,
    SQLSMALLINT* pcbInfoValue) {
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    HandleLock lock(conn);
    
    conn->clear_diagnostics();
    
    const auto& config = BehaviorController::instance().config();
    
    #define RETURN_STRING(s) \
        return copy_string_to_buffer(s, static_cast<SQLCHAR*>(rgbInfoValue), \
                                     cbInfoValueMax, pcbInfoValue)
    
    #define RETURN_USHORT(v) \
        if (rgbInfoValue) *static_cast<SQLUSMALLINT*>(rgbInfoValue) = (v); \
        if (pcbInfoValue) *pcbInfoValue = sizeof(SQLUSMALLINT); \
        return SQL_SUCCESS
    
    #define RETURN_ULONG(v) \
        if (rgbInfoValue) *static_cast<SQLUINTEGER*>(rgbInfoValue) = (v); \
        if (pcbInfoValue) *pcbInfoValue = sizeof(SQLUINTEGER); \
        return SQL_SUCCESS
    
    switch (fInfoType) {
        // Driver Information
        case SQL_DRIVER_NAME:
            RETURN_STRING("mockodbc.dll");
            
        case SQL_DRIVER_VER:
            RETURN_STRING(config.driver_version);
            
        case SQL_DRIVER_ODBC_VER:
            RETURN_STRING(config.driver_odbc_version);
            
        case SQL_ODBC_VER:
            RETURN_STRING("03.80.0000");
            
        // DBMS Information
        case SQL_DBMS_NAME:
            RETURN_STRING(config.dbms_name);
            
        case SQL_DBMS_VER:
            RETURN_STRING(config.dbms_version);
            
        case SQL_SERVER_NAME:
            RETURN_STRING("MockDBServer");
            
        // Data Source Information
        case SQL_DATA_SOURCE_NAME:
            RETURN_STRING(conn->dsn_);
            
        case SQL_DATA_SOURCE_READ_ONLY:
            RETURN_STRING(conn->access_mode_ == SQL_MODE_READ_ONLY ? "Y" : "N");
            
        case SQL_DATABASE_NAME:
            RETURN_STRING("MockDatabase");
            
        case SQL_USER_NAME:
            RETURN_STRING(conn->uid_);
            
        // Supported SQL
        case SQL_SQL_CONFORMANCE:
            RETURN_ULONG(SQL_SC_SQL92_INTERMEDIATE);
            
        case SQL_ODBC_SQL_CONFORMANCE:
            RETURN_USHORT(SQL_OSC_CORE);
            
        // Cursor Characteristics
        case SQL_CURSOR_COMMIT_BEHAVIOR:
            RETURN_USHORT(SQL_CB_CLOSE);
            
        case SQL_CURSOR_ROLLBACK_BEHAVIOR:
            RETURN_USHORT(SQL_CB_CLOSE);
            
        case SQL_CURSOR_SENSITIVITY:
            RETURN_ULONG(SQL_INSENSITIVE);
            
        case SQL_SCROLL_OPTIONS:
            RETURN_ULONG(SQL_SO_FORWARD_ONLY | SQL_SO_STATIC);
            
        case SQL_STATIC_CURSOR_ATTRIBUTES1:
            RETURN_ULONG(SQL_CA1_NEXT);
            
        case SQL_FORWARD_ONLY_CURSOR_ATTRIBUTES1:
            RETURN_ULONG(SQL_CA1_NEXT);
            
        case SQL_DYNAMIC_CURSOR_ATTRIBUTES1:
            RETURN_ULONG(0);
            
        case SQL_KEYSET_CURSOR_ATTRIBUTES1:
            RETURN_ULONG(0);
            
        // Transaction Support
        case SQL_TXN_CAPABLE:
            RETURN_USHORT(SQL_TC_ALL);
            
        case SQL_TXN_ISOLATION_OPTION:
            RETURN_ULONG(SQL_TXN_READ_UNCOMMITTED | SQL_TXN_READ_COMMITTED | 
                        SQL_TXN_REPEATABLE_READ | SQL_TXN_SERIALIZABLE);
            
        case SQL_DEFAULT_TXN_ISOLATION:
            RETURN_ULONG(SQL_TXN_READ_COMMITTED);
            
        // Identifier Case
        case SQL_IDENTIFIER_CASE:
            RETURN_USHORT(SQL_IC_UPPER);
            
        case SQL_IDENTIFIER_QUOTE_CHAR:
            RETURN_STRING("\"");
            
        // Catalog Support
        case SQL_CATALOG_NAME:
            RETURN_STRING("Y");
            
        case SQL_CATALOG_NAME_SEPARATOR:
            RETURN_STRING(".");
            
        case SQL_CATALOG_TERM:
            RETURN_STRING("catalog");
            
        case SQL_SCHEMA_TERM:
            RETURN_STRING("schema");
            
        case SQL_TABLE_TERM:
            RETURN_STRING("table");
            
        case SQL_PROCEDURE_TERM:
            RETURN_STRING("procedure");
            
        // Max Lengths
        case SQL_MAX_CATALOG_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_SCHEMA_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_TABLE_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_COLUMN_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_CURSOR_NAME_LEN:
            RETURN_USHORT(64);
            
        case SQL_MAX_IDENTIFIER_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_PROCEDURE_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_USER_NAME_LEN:
            RETURN_USHORT(128);
            
        case SQL_MAX_DRIVER_CONNECTIONS:
            RETURN_USHORT(config.max_connections > 0 ? config.max_connections : 0);
            
        case SQL_MAX_CONCURRENT_ACTIVITIES:
            RETURN_USHORT(0);  // No limit
            
        // Feature Support
        case SQL_GETDATA_EXTENSIONS:
            RETURN_ULONG(SQL_GD_ANY_COLUMN | SQL_GD_ANY_ORDER | SQL_GD_BOUND);
            
        case SQL_PARAM_ARRAY_ROW_COUNTS:
            RETURN_ULONG(SQL_PARC_NO_BATCH);
            
        case SQL_PARAM_ARRAY_SELECTS:
            RETURN_ULONG(SQL_PAS_NO_SELECT);
            
        case SQL_BATCH_ROW_COUNT:
            RETURN_ULONG(SQL_BRC_EXPLICIT);
            
        case SQL_BATCH_SUPPORT:
            RETURN_ULONG(SQL_BS_SELECT_EXPLICIT | SQL_BS_ROW_COUNT_EXPLICIT);
            
        case SQL_BOOKMARK_PERSISTENCE:
            RETURN_ULONG(0);
            
        case SQL_DESCRIBE_PARAMETER:
            RETURN_STRING("Y");
            
        case SQL_MULT_RESULT_SETS:
            RETURN_STRING("N");
            
        case SQL_MULTIPLE_ACTIVE_TXN:
            RETURN_STRING("Y");
            
        case SQL_NEED_LONG_DATA_LEN:
            RETURN_STRING("N");
            
        case SQL_NULL_COLLATION:
            RETURN_USHORT(SQL_NC_HIGH);
            
        case SQL_OUTER_JOINS:
            RETURN_STRING("Y");
            
        case SQL_ORDER_BY_COLUMNS_IN_SELECT:
            RETURN_STRING("N");
            
        case SQL_PROCEDURES:
            RETURN_STRING("N");
            
        case SQL_ROW_UPDATES:
            RETURN_STRING("N");
            
        case SQL_SEARCH_PATTERN_ESCAPE:
            RETURN_STRING("\\");
            
        case SQL_SPECIAL_CHARACTERS:
            RETURN_STRING("");
            
        // Numeric Functions
        case SQL_NUMERIC_FUNCTIONS:
            RETURN_ULONG(SQL_FN_NUM_ABS | SQL_FN_NUM_CEILING | SQL_FN_NUM_FLOOR |
                        SQL_FN_NUM_ROUND | SQL_FN_NUM_SQRT);
            
        // String Functions
        case SQL_STRING_FUNCTIONS:
            RETURN_ULONG(SQL_FN_STR_CONCAT | SQL_FN_STR_LENGTH | SQL_FN_STR_LTRIM |
                        SQL_FN_STR_RTRIM | SQL_FN_STR_SUBSTRING);
            
        // System Functions
        case SQL_SYSTEM_FUNCTIONS:
            RETURN_ULONG(SQL_FN_SYS_DBNAME | SQL_FN_SYS_USERNAME);
            
        // Timedate Functions
        case SQL_TIMEDATE_FUNCTIONS:
            RETURN_ULONG(SQL_FN_TD_NOW | SQL_FN_TD_CURDATE | SQL_FN_TD_CURTIME);
            
        // Convert Functions
        case SQL_CONVERT_FUNCTIONS:
            RETURN_ULONG(SQL_FN_CVT_CAST | SQL_FN_CVT_CONVERT);
            
        // Aggregate Functions
        case SQL_AGGREGATE_FUNCTIONS:
            RETURN_ULONG(SQL_AF_AVG | SQL_AF_COUNT | SQL_AF_MAX | SQL_AF_MIN | SQL_AF_SUM);
            
        // SQL Subqueries
        case SQL_SUBQUERIES:
            RETURN_ULONG(SQL_SQ_COMPARISON | SQL_SQ_EXISTS | SQL_SQ_IN);
            
        // Union Support
        case SQL_UNION:
            RETURN_ULONG(SQL_U_UNION | SQL_U_UNION_ALL);
            
        // Async Mode
        case SQL_ASYNC_MODE:
            RETURN_ULONG(SQL_AM_NONE);
            
        // SQL92 Features
        case SQL_SQL92_PREDICATES:
            RETURN_ULONG(SQL_SP_BETWEEN | SQL_SP_COMPARISON | SQL_SP_EXISTS |
                        SQL_SP_IN | SQL_SP_ISNOTNULL | SQL_SP_ISNULL | SQL_SP_LIKE);
            
        case SQL_SQL92_VALUE_EXPRESSIONS:
            RETURN_ULONG(SQL_SVE_CASE | SQL_SVE_CAST | SQL_SVE_COALESCE | SQL_SVE_NULLIF);
            
        default:
            conn->add_diagnostic(sqlstate::INVALID_INFO_TYPE, 0,
                                "Information type out of range");
            return SQL_ERROR;
    }
    
    #undef RETURN_STRING
    #undef RETURN_USHORT
    #undef RETURN_ULONG
}

SQLRETURN SQL_API SQLGetTypeInfo(
    SQLHSTMT hstmt,
    SQLSMALLINT fSqlType) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLGetTypeInfo")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated SQLGetTypeInfo failure");
        return SQL_ERROR;
    }
    
    // Set up result columns (19 columns as per ODBC spec)
    stmt->executed_ = true;
    stmt->cursor_open_ = true;
    stmt->current_row_ = -1;
    
    stmt->column_names_ = {
        "TYPE_NAME", "DATA_TYPE", "COLUMN_SIZE", "LITERAL_PREFIX", "LITERAL_SUFFIX",
        "CREATE_PARAMS", "NULLABLE", "CASE_SENSITIVE", "SEARCHABLE", "UNSIGNED_ATTRIBUTE",
        "FIXED_PREC_SCALE", "AUTO_UNIQUE_VALUE", "LOCAL_TYPE_NAME", "MINIMUM_SCALE",
        "MAXIMUM_SCALE", "SQL_DATA_TYPE", "SQL_DATETIME_SUB", "NUM_PREC_RADIX",
        "INTERVAL_PRECISION"
    };
    
    stmt->column_types_ = {
        SQL_WVARCHAR, SQL_SMALLINT, SQL_INTEGER, SQL_WVARCHAR, SQL_WVARCHAR,
        SQL_WVARCHAR, SQL_SMALLINT, SQL_SMALLINT, SQL_SMALLINT, SQL_SMALLINT,
        SQL_SMALLINT, SQL_SMALLINT, SQL_WVARCHAR, SQL_SMALLINT,
        SQL_SMALLINT, SQL_SMALLINT, SQL_SMALLINT, SQL_INTEGER,
        SQL_SMALLINT
    };
    
    stmt->num_result_cols_ = 19;
    stmt->result_data_.clear();
    
    auto types = get_mock_types(config.types);
    
    for (const auto& type : types) {
        // Filter by requested type
        if (fSqlType != SQL_ALL_TYPES && type.data_type != fSqlType) {
            continue;
        }
        
        std::vector<std::variant<std::monostate, long long, double, std::string>> row;
        row.push_back(type.type_name);
        row.push_back(static_cast<long long>(type.data_type));
        row.push_back(static_cast<long long>(type.column_size));
        row.push_back(type.literal_prefix.empty() ? 
                     std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : 
                     type.literal_prefix);
        row.push_back(type.literal_suffix.empty() ? 
                     std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : 
                     type.literal_suffix);
        row.push_back(type.create_params.empty() ? 
                     std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : 
                     type.create_params);
        row.push_back(static_cast<long long>(type.nullable));
        row.push_back(static_cast<long long>(type.case_sensitive));
        row.push_back(static_cast<long long>(type.searchable));
        row.push_back(static_cast<long long>(type.unsigned_attribute));
        row.push_back(static_cast<long long>(type.fixed_prec_scale));
        row.push_back(static_cast<long long>(type.auto_unique_value));
        row.push_back(type.local_type_name);
        row.push_back(static_cast<long long>(type.minimum_scale));
        row.push_back(static_cast<long long>(type.maximum_scale));
        row.push_back(static_cast<long long>(type.sql_data_type));
        row.push_back(static_cast<long long>(type.sql_datetime_sub));
        row.push_back(static_cast<long long>(type.num_prec_radix));
        row.push_back(static_cast<long long>(type.interval_precision));
        
        stmt->result_data_.push_back(std::move(row));
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetFunctions(
    SQLHDBC hdbc,
    SQLUSMALLINT fFunction,
    SQLUSMALLINT* pfExists) {
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    HandleLock lock(conn);
    
    conn->clear_diagnostics();
    
    // List of supported functions — must exactly match .def exports
    static const SQLUSMALLINT supported_functions[] = {
        SQL_API_SQLALLOCHANDLE,
        SQL_API_SQLBINDCOL,
        SQL_API_SQLBINDPARAMETER,
        SQL_API_SQLBROWSECONNECT,
        SQL_API_SQLBULKOPERATIONS,
        SQL_API_SQLCANCEL,
        SQL_API_SQLCLOSECURSOR,
        SQL_API_SQLCOLATTRIBUTE,
        SQL_API_SQLCOLUMNS,
        SQL_API_SQLCOLUMNPRIVILEGES,
        SQL_API_SQLCONNECT,
        SQL_API_SQLCOPYDESC,
        SQL_API_SQLDESCRIBECOL,
        SQL_API_SQLDESCRIBEPARAM,
        SQL_API_SQLDISCONNECT,
        SQL_API_SQLDRIVERCONNECT,
        SQL_API_SQLENDTRAN,
        SQL_API_SQLEXECDIRECT,
        SQL_API_SQLEXECUTE,
        SQL_API_SQLFETCH,
        SQL_API_SQLFETCHSCROLL,
        SQL_API_SQLFOREIGNKEYS,
        SQL_API_SQLFREEHANDLE,
        SQL_API_SQLFREESTMT,
        SQL_API_SQLGETCONNECTATTR,
        SQL_API_SQLGETCURSORNAME,
        SQL_API_SQLGETDATA,
        SQL_API_SQLGETDESCFIELD,
        SQL_API_SQLGETDESCREC,
        SQL_API_SQLGETDIAGFIELD,
        SQL_API_SQLGETDIAGREC,
        SQL_API_SQLGETENVATTR,
        SQL_API_SQLGETFUNCTIONS,
        SQL_API_SQLGETINFO,
        SQL_API_SQLGETSTMTATTR,
        SQL_API_SQLGETTYPEINFO,
        SQL_API_SQLMORERESULTS,
        SQL_API_SQLNATIVESQL,
        SQL_API_SQLNUMPARAMS,
        SQL_API_SQLNUMRESULTCOLS,
        SQL_API_SQLPARAMDATA,
        SQL_API_SQLPREPARE,
        SQL_API_SQLPRIMARYKEYS,
        SQL_API_SQLPROCEDURECOLUMNS,
        SQL_API_SQLPROCEDURES,
        SQL_API_SQLPUTDATA,
        SQL_API_SQLROWCOUNT,
        SQL_API_SQLSETCONNECTATTR,
        SQL_API_SQLSETCURSORNAME,
        SQL_API_SQLSETDESCFIELD,
        SQL_API_SQLSETDESCREC,
        SQL_API_SQLSETENVATTR,
        SQL_API_SQLSETPOS,
        SQL_API_SQLSETSTMTATTR,
        SQL_API_SQLSPECIALCOLUMNS,
        SQL_API_SQLSTATISTICS,
        SQL_API_SQLTABLES,
        SQL_API_SQLTABLEPRIVILEGES,
    };
    
    const size_t num_supported = sizeof(supported_functions) / sizeof(supported_functions[0]);
    
    if (fFunction == SQL_API_ODBC3_ALL_FUNCTIONS) {
        // Fill in the 250-element array
        if (pfExists) {
            std::memset(pfExists, 0, SQL_API_ODBC3_ALL_FUNCTIONS_SIZE * sizeof(SQLUSMALLINT));
            
            for (size_t i = 0; i < num_supported; ++i) {
                SQLUSMALLINT func = supported_functions[i];
                if (func < SQL_API_ODBC3_ALL_FUNCTIONS_SIZE * 16) {
                    pfExists[func >> 4] |= (1 << (func & 0xF));
                }
            }
        }
    } else if (fFunction == SQL_API_ALL_FUNCTIONS) {
        // Legacy 100-element array
        if (pfExists) {
            std::memset(pfExists, SQL_FALSE, 100 * sizeof(SQLUSMALLINT));
            
            for (size_t i = 0; i < num_supported; ++i) {
                if (supported_functions[i] < 100) {
                    pfExists[supported_functions[i]] = SQL_TRUE;
                }
            }
        }
    } else {
        // Single function query
        if (pfExists) {
            *pfExists = SQL_FALSE;
            for (size_t i = 0; i < num_supported; ++i) {
                if (supported_functions[i] == fFunction) {
                    *pfExists = SQL_TRUE;
                    break;
                }
            }
        }
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNativeSql(
    SQLHDBC hdbc,
    SQLCHAR* szSqlStrIn,
    SQLINTEGER cbSqlStrIn,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStrMax,
    SQLINTEGER* pcbSqlStr) {
    
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;
    
    // Mock: just return the input SQL unchanged
    std::string sql = sql_to_string(szSqlStrIn, static_cast<SQLSMALLINT>(cbSqlStrIn));
    
    if (pcbSqlStr) {
        *pcbSqlStr = static_cast<SQLINTEGER>(sql.length());
    }
    
    if (szSqlStr && cbSqlStrMax > 0) {
        size_t copy_len = std::min(sql.length(), static_cast<size_t>(cbSqlStrMax - 1));
        std::memcpy(szSqlStr, sql.c_str(), copy_len);
        szSqlStr[copy_len] = '\0';
        
        if (static_cast<SQLINTEGER>(sql.length()) >= cbSqlStrMax) {
            return SQL_SUCCESS_WITH_INFO;
        }
    }
    
    return SQL_SUCCESS;
}

} // extern "C"
