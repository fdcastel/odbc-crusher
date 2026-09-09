// Statement API - SQLExecDirect, SQLPrepare, SQLExecute, SQLFetch, etc.

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_data.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include "utils/buffer_copy.hpp"
#include "utils/c_types.hpp"
#include "odbc/param_binding.hpp"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cctype>
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

namespace {
static void record_dynamic_function(StatementHandle* stmt,
                                    const ParsedQuery& parsed,
                                    size_t result_rows)
{
    struct Entry { const char* name; SQLINTEGER code; };
    Entry e{"", SQL_DIAG_UNKNOWN_STATEMENT};
    switch (parsed.query_type) {
        case ParsedQuery::QueryType::Select:
            e = {"SELECT CURSOR", SQL_DIAG_SELECT_CURSOR};
            break;
        case ParsedQuery::QueryType::Insert:
            e = {"INSERT", SQL_DIAG_INSERT};
            break;
        case ParsedQuery::QueryType::Update:
            // The mock only ever produces the searched form; there is no
            // positioned-update path to report SQL_DIAG_DYNAMIC_UPDATE for.
            e = {"UPDATE WHERE", SQL_DIAG_UPDATE_WHERE};
            break;
        case ParsedQuery::QueryType::Delete:
            e = {"DELETE WHERE", SQL_DIAG_DELETE_WHERE};
            break;
        case ParsedQuery::QueryType::CreateTable:
            e = {"CREATE TABLE", SQL_DIAG_CREATE_TABLE};
            break;
        case ParsedQuery::QueryType::DropTable:
            e = {"DROP TABLE", SQL_DIAG_DROP_TABLE};
            break;
        case ParsedQuery::QueryType::Call:
            e = {"CALL", SQL_DIAG_CALL};
            break;
        case ParsedQuery::QueryType::Other:
        default:
            break;
    }
    stmt->dynamic_function_ = e.name;
    stmt->dynamic_function_code_ = e.code;

    // Only a cursor-opening statement has a cursor row count; for anything
    // else the field stays 0, which is what the spec says to report.
    const bool opens_cursor =
        parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.is_literal_select;
    stmt->cursor_row_count_ = opens_cursor
        ? static_cast<SQLLEN>(result_rows)
        : 0;
}

} // anonymous namespace

