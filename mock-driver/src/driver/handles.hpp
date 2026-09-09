#pragma once

#include "common.hpp"
#include "diagnostics.hpp"
#include <cstdint>
#include <memory>
#include <mutex>

namespace mock_odbc {

// I6: defined in mock/mock_txn.hpp. Forward-declared so the handle definitions
// stay free of the mock layer.
class TxnBuffer;

// Base class for all ODBC handles
class OdbcHandle {
public:
    explicit OdbcHandle(HandleType type);
    virtual ~OdbcHandle() = default;
    
    // Prevent copying
    OdbcHandle(const OdbcHandle&) = delete;
    OdbcHandle& operator=(const OdbcHandle&) = delete;
    
    // Handle validation
    bool is_valid() const { return magic_ == HANDLE_MAGIC; }
    HandleType type() const { return type_; }
    
    // Diagnostics
    void clear_diagnostics();
    void add_diagnostic(const std::string& sqlstate, SQLINTEGER native_error, 
                       const std::string& message);
    size_t diagnostic_count() const { return diagnostics_.size(); }
    const DiagnosticRecord* get_diagnostic(SQLSMALLINT rec_number) const;
    
    // Header fields for all handles - SQLGetDiagField record 0.
    //
    // D22: all four of cursor_row_count_, dynamic_function_,
    // dynamic_function_code_ and return_code_ were declared here and
    // read by diagnostic_api.cpp but never written anywhere, so the
    // header fields reported fiction. They are maintained now - see
    // set_return_code() below and record_dynamic_function() in
    // statement_api.cpp.
    //
    // D14: row_count_ is SQLLEN because that is what both SQLRowCount
    // and SQL_DIAG_ROW_COUNT deal in. StatementHandle used to declare a
    // second row_count_ of its own, which shadowed this one: SQLRowCount
    // wrote the derived field and SQLGetDiagField read the base field,
    // so the diagnostic header always said 0 and contradicted the
    // statement's own answer.
    SQLLEN cursor_row_count_ = 0;
    SQLINTEGER dynamic_function_code_ = 0;
    std::string dynamic_function_;
    SQLINTEGER number_ = 0;
    SQLRETURN return_code_ = SQL_SUCCESS;
    SQLLEN row_count_ = 0;

    // D22: SQL_DIAG_RETURNCODE is the return code of the last function
    // called on this handle. Rather than touch all 96 entry points, the
    // rule is derived from the diagnostics the call posts, which is the
    // same information: clearing the queue (which every entry point does
    // on the way in) resets it to SQL_SUCCESS, a posted `01xxx` state
    // makes it SQL_SUCCESS_WITH_INFO, and any other state makes it
    // SQL_ERROR, with an error never downgraded to a warning. The one
    // case this does not see is a call that returns SQL_NO_DATA without
    // posting anything; that reads back as SQL_SUCCESS.
    void set_return_code(SQLRETURN rc) { return_code_ = rc; }
    
    // Per-handle mutex for thread safety
    std::mutex& mutex() { return mutex_; }
    
protected:
    uint32_t magic_;
    HandleType type_;
    std::vector<DiagnosticRecord> diagnostics_;
    std::mutex mutex_;
};

// Environment Handle
class EnvironmentHandle : public OdbcHandle {
public:
    EnvironmentHandle();
    ~EnvironmentHandle() override;
    
    // Attributes
    SQLINTEGER odbc_version_ = SQL_OV_ODBC3;
    SQLINTEGER connection_pooling_ = SQL_CP_OFF;
    SQLINTEGER cp_match_ = SQL_CP_STRICT_MATCH;
    SQLINTEGER output_nts_ = SQL_TRUE;
    
    // Allocated connections
    std::vector<ConnectionHandle*> connections_;
};

// Connection Handle
class ConnectionHandle : public OdbcHandle {
public:
    explicit ConnectionHandle(EnvironmentHandle* env);
    ~ConnectionHandle() override;

    EnvironmentHandle* environment() const { return env_; }
    bool is_connected() const { return connected_; }

