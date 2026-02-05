// Statement API - SQLExecDirect, SQLPrepare, SQLExecute, SQLFetch, etc.

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_data.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include <cstring>

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLExecDirect(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    // Check for failure injection
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLExecDirect")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated execution failure");
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Parse and execute SQL
    stmt->sql_ = sql_to_string(szSqlStr, static_cast<SQLSMALLINT>(cbSqlStr));
    auto parsed = parse_sql(stmt->sql_);
    
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    auto result = execute_query(parsed, config.result_set_size);
    
    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }
    
    // Store result
    stmt->executed_ = true;
    stmt->prepared_ = false;
    stmt->cursor_open_ = !result.data.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    stmt->row_count_ = result.affected_rows > 0 ? result.affected_rows : 
                       static_cast<SQLLEN>(result.data.size());
    
    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->result_data_.clear();
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrepare(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLPrepare")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated prepare failure");
        return SQL_ERROR;
    }
    
    stmt->sql_ = sql_to_string(szSqlStr, static_cast<SQLSMALLINT>(cbSqlStr));
    
    // Validate SQL syntax
    auto parsed = parse_sql(stmt->sql_);
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    stmt->prepared_ = true;
    stmt->executed_ = false;
    stmt->cursor_open_ = false;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLExecute(SQLHSTMT hstmt) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    if (!stmt->prepared_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Statement not prepared");
        return SQL_ERROR;
    }
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLExecute")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated execute failure");
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Execute prepared statement
    auto parsed = parse_sql(stmt->sql_);
    auto result = execute_query(parsed, config.result_set_size);
    
    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }
    
    stmt->executed_ = true;
    stmt->cursor_open_ = !result.data.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    stmt->row_count_ = result.affected_rows > 0 ? result.affected_rows :
                       static_cast<SQLLEN>(result.data.size());
    
    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->result_data_.clear();
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    if (!stmt->executed_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Statement not executed");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLFetch")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated fetch failure");
        return SQL_ERROR;
    }
    
    // Move to next row
    stmt->current_row_++;
    
    if (stmt->current_row_ >= static_cast<SQLLEN>(stmt->result_data_.size())) {
        stmt->cursor_open_ = false;
        return SQL_NO_DATA;
    }
    
    // Transfer data to bound columns
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
                    if (binding.target_value) {
                        *static_cast<SQLINTEGER*>(binding.target_value) = 
                            static_cast<SQLINTEGER>(value);
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = sizeof(SQLINTEGER);
                    }
                    break;
                    
                case SQL_C_SBIGINT:
                    if (binding.target_value) {
                        *static_cast<SQLBIGINT*>(binding.target_value) = value;
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = sizeof(SQLBIGINT);
                    }
                    break;
                    
                case SQL_C_SSHORT:
                    if (binding.target_value) {
                        *static_cast<SQLSMALLINT*>(binding.target_value) = 
                            static_cast<SQLSMALLINT>(value);
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = sizeof(SQLSMALLINT);
                    }
                    break;
                    
                case SQL_C_CHAR:
                default: {
                    std::string str = std::to_string(value);
                    if (binding.target_value && binding.buffer_length > 0) {
                        size_t copy_len = std::min(str.length(), 
                                                   static_cast<size_t>(binding.buffer_length - 1));
                        std::memcpy(binding.target_value, str.c_str(), copy_len);
                        static_cast<char*>(binding.target_value)[copy_len] = '\0';
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = static_cast<SQLLEN>(str.length());
                    }
                    break;
                }
            }
        } else if (std::holds_alternative<double>(cell)) {
            double value = std::get<double>(cell);
            
            switch (binding.target_type) {
                case SQL_C_DOUBLE:
                    if (binding.target_value) {
                        *static_cast<SQLDOUBLE*>(binding.target_value) = value;
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = sizeof(SQLDOUBLE);
                    }
                    break;
                    
                case SQL_C_FLOAT:
                    if (binding.target_value) {
                        *static_cast<SQLREAL*>(binding.target_value) = 
                            static_cast<SQLREAL>(value);
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = sizeof(SQLREAL);
                    }
                    break;
                    
                case SQL_C_CHAR:
                default: {
                    std::string str = std::to_string(value);
                    if (binding.target_value && binding.buffer_length > 0) {
                        size_t copy_len = std::min(str.length(),
                                                   static_cast<size_t>(binding.buffer_length - 1));
                        std::memcpy(binding.target_value, str.c_str(), copy_len);
                        static_cast<char*>(binding.target_value)[copy_len] = '\0';
                    }
                    if (binding.str_len_or_ind) {
                        *binding.str_len_or_ind = static_cast<SQLLEN>(str.length());
                    }
                    break;
                }
            }
        } else if (std::holds_alternative<std::string>(cell)) {
            const std::string& value = std::get<std::string>(cell);
            
            if (binding.target_value && binding.buffer_length > 0) {
                size_t copy_len = std::min(value.length(),
                                           static_cast<size_t>(binding.buffer_length - 1));
                std::memcpy(binding.target_value, value.c_str(), copy_len);
                static_cast<char*>(binding.target_value)[copy_len] = '\0';
            }
            if (binding.str_len_or_ind) {
                *binding.str_len_or_ind = static_cast<SQLLEN>(value.length());
            }
        }
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLGetData(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (!stmt->executed_ || stmt->current_row_ < 0) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "No current row");
        return SQL_ERROR;
    }
    
    if (stmt->result_data_.empty() || 
        static_cast<size_t>(stmt->current_row_) >= stmt->result_data_.size()) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Invalid row position");
        return SQL_ERROR;
    }
    
    if (icol < 1 || icol > static_cast<SQLUSMALLINT>(stmt->result_data_[stmt->current_row_].size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const auto& cell = stmt->result_data_[stmt->current_row_][icol - 1];
    
    // Handle NULL
    if (std::holds_alternative<std::monostate>(cell)) {
        if (pcbValue) *pcbValue = SQL_NULL_DATA;
        return SQL_SUCCESS;
    }
    
    // Convert based on target type
    if (std::holds_alternative<long long>(cell)) {
        long long value = std::get<long long>(cell);
        
        switch (fCType) {
            case SQL_C_SLONG:
            case SQL_C_LONG:
                if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = static_cast<SQLINTEGER>(value);
                if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
                break;
                
            case SQL_C_SBIGINT:
                if (rgbValue) *static_cast<SQLBIGINT*>(rgbValue) = value;
                if (pcbValue) *pcbValue = sizeof(SQLBIGINT);
                break;
                
            case SQL_C_SSHORT:
                if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = static_cast<SQLSMALLINT>(value);
                if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
                break;
                
            case SQL_C_CHAR:
            default: {
                std::string str = std::to_string(value);
                if (rgbValue && cbValueMax > 0) {
                    size_t copy_len = std::min(str.length(), static_cast<size_t>(cbValueMax - 1));
                    std::memcpy(rgbValue, str.c_str(), copy_len);
                    static_cast<char*>(rgbValue)[copy_len] = '\0';
                }
                if (pcbValue) *pcbValue = static_cast<SQLLEN>(str.length());
                break;
            }
        }
    } else if (std::holds_alternative<double>(cell)) {
        double value = std::get<double>(cell);
        
        switch (fCType) {
            case SQL_C_DOUBLE:
                if (rgbValue) *static_cast<SQLDOUBLE*>(rgbValue) = value;
                if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                break;
                
            case SQL_C_FLOAT:
                if (rgbValue) *static_cast<SQLREAL*>(rgbValue) = static_cast<SQLREAL>(value);
                if (pcbValue) *pcbValue = sizeof(SQLREAL);
                break;
                
            case SQL_C_CHAR:
            default: {
                std::string str = std::to_string(value);
                if (rgbValue && cbValueMax > 0) {
                    size_t copy_len = std::min(str.length(), static_cast<size_t>(cbValueMax - 1));
                    std::memcpy(rgbValue, str.c_str(), copy_len);
                    static_cast<char*>(rgbValue)[copy_len] = '\0';
                }
                if (pcbValue) *pcbValue = static_cast<SQLLEN>(str.length());
                break;
            }
        }
    } else if (std::holds_alternative<std::string>(cell)) {
        const std::string& value = std::get<std::string>(cell);
        
        if (rgbValue && cbValueMax > 0) {
            size_t copy_len = std::min(value.length(), static_cast<size_t>(cbValueMax - 1));
            std::memcpy(rgbValue, value.c_str(), copy_len);
            static_cast<char*>(rgbValue)[copy_len] = '\0';
        }
        if (pcbValue) *pcbValue = static_cast<SQLLEN>(value.length());
        
        if (static_cast<SQLLEN>(value.length()) >= cbValueMax) {
            return SQL_SUCCESS_WITH_INFO;  // Truncation
        }
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNumResultCols(
    SQLHSTMT hstmt,
    SQLSMALLINT* pccol) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (pccol) {
        *pccol = stmt->num_result_cols_;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDescribeCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLCHAR* szColName,
    SQLSMALLINT cbColNameMax,
    SQLSMALLINT* pcbColName,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbColDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (icol < 1 || icol > static_cast<SQLUSMALLINT>(stmt->column_names_.size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const std::string& name = stmt->column_names_[icol - 1];
    SQLSMALLINT type = stmt->column_types_[icol - 1];
    
    if (szColName) {
        copy_string_to_buffer(name, szColName, cbColNameMax, pcbColName);
    } else if (pcbColName) {
        *pcbColName = static_cast<SQLSMALLINT>(name.length());
    }
    
    if (pfSqlType) *pfSqlType = type;
    
    // Default column size based on type
    if (pcbColDef) {
        switch (type) {
            case SQL_INTEGER: *pcbColDef = 10; break;
            case SQL_SMALLINT: *pcbColDef = 5; break;
            case SQL_BIGINT: *pcbColDef = 19; break;
            case SQL_VARCHAR: *pcbColDef = 255; break;
            case SQL_DECIMAL: *pcbColDef = 18; break;
            case SQL_TYPE_DATE: *pcbColDef = 10; break;
            case SQL_TYPE_TIMESTAMP: *pcbColDef = 26; break;
            default: *pcbColDef = 255;
        }
    }
    
    if (pibScale) *pibScale = (type == SQL_DECIMAL) ? 2 : 0;
    if (pfNullable) *pfNullable = SQL_NULLABLE;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLBindCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (icol == 0) {
        // Unbind bookmark column - not supported
        return SQL_SUCCESS;
    }
    
    if (!rgbValue) {
        // Unbind column
        stmt->column_bindings_.erase(icol);
        return SQL_SUCCESS;
    }
    
    StatementHandle::ColumnBinding binding;
    binding.target_type = fCType;
    binding.target_value = rgbValue;
    binding.buffer_length = cbValueMax;
    binding.str_len_or_ind = pcbValue;
    
    stmt->column_bindings_[icol] = binding;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLBindParameter(
    SQLHSTMT hstmt,
    SQLUSMALLINT ipar,
    SQLSMALLINT fParamType,
    SQLSMALLINT fCType,
    SQLSMALLINT fSqlType,
    SQLULEN cbColDef,
    SQLSMALLINT ibScale,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) {
    
    (void)ibScale;
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (ipar == 0) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Parameter number must be >= 1");
        return SQL_ERROR;
    }
    
    if (!rgbValue) {
        // Unbind parameter
        stmt->parameter_bindings_.erase(ipar);
        return SQL_SUCCESS;
    }
    
    StatementHandle::ParameterBinding binding;
    binding.input_output_type = fParamType;
    binding.value_type = fCType;
    binding.param_type = fSqlType;
    binding.column_size = cbColDef;
    binding.decimal_digits = 0;
    binding.param_value = rgbValue;
    binding.buffer_length = cbValueMax;
    binding.str_len_or_ind = pcbValue;
    
    stmt->parameter_bindings_[ipar] = binding;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLRowCount(
    SQLHSTMT hstmt,
    SQLLEN* pcrow) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (pcrow) {
        *pcrow = stmt->row_count_;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT hstmt) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    if (!stmt->cursor_open_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor not open");
        return SQL_ERROR;
    }
    
    stmt->cursor_open_ = false;
    stmt->current_row_ = -1;
    stmt->result_data_.clear();
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT hstmt) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    // Mock driver doesn't support multiple result sets
    return SQL_NO_DATA;
}

SQLRETURN SQL_API SQLGetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) {
    
    FILE* f = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
    if (f) {
        fprintf(f, "SQLGetStmtAttr called: hstmt=%p, attr=%d, rgbValue=%p\n", hstmt, fAttribute, rgbValue);
        fflush(f);
        fclose(f);
    }
    
    (void)cbValueMax;
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) {
        FILE* f2 = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
        if (f2) {
            fprintf(f2, "  validate returned NULL\n");
            fflush(f2);
            fclose(f2);
        }
        return SQL_INVALID_HANDLE;
    }
    
    FILE* f3 = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
    if (f3) {
        fprintf(f3, "  validate succeeded, stmt=%p, switching on attribute...\n", stmt);
        fflush(f3);
        fclose(f3);
    }
    
    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE: {
            FILE* f4 = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
            if (f4) {
                fprintf(f4, "  case SQL_ATTR_CURSOR_TYPE\n");
                fflush(f4);
                fclose(f4);
            }
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->cursor_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
        }
            
        case SQL_ATTR_CONCURRENCY:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->concurrency_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_MAX_ROWS:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->max_rows_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_QUERY_TIMEOUT:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->query_timeout_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_ROW_ARRAY_SIZE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->row_array_size_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->paramset_size_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->async_enable_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        default:
            FILE* f5 = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
            if (f5) {
                fprintf(f5, "  default case for unknown attribute\n");
                fflush(f5);
                fclose(f5);
            }
            return SQL_SUCCESS;  // Ignore unknown attributes
    }
    
    FILE* f6 = fopen("C:\\temp\\debug_getstmtattr.txt", "a");
    if (f6) {
        fprintf(f6, "  about to return SQL_SUCCESS\n");
        fflush(f6);
        fclose(f6);
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) {
    
    (void)cbValue;
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    SQLULEN value = reinterpret_cast<SQLULEN>(rgbValue);
    
    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE:
            stmt->cursor_type_ = value;
            break;
            
        case SQL_ATTR_CONCURRENCY:
            stmt->concurrency_ = value;
            break;
            
        case SQL_ATTR_MAX_ROWS:
            stmt->max_rows_ = value;
            break;
            
        case SQL_ATTR_QUERY_TIMEOUT:
            stmt->query_timeout_ = value;
            break;
            
        case SQL_ATTR_ROW_ARRAY_SIZE:
            stmt->row_array_size_ = value;
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            stmt->paramset_size_ = value;
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            stmt->async_enable_ = value;
            break;
            
        default:
            // Ignore unknown attributes
            break;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLFreeStmt(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    switch (fOption) {
        case SQL_CLOSE:
            stmt->cursor_open_ = false;
            stmt->current_row_ = -1;
            stmt->result_data_.clear();
            break;
            
        case SQL_UNBIND:
            stmt->column_bindings_.clear();
            break;
            
        case SQL_RESET_PARAMS:
            stmt->parameter_bindings_.clear();
            break;
            
        case SQL_DROP:
            delete stmt;
            break;
    }
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLCancel(SQLHSTMT hstmt) {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    // Mock: just reset state
    stmt->cursor_open_ = false;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLNumParams(
    SQLHSTMT hstmt,
    SQLSMALLINT* pcpar) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    // Count ? placeholders in SQL
    int count = 0;
    for (char c : stmt->sql_) {
        if (c == '?') count++;
    }
    
    if (pcpar) *pcpar = static_cast<SQLSMALLINT>(count);
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLDescribeParam(
    SQLHSTMT hstmt,
    SQLUSMALLINT ipar,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbParamDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)ipar;
    
    // Default parameter description
    if (pfSqlType) *pfSqlType = SQL_VARCHAR;
    if (pcbParamDef) *pcbParamDef = 255;
    if (pibScale) *pibScale = 0;
    if (pfNullable) *pfNullable = SQL_NULLABLE;
    
    return SQL_SUCCESS;
}

// ODBC 2.x compatibility functions
SQLRETURN SQL_API SQLGetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLPOINTER pvParam) {
    // Map to ODBC 3.x function
    return SQLGetStmtAttr(hstmt, fOption, pvParam, SQL_MAX_OPTION_STRING_LENGTH, NULL);
}

SQLRETURN SQL_API SQLSetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLULEN vParam) {
    // Map to ODBC 3.x function
    return SQLSetStmtAttr(hstmt, fOption, reinterpret_cast<SQLPOINTER>(vParam), SQL_NTS);
}

} // extern "C"