namespace mock_odbc {

// I6: what this statement's connection may see and where its writes go.
//
// Keyed on autocommit being OFF, deliberately **not** on
// ConnectionHandle::in_transaction_ - that is set *after* the executor runs
// (below, and in SQLExecute), so during a transaction's first statement it is
// still false. Autocommit is also the correct ODBC model: there is no BEGIN,
// the statement opens the transaction.
static TxnContext txn_context_for(StatementHandle* stmt) {
    auto* conn = stmt ? stmt->connection() : nullptr;
    if (!conn || !conn->pending_writes()) return {};

    TxnContext ctx;
    ctx.buffered = (conn->autocommit_ == SQL_AUTOCOMMIT_OFF);

    // I6: honestly at READ UNCOMMITTED, or dishonestly under DirtyReads.
    //
    // The second is what a probe needs in order to be able to fail. A driver
    // showing a dirty read at READ UNCOMMITTED is behaving correctly; one that
    // reports READ COMMITTED and shows them anyway is the defect, and
    // SQL_DEFAULT_TXN_ISOLATION deliberately keeps reporting the level that was
    // asked for so the lie stays a lie.
    const auto& cfg = BehaviorController::instance().config();
    ctx.read_uncommitted =
        (conn->txn_isolation_ == SQL_TXN_READ_UNCOMMITTED) || cfg.dirty_reads;
    ctx.writes = conn->pending_writes();
    ctx.conn_id = conn->id();
    return ctx;
}



// D18: SQLFetch and SQLFetchScroll each had their own copy of this loop,
// and they drifted. This one went through write_numeric_as when D11 rebuilt
// the conversion table; the copy in driver_main.cpp kept the original
// switch, which knew four C types and laid the decimal spelling of a number
// over the caller's buffer for everything else - so an application that
// scrolled got the pre-D11 driver. One loop now, and D11's table reaches
// the scroll path with it.
// Returns SQL_ERROR when a column cannot be converted to its bound C type
// (07006 posted); SQL_SUCCESS otherwise.
SQLRETURN deliver_row_to_bound_columns(StatementHandle* stmt,
                                       const std::vector<std::variant<
                                           std::monostate, long long, double,
                                           std::string>>& row) {
    
    // D11: a bound column is an *array* when SQL_ATTR_ROW_ARRAY_SIZE is more
    // than one, and `row_set_element_` says which element this row fills. The
    // stride is the buffer length for column-wise binding (the default) or
    // SQL_ATTR_ROW_BIND_TYPE for row-wise, plus SQL_ATTR_ROW_BIND_OFFSET_PTR
    // if the application set one. Element 0 with no offset is exactly what
    // the single-row path did, so a plain SQLFetch is unaffected.
    const SQLULEN element = stmt->row_set_element_;
    const SQLLEN bind_offset = stmt->row_bind_offset_ptr_
                             ? static_cast<SQLLEN>(*stmt->row_bind_offset_ptr_)
                             : 0;

    for (const auto& [col_num, binding_ref] : stmt->column_bindings_) {
        if (col_num < 1 || col_num > static_cast<SQLUSMALLINT>(row.size())) {
            continue;
        }

        // A copy, with its pointers moved to this row's element.
        StatementHandle::ColumnBinding binding = binding_ref;
        {
            const SQLLEN stride =
                stmt->row_bind_type_ == SQL_BIND_BY_COLUMN
                    ? binding.buffer_length
                    : static_cast<SQLLEN>(stmt->row_bind_type_);
            const SQLLEN step = bind_offset
                              + static_cast<SQLLEN>(element) * stride;
            if (binding.target_value && step != 0) {
                binding.target_value =
                    static_cast<char*>(binding.target_value) + step;
            }
            if (binding.str_len_or_ind) {
                // The indicator array strides by its own element size when
                // binding column-wise; row-wise it lives inside the struct.
                const SQLLEN ind_step =
                    stmt->row_bind_type_ == SQL_BIND_BY_COLUMN
                        ? bind_offset + static_cast<SQLLEN>(
                              element * sizeof(SQLLEN))
                        : step;
                binding.str_len_or_ind = reinterpret_cast<SQLLEN*>(
                    reinterpret_cast<char*>(binding.str_len_or_ind) + ind_step);
            }
        }

        const auto& cell = row[col_num - 1];

        // Handle NULL
        if (std::holds_alternative<std::monostate>(cell)) {
            if (binding.str_len_or_ind) {
                *binding.str_len_or_ind = SQL_NULL_DATA;
            }
            continue;
        }
        
        // D11/D18: one delivery path for every numeric C type.
        //
        // This used to be two switches that between them handled SQL_C_SLONG,
        // SQL_C_SBIGINT, SQL_C_SSHORT, SQL_C_DOUBLE and SQL_C_FLOAT, with
        // everything else falling through to `default:` and being written as
        // an **ANSI decimal string**. An application binding a column as
        // SQL_C_ULONG, SQL_C_UBIGINT, SQL_C_UTINYINT, SQL_C_BIT or
        // SQL_C_WCHAR got the characters of the number laid over its buffer
        // and an indicator claiming a string length - SQL_SUCCESS returned,
        // garbage delivered. write_numeric_as knows every C type in the
        // table, so a type added there is handled here without an edit.
        const bool is_float_cell = std::holds_alternative<double>(cell);
        if (is_float_cell || std::holds_alternative<long long>(cell)) {
            // D34
            const long long ival = is_float_cell
                ? 0
                : apply_numeric_skew(std::get<long long>(cell),
                                     FetchPath::BoundColumn);
            const double dval = is_float_cell
                ? apply_numeric_skew(std::get<double>(cell),
                                     FetchPath::BoundColumn)
                : 0.0;

            const SQLRETURN wrote = write_numeric_as(
                binding.target_type, ival, dval, is_float_cell,
                binding.target_value, binding.buffer_length,
                binding.str_len_or_ind);

            if (wrote == SQL_ERROR) {
                // A numeric cell requested as a date, an interval or an
                // unknown C type. 07006 is the spec's answer for a
                // conversion it does not define, and it is a great deal more
                // useful than silently writing digits.
                stmt->add_diagnostic(sqlstate::DATA_TYPE_ATTRIBUTE_VIOLATION, 0,
                                     "Restricted data type attribute "
                                     "violation for column " +
                                     std::to_string(col_num));
                return SQL_ERROR;
            }
            if (wrote == SQL_SUCCESS_WITH_INFO) {
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                     "String data, right truncated");
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

}  // namespace mock_odbc

extern "C" {

SQLRETURN SQL_API SQLExecDirect(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
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
        for (int i = 0; i < config.error_count; ++i) {
            stmt->add_diagnostic(config.error_code, i + 1,
                "Simulated execution failure (record " + std::to_string(i + 1) + " of " + std::to_string(config.error_count) + ")");
        }
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Parse and execute SQL
    stmt->sql_ = sql_to_string(szSqlStr, cbSqlStr);   // D14: no truncation
    auto parsed = parse_sql(stmt->sql_);
    
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    auto result = execute_query(parsed, config.result_set_size,
                                txn_context_for(stmt));

    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }

    // PORT plan port 3 — write OUT/INOUT/RETURN values back to bound params.
    if (!result.proc_name.empty()) {
        apply_proc_output_writeback(stmt, result.proc_name, result.proc_output_values);
    }

    // Store result
    stmt->executed_ = true;
    stmt->prepared_ = false;
    // D14: a cursor opens because the statement returned a *result set*,
    // not because that result set has rows. Keying it on `!data.empty()`
    // meant a SELECT matching nothing left no cursor to close, and the
    // SQLCloseCursor an application is told to make then failed 24000.
    stmt->cursor_open_ = !result.column_names.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    // Propagate the executor's affected_rows verbatim when it set one — this
    // includes the spec-baseline -1 from `EXECUTE PROCEDURE` (affected count
    // unknown).
    //
    // D14: the fallback used to be the *result-set size*, so SQLRowCount
    // answered a SELECT with the number of rows it had produced. SQLRowCount
    // reports rows **affected** by an INSERT, UPDATE or DELETE; a cursor
    // statement has none, and -1 is what says so. The cursor's own row count
    // has its own field, SQL_DIAG_CURSOR_ROW_COUNT, which D22 now fills in.
    const bool cursor_statement =
        parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.is_literal_select;
    stmt->row_count_ = cursor_statement
                       ? static_cast<SQLLEN>(-1)
                       : static_cast<SQLLEN>(result.affected_rows);
    record_dynamic_function(stmt, parsed, result.data.size());
    // D28: ODBC's transaction model is implicit - there is no BEGIN, so a
    // transaction opens the moment a statement executes with autocommit
    // OFF and stays open until SQLEndTran. Without this the connection
    // had no notion of being mid-transaction at all.
    if (auto* txn_conn = stmt->connection()) {
        if (txn_conn->autocommit_ == SQL_AUTOCOMMIT_OFF) {
            txn_conn->in_transaction_ = true;
        }
    }
    
    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->column_sizes_ = result.column_sizes;   // D14
    stmt->result_data_.clear();
    stmt->reset_getdata_continuation();          // D85
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLPrepare(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
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
    
    stmt->sql_ = sql_to_string(szSqlStr, cbSqlStr);   // D14: no truncation
    
    // Validate SQL syntax
    auto parsed = parse_sql(stmt->sql_);
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    stmt->prepared_ = true;
    stmt->executed_ = false;
    stmt->cursor_open_ = false;

    // D14: SQLNumResultCols and SQLDescribeCol answered 0 columns until the
    // statement had been executed, because the metadata was only ever built
    // by the execute path. A prepared cursor statement has a known result
    // shape and the spec expects both calls to describe it.
    //
    // The shape is taken from the executor rather than from a second
    // implementation, so the two cannot drift - that is the mistake D13 spent
    // 88 lines undoing. The WHERE clause is cleared first: it has no bearing
    // on the column list, and at prepare time it still holds unsubstituted
    // `?` markers that the predicate builder would rightly reject.
    const bool cursor_statement =
        parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.is_literal_select;
    if (cursor_statement) {
        ParsedQuery shape = parsed;
        shape.where_clause.clear();
        auto described = execute_query(shape, config.result_set_size,
                                       txn_context_for(stmt));
        if (described.success) {
            stmt->num_result_cols_ =
                static_cast<SQLSMALLINT>(described.column_names.size());
            stmt->column_names_ = std::move(described.column_names);
            stmt->column_types_.assign(described.column_types.begin(),
                                       described.column_types.end());
            stmt->column_sizes_ = described.column_sizes;
        }
    } else {
        stmt->num_result_cols_ = 0;
        stmt->column_names_.clear();
        stmt->column_types_.clear();
        stmt->column_sizes_.clear();
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLExecute(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
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
        // For array params, fill status array with errors
        if (stmt->paramset_size_ > 1 && stmt->param_status_ptr_) {
            for (SQLULEN i = 0; i < stmt->paramset_size_; ++i) {
                stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
            }
        }
        if (stmt->params_processed_ptr_) {
            *stmt->params_processed_ptr_ = stmt->paramset_size_;
        }
        stmt->add_diagnostic(config.error_code, 0, "Simulated execute failure");
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Parse SQL once
    auto parsed = parse_sql(stmt->sql_);
    
    // --- Array parameter execution ---
    if (stmt->paramset_size_ > 1) {
        SQLULEN success_count = 0;
        SQLULEN error_count = 0;
        SQLULEN processed = 0;
        SQLLEN total_affected = 0;
        
        // Accumulate result data from all parameter sets
        std::vector<std::string> result_col_names;
        std::vector<SQLSMALLINT> result_col_types;
        std::vector<std::vector<std::variant<std::monostate, long long, double, std::string>>> all_result_data;
        
        for (SQLULEN i = 0; i < stmt->paramset_size_; ++i) {
            processed = i + 1;
            
            // Check operation array for SQL_PARAM_IGNORE
            if (stmt->param_operation_ptr_ && 
                stmt->param_operation_ptr_[i] == SQL_PARAM_IGNORE) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_UNUSED;
                }
                continue;
            }
            
            // PORT plan port 6 canary — ArrayBindRowFailsAt: synthesize a
            // SQLSTATE 23000 error for the configured 1-indexed row, leave
            // surrounding rows to execute normally. Drives the per-row
            // status array probe.
            if (config.array_bind_row_fails_at > 0 &&
                static_cast<int>(i + 1) == config.array_bind_row_fails_at) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
                }
                error_count++;
                stmt->add_diagnostic("23000", 0,
                    "Parameter set " + std::to_string(i + 1) +
                    ": injected ArrayBindRowFailsAt failure");
                continue;
            }

            // Execute with current parameter set — substitute bound param values
            ParsedQuery row_parsed = parsed;
            substitute_params(row_parsed, stmt->parameter_bindings_, i, stmt->param_bind_type_);
            auto result = execute_query(row_parsed, config.result_set_size,
                                        txn_context_for(stmt));

            if (result.success) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_SUCCESS;
                }
                success_count++;
                total_affected += result.affected_rows > 0 ? result.affected_rows : 
                                  static_cast<SQLLEN>(result.data.size());
                
                // Capture column metadata from first successful execution
                if (result_col_names.empty() && !result.column_names.empty()) {
                    result_col_names = result.column_names;
                    for (auto t : result.column_types) {
                        result_col_types.push_back(t);
                    }
                }
                
                // Accumulate result data
                for (const auto& row : result.data) {
                    std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
                    for (const auto& cell : row) {
                        converted_row.push_back(cell);
                    }
                    all_result_data.push_back(std::move(converted_row));
                }
            } else {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
                }
                error_count++;
                stmt->add_diagnostic(result.error_sqlstate, 0, 
                    "Parameter set " + std::to_string(i + 1) + ": " + result.error_message);
            }
        }
        
