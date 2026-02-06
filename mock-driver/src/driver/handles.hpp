#pragma once

#include "common.hpp"
#include "diagnostics.hpp"
#include <cstdint>
#include <mutex>

namespace mock_odbc {

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
    
    // Header fields for all handles
    SQLINTEGER cursor_row_count_ = 0;
    SQLINTEGER dynamic_function_code_ = 0;
    std::string dynamic_function_;
    SQLINTEGER number_ = 0;
    SQLRETURN return_code_ = SQL_SUCCESS;
    SQLINTEGER row_count_ = 0;
    
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
    
    // Connection state
    bool connected_ = false;
    std::string connection_string_;
    std::string dsn_;
    std::string uid_;
    std::string pwd_;
    
    // Attributes
    SQLUINTEGER access_mode_ = SQL_MODE_READ_WRITE;
    SQLUINTEGER autocommit_ = SQL_AUTOCOMMIT_ON;
    SQLUINTEGER login_timeout_ = 0;
    SQLUINTEGER connection_timeout_ = 0;
    SQLUINTEGER txn_isolation_ = SQL_TXN_READ_COMMITTED;
    SQLUINTEGER current_catalog_ = 0;
    std::string current_catalog_name_;
    
    // Allocated statements
    std::vector<StatementHandle*> statements_;
    
private:
    EnvironmentHandle* env_;
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
    SQLLEN row_count_ = 0;
    SQLLEN current_row_ = -1;
    
    // Attributes
    SQLULEN cursor_type_ = SQL_CURSOR_FORWARD_ONLY;
    SQLULEN concurrency_ = SQL_CONCUR_READ_ONLY;
    SQLULEN max_rows_ = 0;
    SQLULEN query_timeout_ = 0;
    SQLULEN row_array_size_ = 1;
    SQLULEN paramset_size_ = 1;
    SQLULEN async_enable_ = SQL_ASYNC_ENABLE_OFF;
    SQLULEN noscan_ = SQL_NOSCAN_OFF;
    SQLULEN max_length_ = 0;
    SQLULEN retrieve_data_ = SQL_RD_ON;
    
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
