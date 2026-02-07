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

void OdbcStatement::recycle() noexcept {
    // Close any open cursor and reset the statement state.
    // Uses SQL_CLOSE which silently succeeds even when no cursor is open,
    // unlike SQLCloseCursor which returns 24000 in that case.
    // This mimics PostgreSQL ODBC's SC_initialize_and_recycle() pattern
    // and is necessary for drivers like Firebird that can crash if
    // SQLExecDirect is called on a handle with a dirty cursor state.
    SQLFreeStmt(handle_, SQL_CLOSE);
    SQLFreeStmt(handle_, SQL_RESET_PARAMS);
}

void OdbcStatement::execute(std::string_view sql) {
    recycle();
    SQLRETURN ret = SQLExecDirect(handle_, (SQLCHAR*)sql.data(), static_cast<SQLINTEGER>(sql.length()));
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLExecDirect");
}

void OdbcStatement::prepare(std::string_view sql) {
    recycle();
    SQLRETURN ret = SQLPrepare(handle_, (SQLCHAR*)sql.data(), static_cast<SQLINTEGER>(sql.length()));
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLPrepare");
}

void OdbcStatement::execute_prepared() {
    // Close any open cursor from a previous execution, but don't reset
    // params since we're re-executing a prepared statement with bindings.
    SQLFreeStmt(handle_, SQL_CLOSE);
    SQLRETURN ret = SQLExecute(handle_);
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLExecute");
}

bool OdbcStatement::fetch() {
    SQLRETURN ret = SQLFetch(handle_);
    
    if (ret == SQL_NO_DATA) {
        return false;
    }
    
    // Allow SQL_SUCCESS_WITH_INFO (warnings)
    if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO) {
        return true;
    }
    
    // Other errors
    check_odbc_result(ret, SQL_HANDLE_STMT, handle_, "SQLFetch");
    return false;  // Should not reach here
}

void OdbcStatement::close_cursor() {
    // Use SQL_CLOSE which is safe even when no cursor is open
    SQLFreeStmt(handle_, SQL_CLOSE);
}

} // namespace odbc_crusher::core