        // Set params processed count
        if (stmt->params_processed_ptr_) {
            *stmt->params_processed_ptr_ = processed;
        }
        
        // Set statement state
        stmt->executed_ = true;
        stmt->cursor_open_ = !all_result_data.empty();
        stmt->current_row_ = -1;
        stmt->row_count_ = total_affected;
        stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result_col_names.size());
        stmt->column_names_ = std::move(result_col_names);
        stmt->column_types_ = std::move(result_col_types);
        stmt->result_data_ = std::move(all_result_data);
        stmt->reset_getdata_continuation();      // D85
        
        // Determine return code based on success/error counts
        if (error_count == 0) {
            return SQL_SUCCESS;
        } else if (success_count == 0) {
            return SQL_ERROR;
        } else {
            // Mixed results - some succeeded, some failed
            return SQL_SUCCESS_WITH_INFO;
        }
    }
    
    // --- Single parameter set execution (original path) ---
    
    // Substitute bound parameter values into the parsed query (INSERT and literal SELECT)
    substitute_params(parsed, stmt->parameter_bindings_, 0, stmt->param_bind_type_);
    
    auto result = execute_query(parsed, config.result_set_size,
                                txn_context_for(stmt));

    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }

    // PORT plan port 3 — write OUT/INOUT/RETURN values back to bound params.
    if (!result.proc_name.empty()) {
        apply_proc_output_writeback(stmt, result.proc_name, result.proc_output_values);
    }

    // Set params processed for single execution too
    if (stmt->params_processed_ptr_) {
        *stmt->params_processed_ptr_ = 1;
    }
    if (stmt->param_status_ptr_) {
        stmt->param_status_ptr_[0] = SQL_PARAM_SUCCESS;
    }

    stmt->executed_ = true;
    // D14: a cursor opens because the statement returned a *result set*,
    // not because that result set has rows. Keying it on `!data.empty()`
    // meant a SELECT matching nothing left no cursor to close, and the
    // SQLCloseCursor an application is told to make then failed 24000.
    stmt->cursor_open_ = !result.column_names.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    // Same semantics as SQLExecDirect — see the D14 note there.
    const bool cursor_statement =
        parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.is_literal_select;
    stmt->row_count_ = cursor_statement
                       ? static_cast<SQLLEN>(-1)
                       : static_cast<SQLLEN>(result.affected_rows);
    record_dynamic_function(stmt, parsed, result.data.size());
    // D28: ODBC's transaction model is implicit - there is no BEGIN, so a
    // transaction opens the moment a statement executes with autocommit
    // OFF and stays open until SQLEndTran. Without this the connection
    // had no notion of being mid-transaction at all.
    if (auto* txn_conn = stmt->connection()) {
        if (txn_conn->autocommit_ == SQL_AUTOCOMMIT_OFF) {
            txn_conn->in_transaction_ = true;
        }
    }

    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->column_sizes_ = result.column_sizes;   // D14
    stmt->result_data_.clear();
    stmt->reset_getdata_continuation();          // D85
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    if (!stmt->executed_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor is not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLFetch")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated fetch failure");
        return SQL_ERROR;
    }
    
    // D11: SQLFetch delivers a *row set*, not a row. It advanced one row and
    // wrote element 0 whatever SQL_ATTR_ROW_ARRAY_SIZE said, so an
    // application that bound arrays of ten and asked for ten rows got one -
    // and, because the driver answered SQL_SUCCESS and never wrote the
    // fetched-rows count, had no way to discover it.
    const SQLULEN row_set_size = stmt->row_array_size_ > 0
                               ? stmt->row_array_size_ : 1;
    const SQLLEN total = static_cast<SQLLEN>(stmt->result_data_.size());

    stmt->current_row_++;
    if (stmt->current_row_ >= total) {
        // D14: the cursor used to close itself here. Running off the end of a
        // result set is not the same as closing the cursor - the spec has the
        // application call SQLCloseCursor after its fetch loop, and closing
        // early made that call fail 24000 on every well-written program.
        if (stmt->rows_fetched_ptr_) *stmt->rows_fetched_ptr_ = 0;
        return SQL_NO_DATA;
    }

    SQLULEN delivered = 0;
    for (SQLULEN i = 0; i < row_set_size; ++i) {
        const SQLLEN at = stmt->current_row_ + static_cast<SQLLEN>(i);
        if (at >= total) {
            // Past the end: the spec has the remaining status entries say so.
            if (stmt->row_status_ptr_) {
                stmt->row_status_ptr_[i] = SQL_ROW_NOROW;
            }
            continue;
        }
        // Element `i` of each bound array. The offset is applied inside the
        // delivery loop via set_row_set_element below.
        stmt->row_set_element_ = i;
        if (deliver_row_to_bound_columns(stmt, stmt->result_data_[at])
                == SQL_ERROR) {
            stmt->row_set_element_ = 0;
            if (stmt->row_status_ptr_) stmt->row_status_ptr_[i] = SQL_ROW_ERROR;
            return SQL_ERROR;
        }
        if (stmt->row_status_ptr_) stmt->row_status_ptr_[i] = SQL_ROW_SUCCESS;
        ++delivered;
    }
    stmt->row_set_element_ = 0;

    // The cursor sits on the last row of the set it just delivered, so the
    // next SQLFetch starts after it.
    if (delivered > 1) {
        stmt->current_row_ += static_cast<SQLLEN>(delivered) - 1;
    }
    if (stmt->rows_fetched_ptr_) *stmt->rows_fetched_ptr_ = delivered;
    
    // D35: a driver that warns on every row it returns. The application must
    // keep fetching until SQL_NO_DATA; a loop written `== SQL_SUCCESS` stops
    // here instead, mid-result-set.
    if (config.fetch_returns_warning) {
        stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                             "String data, right truncated");
        return SQL_SUCCESS_WITH_INFO;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetData(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // D36 (down payment): SQLGetData was not faultable, so a probe that
    // ignored its return code could not be caught. A27 is exactly that bug -
    // two transaction probes read COUNT(*) into a variable initialised to 0
    // and never checked the call, and 0 was one of them's PASS condition.
    {
        const auto& config = BehaviorController::instance().config();
        if (config.should_fail("SQLGetData")) {
            stmt->add_diagnostic(config.error_code, 0,
                                 "Simulated SQLGetData failure");
            return SQL_ERROR;
        }
    }

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
    
    // D37: a continuation belongs to one (column, row). Asking for a
    // different column, or fetching, restarts the value - which is what the
    // spec says and what a caller looping on 01004 depends on.
    if (stmt->getdata_col_ != icol || stmt->getdata_row_ != stmt->current_row_) {
        stmt->getdata_col_ = icol;
        stmt->getdata_row_ = stmt->current_row_;
        stmt->getdata_offset_ = 0;
    }

    const auto& cell = stmt->result_data_[stmt->current_row_][icol - 1];

    // Handle NULL
    if (std::holds_alternative<std::monostate>(cell)) {
        // SilentCorruption=NullAsEmpty — Oracle-style "empty string and NULL
        // are the same thing" misbehaviour: for character target types the
        // mock returns an empty buffer with indicator=0 instead of
        // SQL_NULL_DATA. Drives the PORT plan port 2 e2e canary; correct
        // drivers/applications must distinguish the two.
        const auto& config = BehaviorController::instance().config();
        if (config.silent_corruption ==
                DriverConfig::SilentCorruptionMode::NullAsEmpty &&
            (fCType == SQL_C_CHAR || fCType == SQL_C_WCHAR ||
             fCType == SQL_C_DEFAULT || fCType == SQL_ARD_TYPE)) {
            if (rgbValue && cbValueMax > 0) {
                if (fCType == SQL_C_WCHAR) {
                    auto* w = static_cast<SQLWCHAR*>(rgbValue);
                    w[0] = 0;
                } else {
                    static_cast<char*>(rgbValue)[0] = '\0';
                }
            }
            if (pcbValue) *pcbValue = 0;
            return SQL_SUCCESS;
        }
        if (pcbValue) *pcbValue = SQL_NULL_DATA;
        return SQL_SUCCESS;
    }
    
    // Handle SQL_C_DEFAULT: map to appropriate type based on cell content
    SQLSMALLINT effective_type = fCType;
    if (fCType == SQL_C_DEFAULT || fCType == SQL_ARD_TYPE) {
        if (std::holds_alternative<long long>(cell)) effective_type = SQL_C_SBIGINT;
        else if (std::holds_alternative<double>(cell)) effective_type = SQL_C_DOUBLE;
        else effective_type = SQL_C_CHAR;
    }

    // Convert based on target type
    if (std::holds_alternative<long long>(cell)) {
        // D34
        long long value = apply_numeric_skew(std::get<long long>(cell),
                                             FetchPath::GetData);
        
        switch (effective_type) {
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
            
            case SQL_C_DOUBLE:
                if (rgbValue) *static_cast<SQLDOUBLE*>(rgbValue) = static_cast<SQLDOUBLE>(value);
                if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                break;
                
            case SQL_C_NUMERIC: {
                // Convert integer to SQL_NUMERIC_STRUCT
                if (rgbValue) {
                    SQL_NUMERIC_STRUCT* ns = static_cast<SQL_NUMERIC_STRUCT*>(rgbValue);
                    std::memset(ns, 0, sizeof(SQL_NUMERIC_STRUCT));
                    ns->precision = 18;
                    ns->scale = 0;
                    ns->sign = (value >= 0) ? 1 : 0;
                    unsigned long long abs_val = (value >= 0) ? static_cast<unsigned long long>(value)
                                                              : static_cast<unsigned long long>(-value);
                    for (int b = 0; b < SQL_MAX_NUMERIC_LEN && abs_val > 0; ++b) {
                        ns->val[b] = static_cast<SQLCHAR>(abs_val & 0xFF);
                        abs_val >>= 8;
                    }
                }
                if (pcbValue) *pcbValue = sizeof(SQL_NUMERIC_STRUCT);
                break;
            }
                
            case SQL_C_WCHAR:
                break;  // Handled by SQL_C_WCHAR catch-all below
                
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
        // D34
        double value = apply_numeric_skew(std::get<double>(cell),
                                          FetchPath::GetData);
        
        switch (effective_type) {
            case SQL_C_DOUBLE:
                if (rgbValue) *static_cast<SQLDOUBLE*>(rgbValue) = value;
                if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                break;
                
            case SQL_C_FLOAT:
                if (rgbValue) *static_cast<SQLREAL*>(rgbValue) = static_cast<SQLREAL>(value);
                if (pcbValue) *pcbValue = sizeof(SQLREAL);
                break;
                
            case SQL_C_NUMERIC: {
                // Convert double to SQL_NUMERIC_STRUCT
                if (rgbValue) {
                    SQL_NUMERIC_STRUCT* ns = static_cast<SQL_NUMERIC_STRUCT*>(rgbValue);
                    std::memset(ns, 0, sizeof(SQL_NUMERIC_STRUCT));
                    ns->sign = (value >= 0) ? 1 : 0;
                    double abs_val = std::abs(value);
                    // Determine scale from decimal places
                    ns->scale = 0;
                    ns->precision = 18;
                    // Use provided descriptor scale if available (via descriptor)
                    // Default: detect decimal digits
                    double int_part;
                    double frac_part = std::modf(abs_val, &int_part);
                    SQLSCHAR scale = 0;
                    if (frac_part > 0.0) {
                        // Find appropriate scale (up to 10)
                        for (scale = 1; scale <= 10; ++scale) {
                            double scaled = abs_val * std::pow(10.0, scale);
                            double rounded = std::round(scaled);
                            if (std::abs(scaled - rounded) < 1e-6) break;
                        }
                    }
                    ns->scale = scale;
                    // Compute val[] = abs_val * 10^scale as little-endian integer
                    unsigned long long int_val = static_cast<unsigned long long>(
                        std::round(abs_val * std::pow(10.0, scale)));
                    for (int b = 0; b < SQL_MAX_NUMERIC_LEN && int_val > 0; ++b) {
                        ns->val[b] = static_cast<SQLCHAR>(int_val & 0xFF);
                        int_val >>= 8;
                    }
                }
                if (pcbValue) *pcbValue = sizeof(SQL_NUMERIC_STRUCT);
                break;
            }
                
            case SQL_C_WCHAR:
                break;  // Handled by SQL_C_WCHAR catch-all below
                
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
        const std::string& cell_value = std::get<std::string>(cell);
        // SilentCorruption=MangleUnicode — replace every non-ASCII byte (which
        // covers the leading bytes of any multibyte UTF-8 codepoint) with '?'.
        // Drives the PORT plan port 7 e2e canary; correct drivers preserve
        // every codepoint regardless of the system codepage. Only touches
        // char/wchar fetches — leaves date/time parsing alone.
        const auto& cfg = BehaviorController::instance().config();
        const bool mangle_unicode =
            cfg.silent_corruption ==
                DriverConfig::SilentCorruptionMode::MangleUnicode &&
            (effective_type == SQL_C_WCHAR ||
             effective_type == SQL_C_CHAR  ||
             effective_type == SQL_C_DEFAULT ||
             effective_type == SQL_ARD_TYPE);
        std::string mangled_buf;
        if (mangle_unicode) {
            mangled_buf = cell_value;
            for (auto& b : mangled_buf) {
                if (static_cast<unsigned char>(b) > 0x7F) b = '?';
            }
        }
        const std::string& value = mangle_unicode ? mangled_buf : cell_value;

        if (effective_type == SQL_C_WCHAR) {
            // Convert UTF-8 string to UTF-16 (SQLWCHAR)
            SQLSMALLINT wbytes = 0;
            SQLRETURN r = copy_string_to_wbuffer(value,
                              static_cast<SQLWCHAR*>(rgbValue),
                              static_cast<SQLINTEGER>(cbValueMax), &wbytes);
            // pcbValue reports total bytes needed (excl NUL), regardless of truncation
            if (pcbValue) *pcbValue = static_cast<SQLLEN>(wbytes);
            if (r == SQL_SUCCESS_WITH_INFO) {
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                    "String data, right truncated");
                return SQL_SUCCESS_WITH_INFO;
            }
        } else if (effective_type == SQL_C_TYPE_DATE) {
            // Parse date string "YYYY-MM-DD" into SQL_DATE_STRUCT
            SQL_DATE_STRUCT ds = {0, 0, 0};
            if (value.length() >= 10 && value[4] == '-' && value[7] == '-') {
                try {
                    ds.year = static_cast<SQLSMALLINT>(std::stoi(value.substr(0, 4)));
                    ds.month = static_cast<SQLUSMALLINT>(std::stoi(value.substr(5, 2)));
                    ds.day = static_cast<SQLUSMALLINT>(std::stoi(value.substr(8, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_DATE_STRUCT*>(rgbValue) = ds;
            if (pcbValue) *pcbValue = sizeof(SQL_DATE_STRUCT);
        } else if (effective_type == SQL_C_TYPE_TIME) {
            // Parse time string "HH:MM:SS" into SQL_TIME_STRUCT
            SQL_TIME_STRUCT ts = {0, 0, 0};
            if (value.length() >= 8 && value[2] == ':' && value[5] == ':') {
                try {
                    ts.hour = static_cast<SQLUSMALLINT>(std::stoi(value.substr(0, 2)));
                    ts.minute = static_cast<SQLUSMALLINT>(std::stoi(value.substr(3, 2)));
                    ts.second = static_cast<SQLUSMALLINT>(std::stoi(value.substr(6, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_TIME_STRUCT*>(rgbValue) = ts;
            if (pcbValue) *pcbValue = sizeof(SQL_TIME_STRUCT);
        } else if (effective_type == SQL_C_TYPE_TIMESTAMP) {
            // Parse timestamp string "YYYY-MM-DD HH:MM:SS" into SQL_TIMESTAMP_STRUCT
            SQL_TIMESTAMP_STRUCT tss = {0, 0, 0, 0, 0, 0, 0};
            if (value.length() >= 19) {
                try {
                    tss.year = static_cast<SQLSMALLINT>(std::stoi(value.substr(0, 4)));
                    tss.month = static_cast<SQLUSMALLINT>(std::stoi(value.substr(5, 2)));
                    tss.day = static_cast<SQLUSMALLINT>(std::stoi(value.substr(8, 2)));
                    tss.hour = static_cast<SQLUSMALLINT>(std::stoi(value.substr(11, 2)));
                    tss.minute = static_cast<SQLUSMALLINT>(std::stoi(value.substr(14, 2)));
                    tss.second = static_cast<SQLUSMALLINT>(std::stoi(value.substr(17, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_TIMESTAMP_STRUCT*>(rgbValue) = tss;
            if (pcbValue) *pcbValue = sizeof(SQL_TIMESTAMP_STRUCT);
        } else if (effective_type == SQL_C_SLONG || effective_type == SQL_C_LONG ||
                   effective_type == SQL_C_SBIGINT || effective_type == SQL_C_SSHORT ||
                   effective_type == SQL_C_SHORT || effective_type == SQL_C_STINYINT ||
                   effective_type == SQL_C_DOUBLE || effective_type == SQL_C_FLOAT) {
            // Found by A21 in Phase 2: a character cell requested as a numeric
            // C type used to fall into the ANSI branch below, which memcpy'd
            // the raw bytes into the caller's 4-byte SQLINTEGER — SELECT '123'
            // read back as 3355185 (0x333231, the ASCII digits). SQL_CHAR to
            // SQL_C_SLONG is a *required Core* conversion, so this is the mock
            // silently corrupting data on a path the spec makes mandatory.
            //
            // Narrow fix for the integer and floating targets; the wider
            // C-type gaps in this switch remain D11's, and unifying the four
            // independent C-type switches remains D18's.
            const std::string trimmed = [&] {
                auto b = value.find_first_not_of(" 	");
                auto e = value.find_last_not_of(" 	");
                return (b == std::string::npos) ? std::string()
                                                : value.substr(b, e - b + 1);
            }();

            bool ok = false;
            double dbl = 0.0;
            long long ival = 0;
            try {
                size_t consumed = 0;
                if (effective_type == SQL_C_DOUBLE || effective_type == SQL_C_FLOAT) {
                    dbl = std::stod(trimmed, &consumed);
                    ok = consumed == trimmed.size() && !trimmed.empty();
                } else {
                    ival = std::stoll(trimmed, &consumed);
                    ok = consumed == trimmed.size() && !trimmed.empty();
                }
            } catch (...) {
                ok = false;
            }

            if (!ok) {
                // 22018 is what the spec requires for a character value that
                // cannot be cast to the requested type — not a wrong number.
                stmt->add_diagnostic(sqlstate::INVALID_CHARACTER_VALUE, 0,
                                     "Invalid character value for cast specification");
                return SQL_ERROR;
            }

            if (rgbValue) {
                switch (effective_type) {
                    case SQL_C_SLONG:
                    case SQL_C_LONG:
                        *static_cast<SQLINTEGER*>(rgbValue) =
                            static_cast<SQLINTEGER>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
                        break;
                    case SQL_C_SBIGINT:
                        *static_cast<SQLBIGINT*>(rgbValue) =
                            static_cast<SQLBIGINT>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLBIGINT);
                        break;
                    case SQL_C_SSHORT:
                    case SQL_C_SHORT:
                        *static_cast<SQLSMALLINT*>(rgbValue) =
                            static_cast<SQLSMALLINT>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
                        break;
                    case SQL_C_STINYINT:
                        *static_cast<SQLSCHAR*>(rgbValue) =
                            static_cast<SQLSCHAR>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLSCHAR);
                        break;
                    case SQL_C_DOUBLE:
                        *static_cast<SQLDOUBLE*>(rgbValue) = dbl;
                        if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                        break;
                    case SQL_C_FLOAT:
                        *static_cast<SQLREAL*>(rgbValue) = static_cast<SQLREAL>(dbl);
                        if (pcbValue) *pcbValue = sizeof(SQLREAL);
                        break;
                    default:
                        break;
                }
            }
        } else {
            // SQL_C_CHAR or default - return ANSI.
            //
            // D18: the copy, the terminator, the "bytes still available"
            // report and the 01004 all come from copy_chars now, so this
            // branch is the *policy* (where the offset lives, when the
            // sequence ends) and none of the mechanics.
            //
            // D37: the offset is why this is not a one-shot copy. A caller
            // retrieving a long value calls repeatedly; each call continues
            // where the last one stopped, and the sequence ends with
            // SQL_NO_DATA rather than by repeating the first bytes forever.
            const size_t offset = std::min(stmt->getdata_offset_,
                                           value.length());
            const bool exhausted = offset >= value.length();

            if (offset > 0 && exhausted) {
                // The previous call returned the last of the value, and the
                // spec is explicit that a further call yields SQL_NO_DATA.
                stmt->getdata_col_ = 0;
                stmt->getdata_row_ = -1;
                stmt->getdata_offset_ = 0;
                return SQL_NO_DATA;
            }

            const BufferCopyResult res =
                copy_chars(value, offset, rgbValue, cbValueMax);

            // Only advance when bytes were actually delivered. A zero-length
            // ask reports the size and consumes nothing, so the next call
            // starts from the same place.
            stmt->getdata_offset_ = offset + res.copied;

            // Bytes still available *as of this call*, excluding the
            // terminator - not the length of the whole column.
            if (pcbValue) *pcbValue = res.remaining;

            if (res.truncated) {
                // D24: posted here rather than at each call site, so an
                // application can always tell truncation from any other
                // warning by its SQLSTATE.
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                     "String data, right truncated");
                return SQL_SUCCESS_WITH_INFO;
            }
            // The value is complete. A further call for this column takes the
            // `exhausted` branch above and reports SQL_NO_DATA.
        }
    }
    
    // For integer/double cells requested as SQL_C_WCHAR, convert via string
    if (fCType == SQL_C_WCHAR && !std::holds_alternative<std::string>(cell) &&
        !std::holds_alternative<std::monostate>(cell)) {
        std::string str;
        if (std::holds_alternative<long long>(cell)) {
            str = std::to_string(std::get<long long>(cell));
        } else if (std::holds_alternative<double>(cell)) {
            str = std::to_string(std::get<double>(cell));
        }
        SQLSMALLINT wbytes = 0;
        SQLRETURN r = copy_string_to_wbuffer(str,
                          static_cast<SQLWCHAR*>(rgbValue),
                          static_cast<SQLINTEGER>(cbValueMax), &wbytes);
        if (pcbValue) *pcbValue = static_cast<SQLLEN>(wbytes);
        if (r == SQL_SUCCESS_WITH_INFO) {
            stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                "String data, right truncated");
            return SQL_SUCCESS_WITH_INFO;
        }
        return SQL_SUCCESS;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLNumResultCols(
    SQLHSTMT hstmt,
    SQLSMALLINT* pccol) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLNumResultCols")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLNumResultCols failure");
            return SQL_ERROR;
        }
    }

    if (pccol) {
        *pccol = stmt->num_result_cols_;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLDescribeCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLCHAR* szColName,
    SQLSMALLINT cbColNameMax,
    SQLSMALLINT* pcbColName,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbColDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLDescribeCol")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLDescribeCol failure");
            return SQL_ERROR;
        }
    }

    if (icol < 1 || icol > static_cast<SQLUSMALLINT>(stmt->column_names_.size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const std::string& name = stmt->column_names_[icol - 1];
    SQLSMALLINT type = stmt->column_types_[icol - 1];
    
    if (szColName) {
        // D24: the return code was discarded, so a column name that did not
        // fit came back cut short with no diagnostic at all.
        if (copy_string_to_buffer(name, szColName, cbColNameMax, pcbColName)
                == SQL_SUCCESS_WITH_INFO) {
            stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                 "String data, right truncated");
        }
    } else if (pcbColName) {
        *pcbColName = static_cast<SQLSMALLINT>(name.length());
    }
    
    if (pfSqlType) *pfSqlType = type;
    
    // D14: the parser's own measurement first; the per-type default is a
    // fallback for a column the executor did not size, not the answer.
    if (pcbColDef && icol <= stmt->column_sizes_.size()
        && stmt->column_sizes_[icol - 1] != 0) {
        *pcbColDef = stmt->column_sizes_[icol - 1];
    } else if (pcbColDef) {
        switch (type) {
            case SQL_INTEGER: *pcbColDef = 10; break;
            case SQL_SMALLINT: *pcbColDef = 5; break;
            case SQL_BIGINT: *pcbColDef = 19; break;
            case SQL_VARCHAR:
            case SQL_WVARCHAR: *pcbColDef = 255; break;
            case SQL_CHAR:
            case SQL_WCHAR: *pcbColDef = 1; break;
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
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLBindCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLBindCol")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLBindCol failure");
            return SQL_ERROR;
        }
    }

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
MOCK_ENTRY_CATCH(hstmt)

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
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {
    
    (void)ibScale;

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLBindParameter")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLBindParameter failure");
            return SQL_ERROR;
        }
    }

    if (ipar == 0) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Parameter number must be >= 1");
        return SQL_ERROR;
    }
    
    // Validate C type
    switch (fCType) {
        case SQL_C_CHAR:
        case SQL_C_WCHAR:
        case SQL_C_SSHORT:
        case SQL_C_USHORT:
        case SQL_C_SLONG:
        case SQL_C_ULONG:
        case SQL_C_FLOAT:
        case SQL_C_DOUBLE:
        case SQL_C_BIT:
        case SQL_C_STINYINT:
        case SQL_C_UTINYINT:
        case SQL_C_SBIGINT:
        case SQL_C_UBIGINT:
        case SQL_C_BINARY:
        case SQL_C_TYPE_DATE:
        case SQL_C_TYPE_TIME:
        case SQL_C_TYPE_TIMESTAMP:
        case SQL_C_NUMERIC:
        case SQL_C_DEFAULT:
            break; // Valid
        default:
            stmt->add_diagnostic(sqlstate::INVALID_APPLICATION_BUFFER_TYPE, 0,
                                "Invalid application buffer type");
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
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLRowCount(
    SQLHSTMT hstmt,
    SQLLEN* pcrow) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLRowCount")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLRowCount failure");
            return SQL_ERROR;
        }
    }

    // D14: SQLRowCount on a statement that has not been executed is a
    // function sequence error. This used to answer 0, which an application
    // cannot tell from "the DELETE matched nothing".
    if (!stmt->executed_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                             "Statement has not been executed");
        return SQL_ERROR;
    }

    if (pcrow) {
        *pcrow = stmt->row_count_;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLCloseCursor")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLCloseCursor failure");
            return SQL_ERROR;
        }
    }

    if (!stmt->cursor_open_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor not open");
        return SQL_ERROR;
    }
    
    stmt->cursor_open_ = false;
    stmt->current_row_ = -1;
    stmt->result_data_.clear();
    // D85: and the part-retrieved value with it. The spec makes this the
    // equivalent of SQLFreeStmt(SQL_CLOSE), which has always done it; leaving
    // it out here is what let an offset outlive its result set.
    stmt->reset_getdata_continuation();

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLMoreResults")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLMoreResults failure");
            return SQL_ERROR;
        }
    }

    // Mock driver doesn't support multiple result sets
    return SQL_NO_DATA;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) MOCK_ENTRY_TRY {
    
    (void)cbValueMax;
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) {
        return SQL_INVALID_HANDLE;
    }
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLGetStmtAttr")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLGetStmtAttr failure");
            return SQL_ERROR;
        }
    }

    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE: {
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->cursor_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
        }
            
        case SQL_ATTR_CONCURRENCY:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->concurrency_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        // D14: three of these had fields on the handle already and were
        // simply never wired up; SQL_ATTR_CURSOR_SCROLLABLE had no field.
        // All four fell through to a default branch that returned
        // SQL_SUCCESS and left the caller's buffer untouched, so an
        // application read whatever was already there and believed it.
        case SQL_ATTR_MAX_LENGTH:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->max_length_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        case SQL_ATTR_NOSCAN:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->noscan_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        case SQL_ATTR_RETRIEVE_DATA:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->retrieve_data_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        case SQL_ATTR_CURSOR_SCROLLABLE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->cursor_scrollable_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        case SQL_ATTR_METADATA_ID:   // D25
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->metadata_id_;
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

        // D11: read back what the setter now stores.
        case SQL_ATTR_ROWS_FETCHED_PTR:
            if (rgbValue) *static_cast<SQLULEN**>(rgbValue) = stmt->rows_fetched_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;

        case SQL_ATTR_ROW_STATUS_PTR:
            if (rgbValue) {
                *static_cast<SQLUSMALLINT**>(rgbValue) = stmt->row_status_ptr_;
            }
            if (pcbValue) *pcbValue = sizeof(SQLUSMALLINT*);
            break;

        case SQL_ATTR_ROW_BIND_TYPE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->row_bind_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        case SQL_ATTR_ROW_BIND_OFFSET_PTR:
            if (rgbValue) {
                *static_cast<SQLULEN**>(rgbValue) = stmt->row_bind_offset_ptr_;
            }
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->paramset_size_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->async_enable_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        // Array parameter attributes
        case SQL_ATTR_PARAM_STATUS_PTR:
            if (rgbValue) *static_cast<SQLUSMALLINT**>(rgbValue) = stmt->param_status_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLUSMALLINT*);
            break;
            
        case SQL_ATTR_PARAMS_PROCESSED_PTR:
            if (rgbValue) *static_cast<SQLULEN**>(rgbValue) = stmt->params_processed_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;
            
        case SQL_ATTR_PARAM_BIND_TYPE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->param_bind_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
            if (rgbValue) *static_cast<SQLULEN**>(rgbValue) = stmt->param_bind_offset_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;
            
        case SQL_ATTR_PARAM_OPERATION_PTR:
            if (rgbValue) *static_cast<SQLUSMALLINT**>(rgbValue) = stmt->param_operation_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLUSMALLINT*);
            break;

        // Implicit descriptor handles — the DM queries these right after
        // SQLAllocHandle(SQL_HANDLE_STMT) to set up its internal dispatch.
        // Returning NULL causes a DM crash (ODBC32.dll access violation).
        case SQL_ATTR_APP_PARAM_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->app_param_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_IMP_PARAM_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->imp_param_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_APP_ROW_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->app_row_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_IMP_ROW_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->imp_row_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
            
        default:
            // D14: this answered SQL_SUCCESS for every attribute it did not
            // implement, leaving the caller's buffer untouched — so an
            // application read whatever was already there and believed the
            // driver had supplied it. HY092 is what "I do not know that
            // attribute" is spelled as.
            stmt->add_diagnostic(sqlstate::INVALID_ATTRIBUTE_IDENTIFIER, 0,
                                 "Invalid attribute/option identifier: "
                                 + std::to_string(fAttribute));
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) MOCK_ENTRY_TRY {
    
    (void)cbValue;

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLSetStmtAttr")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLSetStmtAttr failure");
            return SQL_ERROR;
        }
    }

    SQLULEN value = reinterpret_cast<SQLULEN>(rgbValue);
    
    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE:
            stmt->cursor_type_ = value;
            break;
            
        case SQL_ATTR_CONCURRENCY:
            stmt->concurrency_ = value;
            break;

        // D14: accepted silently before, so an application could not tell a
        // setting had been ignored. SQL_ATTR_CURSOR_SCROLLABLE also moves
        // cursor_type_, since asking for a scrollable cursor and then being
        // told the cursor is forward-only is exactly the contradiction a
        // conformance probe exists to catch.
        case SQL_ATTR_MAX_LENGTH:
            stmt->max_length_ = value;
            break;

        case SQL_ATTR_NOSCAN:
            stmt->noscan_ = value;
            break;

        case SQL_ATTR_RETRIEVE_DATA:
            stmt->retrieve_data_ = value;
            break;

        case SQL_ATTR_METADATA_ID:   // D25
            stmt->metadata_id_ = value;
            break;

        case SQL_ATTR_CURSOR_SCROLLABLE:
            stmt->cursor_scrollable_ = value;
            if (value == SQL_SCROLLABLE
                && stmt->cursor_type_ == SQL_CURSOR_FORWARD_ONLY) {
                stmt->cursor_type_ = SQL_CURSOR_STATIC;
            } else if (value == SQL_NONSCROLLABLE) {
                stmt->cursor_type_ = SQL_CURSOR_FORWARD_ONLY;
            }
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

        // D11: stored, and honoured by SQLFetch.
        case SQL_ATTR_ROWS_FETCHED_PTR:
            stmt->rows_fetched_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;

        case SQL_ATTR_ROW_STATUS_PTR:
            stmt->row_status_ptr_ = static_cast<SQLUSMALLINT*>(rgbValue);
            break;

        case SQL_ATTR_ROW_BIND_TYPE:
            stmt->row_bind_type_ = value;
            break;

        case SQL_ATTR_ROW_BIND_OFFSET_PTR:
            stmt->row_bind_offset_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            // PORT plan port 6 canary — SupportsArrayBind=false: report
            // option-not-supported when the application requests true
            // array-parameter execution (size > 1). Single-row binds
            // remain accepted so error-injected setup paths work.
            if (value > 1 &&
                !BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: array-parameter execution");
                return SQL_ERROR;
            }
            stmt->paramset_size_ = value;
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            stmt->async_enable_ = value;
            break;
            
        // Array parameter attributes.
        //
        // SupportsArrayBind=false also declines these two, extended in Phase 2
        // for A8: a driver with no array-parameter execution has no use for a
        // per-row status array or a processed-row counter, and real ones do
        // reject them. Without this there was no configuration in which a
        // driver declines an optional Level 1 attribute, so A8's SKIP branch
        // — the one that stops a correct driver being FAILed at Core — could
        // not be exercised.
        case SQL_ATTR_PARAM_STATUS_PTR:
            if (!BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: SQL_ATTR_PARAM_STATUS_PTR");
                return SQL_ERROR;
            }
            stmt->param_status_ptr_ = static_cast<SQLUSMALLINT*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAMS_PROCESSED_PTR:
            if (!BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: SQL_ATTR_PARAMS_PROCESSED_PTR");
                return SQL_ERROR;
            }
            stmt->params_processed_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAM_BIND_TYPE:
            stmt->param_bind_type_ = value;
            break;
            
        case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
            stmt->param_bind_offset_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAM_OPERATION_PTR:
            stmt->param_operation_ptr_ = static_cast<SQLUSMALLINT*>(rgbValue);
            break;
            
        default:
            // D14: silently accepting an attribute the driver does not
            // implement tells the application its setting took effect. The
            // spec distinguishes the two cases: HYC00 when the attribute is
            // recognised but unimplemented, HY092 when it is not recognised
            // at all. The mock knows every attribute it names above, so
            // anything reaching here is the latter.
            stmt->add_diagnostic(sqlstate::INVALID_ATTRIBUTE_IDENTIFIER, 0,
                                 "Invalid attribute/option identifier: "
                                 + std::to_string(fAttribute));
            return SQL_ERROR;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLFreeStmt(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;

    // D4: the `fOption != SQL_DROP` guard used to wrap only
    // clear_diagnostics(), so every option did its real work with no lock at
    // all. SQL_CLOSE clears result_data_, SQL_UNBIND clears
    // column_bindings_, SQL_RESET_PARAMS clears parameter_bindings_ - each of
    // which a concurrent SQLFetch iterates. That was the one place this
    // driver broke the invariant its own README states, and it is the shape
    // that corrupts rather than merely races.
    //
    // SQL_DROP is the exception, and has to stay outside the lock: it
    // deletes the object the mutex lives in, so unlocking afterwards would
    // touch freed memory.
    if (fOption == SQL_DROP) {
        delete stmt;
        return SQL_SUCCESS;
    }

    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    switch (fOption) {
        case SQL_CLOSE:
            stmt->cursor_open_ = false;
            // D14: `executed_` used to survive SQL_CLOSE, so a SQLFetch after
            // closing the cursor walked past the end of an emptied result set
            // and answered SQL_NO_DATA - "there are no more rows" - when the
            // truthful answer is 24000, "there is no cursor". An application
            // looping until SQL_NO_DATA could not tell it had lost its cursor.
            stmt->executed_ = false;
            stmt->current_row_ = -1;
            stmt->result_data_.clear();
            // D37: a closed cursor has no value to continue retrieving.
            stmt->reset_getdata_continuation();
            break;

        case SQL_UNBIND:
            stmt->column_bindings_.clear();
            break;

        case SQL_RESET_PARAMS:
            stmt->parameter_bindings_.clear();
            break;

        default:
            break;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLCancel(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // Mock: just reset state
    stmt->cursor_open_ = false;

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLNumParams(
    SQLHSTMT hstmt,
    SQLSMALLINT* pcpar) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLNumParams")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLNumParams failure");
            return SQL_ERROR;
        }
    }

    // D14: this counted every `?` in the statement text, including ones
    // inside string literals - `SELECT '?' FROM t WHERE a = ?` reported two
    // parameters. count_param_markers has done quote-aware counting all
    // along; SQLDescribeParam and the parser already used it.
    const int count = count_param_markers(stmt->sql_);    
    if (pcpar) *pcpar = static_cast<SQLSMALLINT>(count);
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLDescribeParam(
    SQLHSTMT hstmt,
    SQLUSMALLINT ipar,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbParamDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();
    // D36: fault injection reached 18 of the mock's 65 entry points, so most
    // probes had no configuration that could make them fail.
    {
        const auto& fi_config = BehaviorController::instance().config();
        if (fi_config.should_fail("SQLDescribeParam")) {
            stmt->add_diagnostic(fi_config.error_code, 0,
                                "Simulated SQLDescribeParam failure");
            return SQL_ERROR;
        }
    }

    if (!stmt->prepared_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Statement not prepared");
        return SQL_ERROR;
    }
    
    // Count parameter markers in the prepared SQL
    int num_params = 0;
    {
        bool in_sq = false, in_dq = false;
        for (size_t i = 0; i < stmt->sql_.length(); ++i) {
            char c = stmt->sql_[i];
            if (c == '\'' && !in_dq) {
                if (in_sq && i + 1 < stmt->sql_.length() && stmt->sql_[i + 1] == '\'') { ++i; continue; }
                in_sq = !in_sq;
            } else if (c == '"' && !in_sq) {
                in_dq = !in_dq;
            } else if (c == '?' && !in_sq && !in_dq) {
                ++num_params;
            }
        }
    }
    
    if (ipar < 1 || ipar > static_cast<SQLUSMALLINT>(num_params)) {
        stmt->add_diagnostic("HY000", 0,
                            "Invalid parameter number: " + std::to_string(ipar) +
                            " (statement has " + std::to_string(num_params) + " parameters)");
        return SQL_ERROR;
    }
    
    // D45: describe the parameter from the column it is bound to.
    //
    // This used to answer VARCHAR(255) for every parameter of every
    // statement, so the three SQLDescribeParam probes could not tell a
    // correct driver from one that has no idea what its own parameters are -
    // which is why they printed `expected` beside `actual` and never
    // compared them (B1). Describing an INTEGER parameter as VARCHAR(255) is
    // exactly the defect those probes exist to find.
    //
    // The parse is deliberately narrow: `INSERT INTO <t> (<cols>) VALUES
    // (?, ?, ...)`, which is the shape every probe in this suite uses. Any
    // statement it does not recognise keeps the old VARCHAR(255) answer,
    // which is a legal thing for a driver to say when it cannot infer more.
    SQLSMALLINT sql_type = SQL_VARCHAR;
    SQLULEN param_def = 255;
    SQLSMALLINT scale = 0;
    SQLSMALLINT nullable = SQL_NULLABLE;

    {
        ParsedQuery pq = parse_sql(stmt->sql_);
        if (pq.query_type == ParsedQuery::QueryType::Insert &&
            !pq.table_name.empty() &&
            ipar <= static_cast<SQLUSMALLINT>(pq.insert_columns.size())) {
            // D5: by value; see the note in execute_query.
            const auto table_copy =
                MockCatalog::instance().find_table(pq.table_name);
            const MockTable* table = table_copy ? &*table_copy : nullptr;
            if (table) {
                // `to_upper` is file-local to two other translation units,
                // so compare case-insensitively here rather than exporting it.
                auto same_name = [](const std::string& a, const std::string& b) {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); ++i) {
                        if (std::toupper(static_cast<unsigned char>(a[i])) !=
                            std::toupper(static_cast<unsigned char>(b[i]))) {
                            return false;
                        }
                    }
                    return true;
                };
                const std::string& want = pq.insert_columns[ipar - 1];
                for (const auto& col : table->columns) {
                    if (same_name(col.name, want)) {
                        sql_type  = col.data_type;
                        param_def = col.column_size;
                        scale     = static_cast<SQLSMALLINT>(col.decimal_digits);
                        nullable  = col.nullable;
                        break;
                    }
                }
            }
        }
    }

    if (pfSqlType) *pfSqlType = sql_type;
    if (pcbParamDef) *pcbParamDef = param_def;
    if (pibScale) *pibScale = scale;
    if (pfNullable) *pfNullable = nullable;

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

// ODBC 2.x compatibility functions
SQLRETURN SQL_API SQLGetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLPOINTER pvParam) MOCK_ENTRY_TRY {
    // Map to ODBC 3.x function
    return SQLGetStmtAttr(hstmt, fOption, pvParam, SQL_MAX_OPTION_STRING_LENGTH, NULL);
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLULEN vParam) MOCK_ENTRY_TRY {
    // Map to ODBC 3.x function
    return SQLSetStmtAttr(hstmt, fOption, reinterpret_cast<SQLPOINTER>(vParam), SQL_NTS);
}
MOCK_ENTRY_CATCH(hstmt)

} // extern "C"
