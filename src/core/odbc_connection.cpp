#include "odbc_connection.hpp"
#include "odbc_error.hpp"

namespace odbc_crusher::core {

OdbcConnection::OdbcConnection(OdbcEnvironment& env)
    : env_(env) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_DBC, env_.get_handle(), &handle_);
    check_odbc_result(ret, SQL_HANDLE_ENV, env_.get_handle(), "SQLAllocHandle(DBC)");
}

OdbcConnection::~OdbcConnection() {
    if (connected_) {
        try {
            disconnect();
        } catch (...) {
            // Suppress exceptions in destructor
        }
    }
    
    if (handle_ != SQL_NULL_HDBC) {
        SQLFreeHandle(SQL_HANDLE_DBC, handle_);
    }
}

void OdbcConnection::connect(std::string_view connection_string) {
    if (connected_) {
        throw OdbcError("Already connected");
    }
    
    SQLCHAR out_conn_str[1024];
    SQLSMALLINT out_conn_str_len;
    
    SQLRETURN ret = SQLDriverConnect(
        handle_,
        nullptr,  // No window handle
        (SQLCHAR*)connection_string.data(),
        static_cast<SQLSMALLINT>(connection_string.length()),
        out_conn_str,
        sizeof(out_conn_str),
        &out_conn_str_len,
        SQL_DRIVER_NOPROMPT
    );
    
    check_odbc_result(ret, SQL_HANDLE_DBC, handle_, "SQLDriverConnect");
    connected_ = true;
}

void OdbcConnection::disconnect() {
    if (!connected_) {
        return;
    }
    
    SQLRETURN ret = SQLDisconnect(handle_);
    check_odbc_result(ret, SQL_HANDLE_DBC, handle_, "SQLDisconnect");
    connected_ = false;
}

} // namespace odbc_crusher::core
