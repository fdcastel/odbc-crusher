#include "handles.hpp"
#include <algorithm>

namespace mock_odbc {

// OdbcHandle base class
OdbcHandle::OdbcHandle(HandleType type) 
    : magic_(HANDLE_MAGIC), type_(type) {
}

void OdbcHandle::clear_diagnostics() {
    diagnostics_.clear();
}

void OdbcHandle::add_diagnostic(const std::string& sqlstate, SQLINTEGER native_error,
                                 const std::string& message) {
    diagnostics_.push_back(make_diagnostic(sqlstate, native_error, message));
}

const DiagnosticRecord* OdbcHandle::get_diagnostic(SQLSMALLINT rec_number) const {
    if (rec_number < 1 || static_cast<size_t>(rec_number) > diagnostics_.size()) {
        return nullptr;
    }
    return &diagnostics_[rec_number - 1];
}

// EnvironmentHandle
EnvironmentHandle::EnvironmentHandle() : OdbcHandle(HandleType::ENV) {
}

EnvironmentHandle::~EnvironmentHandle() {
    // Clean up any remaining connections
    for (auto* conn : connections_) {
        delete conn;
    }
    connections_.clear();
    magic_ = 0;  // Invalidate handle
}

// ConnectionHandle
ConnectionHandle::ConnectionHandle(EnvironmentHandle* env) 
    : OdbcHandle(HandleType::DBC), env_(env) {
    if (env_) {
        env_->connections_.push_back(this);
    }
}

ConnectionHandle::~ConnectionHandle() {
    // Clean up any remaining statements
    for (auto* stmt : statements_) {
        delete stmt;
    }
    statements_.clear();
    
    // Remove from environment
    if (env_) {
        auto it = std::find(env_->connections_.begin(), env_->connections_.end(), this);
        if (it != env_->connections_.end()) {
            env_->connections_.erase(it);
        }
    }
    magic_ = 0;
}

// StatementHandle
StatementHandle::StatementHandle(ConnectionHandle* conn) 
    : OdbcHandle(HandleType::STMT), conn_(conn) {
    if (conn_) {
        conn_->statements_.push_back(this);
    }
    // The Windows DM calls SQLGetStmtAttrW for the four implicit
    // descriptor handles immediately after SQLAllocHandle(SQL_HANDLE_STMT).
    // If they are NULL the DM's internal statement structure is incomplete
    // and every subsequent statement-level call crashes (access violation
    // at ODBC32.dll+0x3E48).  Allocate them here unconditionally.
    app_param_desc_ = new DescriptorHandle(conn_, true);
    imp_param_desc_ = new DescriptorHandle(conn_, false);
    app_row_desc_   = new DescriptorHandle(conn_, true);
    imp_row_desc_   = new DescriptorHandle(conn_, false);
}

StatementHandle::~StatementHandle() {
    // Only free implicit descriptors. If the user swapped in an explicit
    // descriptor via SQLSetStmtAttr(SQL_ATTR_APP_{ROW,PARAM}_DESC), that
    // descriptor's lifetime belongs to the caller — deleting it here would
    // double-free. Implicit descriptors keep alloc_type_ == SQL_DESC_ALLOC_AUTO
    // (the default); explicit ones set SQL_DESC_ALLOC_USER.
    auto delete_if_implicit = [](DescriptorHandle* d) {
        if (d && d->alloc_type_ == SQL_DESC_ALLOC_AUTO) {
            delete d;
        }
    };
    delete_if_implicit(app_param_desc_);
    delete_if_implicit(imp_param_desc_);
    delete_if_implicit(app_row_desc_);
    delete_if_implicit(imp_row_desc_);

    // Remove from connection
    if (conn_) {
        auto it = std::find(conn_->statements_.begin(), conn_->statements_.end(), this);
        if (it != conn_->statements_.end()) {
            conn_->statements_.erase(it);
        }
    }
    magic_ = 0;
}

// DescriptorHandle
DescriptorHandle::DescriptorHandle(ConnectionHandle* conn, bool is_app_desc)
    : OdbcHandle(HandleType::DESC), conn_(conn), is_app_desc_(is_app_desc) {
}

DescriptorHandle::~DescriptorHandle() {
    magic_ = 0;
}

// Handle validation helpers
EnvironmentHandle* validate_env_handle(SQLHENV handle) {
    return validate_handle<EnvironmentHandle>(handle);
}

ConnectionHandle* validate_dbc_handle(SQLHDBC handle) {
    return validate_handle<ConnectionHandle>(handle);
}

StatementHandle* validate_stmt_handle(SQLHSTMT handle) {
    return validate_handle<StatementHandle>(handle);
}

DescriptorHandle* validate_desc_handle(SQLHDESC handle) {
    return validate_handle<DescriptorHandle>(handle);
}

} // namespace mock_odbc