    // I6: this connection's identity and its uncommitted writes.
    //
    // The buffer is created at connect and released at disconnect, so a
    // reconnected handle starts with an empty one - which is what
    // test_reused_connection_starts_clean is about. It is deliberately *not*
    // tied to MockCatalog::attach: attach is refcounted across connections,
    // and a transaction belongs to exactly one.
    uint64_t id() const { return id_; }
    TxnBuffer* pending_writes() const { return writes_.get(); }
    void open_write_buffer();
    void close_write_buffer();
    
    // Connection state
    bool connected_ = false;
    std::string connection_string_;
    std::string dsn_;
    std::string uid_;
    std::string pwd_;
    
    // Attributes
    SQLUINTEGER access_mode_ = SQL_MODE_READ_WRITE;
    SQLUINTEGER autocommit_ = SQL_AUTOCOMMIT_ON;
    // D28: there was no transaction state at all, so SQLDisconnect could
    // never answer 25000 and turning autocommit back ON could not commit
    // what the manual-commit transaction had accumulated. A transaction
    // is open from the first statement executed with autocommit OFF
    // until SQLEndTran, which is how ODBC's implicit model works - there
    // is no BEGIN.
    bool in_transaction_ = false;
    SQLUINTEGER login_timeout_ = 0;
    SQLUINTEGER connection_timeout_ = 0;
    SQLUINTEGER txn_isolation_ = SQL_TXN_READ_COMMITTED;
    SQLUINTEGER current_catalog_ = 0;
    std::string current_catalog_name_;
    
    // Allocated statements
    std::vector<StatementHandle*> statements_;

private:
    EnvironmentHandle* env_;

    // I6. `writes_` holds an incomplete type, which is fine because
    // ~ConnectionHandle is defined out of line.
    uint64_t id_;
    std::shared_ptr<TxnBuffer> writes_;
};

// Statement Handle
class StatementHandle : public OdbcHandle {
public:
    explicit StatementHandle(ConnectionHandle* conn);
    ~StatementHandle() override;
    
    ConnectionHandle* connection() const { return conn_; }
    
    // Statement state
    bool prepared_ = false;
    bool executed_ = false;
    bool cursor_open_ = false;
    std::string sql_;
    
    // Result set
    SQLSMALLINT num_result_cols_ = 0;
    // D14: `SQLLEN row_count_` used to live here and shadow the base
    // class's field of the same name. Use OdbcHandle::row_count_.
    SQLLEN current_row_ = -1;

    // SQLGetData continuation state - D37.
    //
    // A character column longer than the caller's buffer is retrieved with
    // repeated SQLGetData calls, each continuing where the last stopped. The
    // mock used to restart from byte 0 every time, so a caller looping on
    // 01004 (which is what the ODBC spec tells it to do) never terminated.
    // The offset is keyed on (column, row) and checked inside SQLGetData, so
    // moving to another column or another row of the same result set restarts
    // the value with nothing to remember.
    //
    // D85: that is as far as the key goes, and the comment here used to claim
    // further - that re-execute, SQLFreeStmt(SQL_CLOSE) and the catalog
    // functions were covered too, "without having to remember to reset
    // anything". They are not. A *new result set* puts column 1 of row 0
    // where the old one had column 1 of row 0, so the coordinates are
    // unchanged and a stale offset is carried into a different value. Reading
    // the first row of `SELECT V FROM T WHERE ID = ?` twice returned the
    // value, then SQL_NO_DATA and an untouched buffer.
    //
    // A result set is therefore an explicit boundary: every site that
    // installs, clears or abandons `result_data_` calls
    // `reset_getdata_continuation()`. Grep for it - the set of callers is the
    // invariant.
    SQLUSMALLINT getdata_col_ = 0;
    SQLLEN getdata_row_ = -1;
    size_t getdata_offset_ = 0;

    // D85: forget any part-retrieved value. Cheap and idempotent, so a site
    // that is not sure whether it needs it should call it.
    void reset_getdata_continuation() {
        getdata_col_ = 0;
        getdata_row_ = -1;
        getdata_offset_ = 0;
    }
    
