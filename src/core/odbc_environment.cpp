#include "odbc_environment.hpp"
#include "odbc_error.hpp"

namespace odbc_crusher::core {

OdbcEnvironment::OdbcEnvironment() {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &handle_);
    check_odbc_result(ret, SQL_HANDLE_ENV, SQL_NULL_HANDLE, "SQLAllocHandle(ENV)");
    
    // Set ODBC version to 3.x
    ret = SQLSetEnvAttr(handle_, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    check_odbc_result(ret, SQL_HANDLE_ENV, handle_, "SQLSetEnvAttr(ODBC_VERSION)");
}

OdbcEnvironment::~OdbcEnvironment() {
    if (handle_ != SQL_NULL_HENV) {
        SQLFreeHandle(SQL_HANDLE_ENV, handle_);
    }
}

OdbcEnvironment::OdbcEnvironment(OdbcEnvironment&& other) noexcept
    : handle_(other.handle_) {
    other.handle_ = SQL_NULL_HENV;
}

OdbcEnvironment& OdbcEnvironment::operator=(OdbcEnvironment&& other) noexcept {
    if (this != &other) {
        if (handle_ != SQL_NULL_HENV) {
            SQLFreeHandle(SQL_HANDLE_ENV, handle_);
        }
        handle_ = other.handle_;
        other.handle_ = SQL_NULL_HENV;
    }
    return *this;
}

} // namespace odbc_crusher::core
