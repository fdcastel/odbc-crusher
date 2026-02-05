#include "odbc_statement.hpp"
#include "odbc_error.hpp"

namespace odbc_crusher::core {

OdbcStatement::OdbcStatement(OdbcConnection& conn)
    : conn_(conn) {
    SQLRETURN ret = SQLAllocHandle(SQL_HANDLE_STMT, conn_.get_handle(), &handle_);
    check_odbc_result(ret, SQL_HANDLE_DBC, conn_.get_handle(), "SQLAllocHandle(STMT)");
}

OdbcStatement::~OdbcStatement() {
    if (handle_ != SQL_NULL_HSTMT) {
        SQLFreeHandle(SQL_HANDLE_STMT, handle_);
    }
}

void OdbcStatement::execute(std::string_view sql) {
    SQLRETURN ret = SQLExecDirect(handle_, (SQLCHAR*)sql.data(), static_cast<SQLINTEGER>(sql.length()));
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLExecDirect");
}

void OdbcStatement::prepare(std::string_view sql) {
    SQLRETURN ret = SQLPrepare(handle_, (SQLCHAR*)sql.data(), static_cast<SQLINTEGER>(sql.length()));
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLPrepare");
}

void OdbcStatement::execute_prepared() {
    SQLRETURN ret = SQLExecute(handle_);
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLExecute");
}

bool OdbcStatement::fetch() {
    SQLRETURN ret = SQLFetch(handle_);
    
    if (ret == SQL_NO_DATA) {
        return false;
    }
    
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLFetch");
    return true;
}

void OdbcStatement::close_cursor() {
    SQLRETURN ret = SQLCloseCursor(handle_);
    if (ret != SQL_ERROR) {  // Allow SQL_SUCCESS_WITH_INFO
        return;
    }
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLCloseCursor");
}

} // namespace odbc_crusher::core