    // Attributes
    SQLULEN cursor_type_ = SQL_CURSOR_FORWARD_ONLY;
    SQLULEN concurrency_ = SQL_CONCUR_READ_ONLY;
    SQLULEN max_rows_ = 0;
    SQLULEN query_timeout_ = 0;
    SQLULEN row_array_size_ = 1;
    // D11: the rest of the block-fetch attributes. row_array_size_ was stored
    // and the others were not even that, so SQLFetch advanced one row and
    // wrote element 0 whatever the array size said - and reported SQL_SUCCESS,
    // with no fetched-rows count, so an application had no way to find out.
    SQLULEN* rows_fetched_ptr_ = nullptr;      // SQL_ATTR_ROWS_FETCHED_PTR
    SQLUSMALLINT* row_status_ptr_ = nullptr;   // SQL_ATTR_ROW_STATUS_PTR
    SQLULEN row_bind_type_ = SQL_BIND_BY_COLUMN;  // SQL_ATTR_ROW_BIND_TYPE
    SQLULEN* row_bind_offset_ptr_ = nullptr;   // SQL_ATTR_ROW_BIND_OFFSET_PTR
    // Which element of each bound array the delivery loop is filling.
    // Set by SQLFetch around each row of the set; 0 for a single-row fetch.
    SQLULEN row_set_element_ = 0;
    SQLULEN paramset_size_ = 1;
    SQLULEN async_enable_ = SQL_ASYNC_ENABLE_OFF;
    SQLULEN noscan_ = SQL_NOSCAN_OFF;
    SQLULEN max_length_ = 0;
    SQLULEN retrieve_data_ = SQL_RD_ON;
    // D14: the three above were stored but never wired to
    // SQLGetStmtAttr/SQLSetStmtAttr - the calls fell through to a
    // default branch that returned SQL_SUCCESS and left the caller's
    // buffer untouched. SQL_ATTR_CURSOR_SCROLLABLE had no field at all.
    SQLULEN cursor_scrollable_ = SQL_NONSCROLLABLE;
    // D25: SQL_ATTR_METADATA_ID had no implementation anywhere - a
    // client that set it still got pattern matching from every catalog
    // function, which is the one thing the attribute exists to turn off.
    SQLULEN metadata_id_ = SQL_FALSE;
    // D29: SQLSetCursorName discarded its argument and SQLGetCursorName
    // synthesised a name from the handle address - so set-then-get did
    // not round-trip, and a heap pointer leaked into a name applications
    // put in SQL text.
    std::string cursor_name_;
    // A stable ordinal for the fallback name, assigned at construction.
    unsigned long cursor_ordinal_ = 0;
    
    // Array parameter attributes (ODBC Arrays of Parameter Values)
    SQLUSMALLINT* param_status_ptr_ = nullptr;       // SQL_ATTR_PARAM_STATUS_PTR
    SQLULEN* params_processed_ptr_ = nullptr;         // SQL_ATTR_PARAMS_PROCESSED_PTR
    SQLULEN param_bind_type_ = SQL_PARAM_BIND_BY_COLUMN; // SQL_ATTR_PARAM_BIND_TYPE (0 = column-wise)
    SQLULEN* param_bind_offset_ptr_ = nullptr;        // SQL_ATTR_PARAM_BIND_OFFSET_PTR
    SQLUSMALLINT* param_operation_ptr_ = nullptr;     // SQL_ATTR_PARAM_OPERATION_PTR
    
    // Bound columns (column number -> binding info)
    struct ColumnBinding {
        SQLSMALLINT target_type;
        SQLPOINTER target_value;
        SQLLEN buffer_length;
        SQLLEN* str_len_or_ind;
    };
    std::unordered_map<SQLUSMALLINT, ColumnBinding> column_bindings_;
    
    // Bound parameters
    struct ParameterBinding {
        SQLSMALLINT input_output_type;
        SQLSMALLINT value_type;
        SQLSMALLINT param_type;
        SQLULEN column_size;
        SQLSMALLINT decimal_digits;
        SQLPOINTER param_value;
        SQLLEN buffer_length;
        SQLLEN* str_len_or_ind;
    };
    std::unordered_map<SQLUSMALLINT, ParameterBinding> parameter_bindings_;
    
    // Mock result data (populated after execute)
    std::vector<std::vector<std::variant<std::monostate, long long, double, std::string>>> result_data_;
    std::vector<std::string> column_names_;
    std::vector<SQLSMALLINT> column_types_;
    // D14: the executor produced real column sizes and threw them away,
    // so SQLDescribeCol hard-coded one per SQL type and reported every
    // VARCHAR as 255 whatever the DDL said.
    std::vector<SQLULEN> column_sizes_;
    
