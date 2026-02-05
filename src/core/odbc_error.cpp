#include "odbc_error.hpp"
#include <sstream>
#include <cstring>

namespace odbc_crusher::core {

OdbcError OdbcError::from_handle(SQLSMALLINT handle_type, SQLHANDLE handle, const std::string& context) {
    std::vector<OdbcDiagnostic> diagnostics;
    
    SQLSMALLINT rec = 1;
    SQLCHAR sqlstate[6] = {0};
    SQLCHAR message[SQL_MAX_MESSAGE_LENGTH] = {0};
    SQLINTEGER native_error = 0;
    SQLSMALLINT text_length = 0;
    
    while (SQL_SUCCEEDED(SQLGetDiagRec(handle_type, handle, rec,
                                       sqlstate, &native_error,
                                       message, SQL_MAX_MESSAGE_LENGTH, &text_length))) {
        OdbcDiagnostic diag;
        diag.sqlstate = reinterpret_cast<char*>(sqlstate);
        diag.native_error = native_error;
        diag.message = reinterpret_cast<char*>(message);
        diag.record_number = rec;
        
        diagnostics.push_back(std::move(diag));
        rec++;
    }
    
    std::string error_msg = context.empty() ? "ODBC error" : context;
    return OdbcError(error_msg, std::move(diagnostics));
}

OdbcError::OdbcError(const std::string& message)
    : std::runtime_error(message) {
}

OdbcError::OdbcError(const std::string& message, std::vector<OdbcDiagnostic> diagnostics)
    : std::runtime_error(message), diagnostics_(std::move(diagnostics)) {
}

std::string OdbcError::format_diagnostics() const {
    std::ostringstream oss;
    oss << what() << "\n";
    
    for (const auto& diag : diagnostics_) {
        oss << "  [" << diag.sqlstate << "] (Native: " << diag.native_error << ") "
            << diag.message << "\n";
    }
    
    return oss.str();
}

void check_odbc_result(SQLRETURN ret, SQLSMALLINT handle_type, SQLHANDLE handle, const std::string& context) {
    if (!SQL_SUCCEEDED(ret)) {
        throw OdbcError::from_handle(handle_type, handle, context);
    }
}

} // namespace odbc_crusher::core
