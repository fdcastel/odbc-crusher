#include "handles.hpp"
#include <algorithm>

namespace mock_odbc {

// OdbcHandle base class
OdbcHandle::OdbcHandle(HandleType type) 
    : magic_(HANDLE_MAGIC), type_(type) {
}

void OdbcHandle::clear_diagnostics() {
    diagnostics_.clear();
    // D22: an entry point clears the queue on the way in, which is also
    // the moment the previous call's return code stops being current.
    return_code_ = SQL_SUCCESS;
}

void OdbcHandle::add_diagnostic(const std::string& sqlstate, SQLINTEGER native_error,
                                 const std::string& message) {
    diagnostics_.push_back(make_diagnostic(sqlstate, native_error, message));
    // D22: keep SQL_DIAG_RETURNCODE consistent with what was posted. A
    // `01xxx` state is a warning; anything else is an error, and an
    // error already recorded is never downgraded by a later warning.
    const bool is_warning = sqlstate.compare(0, 2, "01") == 0;
    if (!is_warning) {
        return_code_ = SQL_ERROR;
    } else if (return_code_ != SQL_ERROR) {
        return_code_ = SQL_SUCCESS_WITH_INFO;
    }
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
    // D8(a): this used to range-for over `connections_` and delete each
    // element - but ~ConnectionHandle erases itself from this very vector,
    // so the iterator was invalidated on the first delete. Freeing an
    // environment with two or more connections skipped elements and then
    // read freed storage. Take the vector first, so the children erase
    // themselves from something empty.
    std::vector<ConnectionHandle*> doomed;
    doomed.swap(connections_);
    for (auto* conn : doomed) {
        delete conn;
    }
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
    // D8(a) - see ~EnvironmentHandle. ~StatementHandle erases itself from
    // `statements_`, so iterating it while deleting was undefined.
    std::vector<StatementHandle*> doomed;
    doomed.swap(statements_);
    for (auto* stmt : doomed) {
        delete stmt;
    }
    
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