    // Descriptors
    DescriptorHandle* app_param_desc_ = nullptr;
    DescriptorHandle* imp_param_desc_ = nullptr;
    DescriptorHandle* app_row_desc_ = nullptr;
    DescriptorHandle* imp_row_desc_ = nullptr;
    
private:
    ConnectionHandle* conn_;
};

// Descriptor Handle
class DescriptorHandle : public OdbcHandle {
public:
    explicit DescriptorHandle(ConnectionHandle* conn, bool is_app_desc);
    ~DescriptorHandle() override;
    
    ConnectionHandle* connection() const { return conn_; }
    bool is_app_descriptor() const { return is_app_desc_; }
    
    // Descriptor fields
    SQLSMALLINT count_ = 0;
    SQLSMALLINT alloc_type_ = SQL_DESC_ALLOC_AUTO;
    
    struct DescriptorRecord {
        SQLSMALLINT type = 0;
        SQLSMALLINT concise_type = 0;
        SQLSMALLINT datetime_interval_code = 0;
        SQLINTEGER datetime_interval_precision = 0;
        SQLLEN display_size = 0;
        SQLLEN length = 0;
        std::string literal_prefix;
        std::string literal_suffix;
        std::string local_type_name;
        std::string name;
        SQLSMALLINT nullable = SQL_NULLABLE_UNKNOWN;
        SQLLEN octet_length = 0;
        SQLSMALLINT precision = 0;
        SQLSMALLINT scale = 0;
        std::string schema_name;
        std::string table_name;
        std::string catalog_name;
        SQLSMALLINT unnamed = SQL_NAMED;
        SQLSMALLINT unsigned_attr = SQL_FALSE;
        SQLPOINTER data_ptr = nullptr;
        SQLLEN* indicator_ptr = nullptr;
        SQLLEN* octet_length_ptr = nullptr;
    };
    
    std::vector<DescriptorRecord> records_;
    
private:
    ConnectionHandle* conn_;
    bool is_app_desc_;
};

// RAII lock guard for any OdbcHandle
// D18: SQLFetch and SQLFetchScroll each had their own copy of the loop that
// delivers a row to the bound columns, and they drifted - the SQLFetch one
// went through write_numeric_as when D11 rebuilt the conversion table and
// the other did not. Defined in statement_api.cpp, called by both.
SQLRETURN deliver_row_to_bound_columns(StatementHandle* stmt,
                                       const std::vector<std::variant<
                                           std::monostate, long long, double,
                                           std::string>>& row);

class HandleLock {
public:
    explicit HandleLock(OdbcHandle* h) : handle_(h) {
        if (handle_) handle_->mutex().lock();
    }
    ~HandleLock() {
        if (handle_) handle_->mutex().unlock();
    }
    HandleLock(const HandleLock&) = delete;
    HandleLock& operator=(const HandleLock&) = delete;
private:
    OdbcHandle* handle_;
};

// Handle validation helpers
template<typename T>
T* validate_handle(SQLHANDLE handle) {
    if (!handle) return nullptr;
    auto* h = static_cast<OdbcHandle*>(handle);
    if (!h->is_valid()) return nullptr;
    
    // Use manual type checking instead of dynamic_cast to avoid DLL boundary issues
    HandleType expected_type;
    if (std::is_same<T, EnvironmentHandle>::value) {
        expected_type = HandleType::ENV;
    } else if (std::is_same<T, ConnectionHandle>::value) {
        expected_type = HandleType::DBC;
    } else if (std::is_same<T, StatementHandle>::value) {
        expected_type = HandleType::STMT;
    } else if (std::is_same<T, DescriptorHandle>::value) {
        expected_type = HandleType::DESC;
    } else {
        return nullptr;
    }
    
    if (h->type() != expected_type) return nullptr;
    return static_cast<T*>(h);
}

EnvironmentHandle* validate_env_handle(SQLHENV handle);
ConnectionHandle* validate_dbc_handle(SQLHDBC handle);
StatementHandle* validate_stmt_handle(SQLHSTMT handle);
DescriptorHandle* validate_desc_handle(SQLHDESC handle);

} // namespace mock_odbc
