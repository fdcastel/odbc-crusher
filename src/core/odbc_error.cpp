#include "guarded_buffer.hpp"
#include "odbc_error.hpp"
#include <sstream>
#include <cstring>

namespace odbc_crusher::core {

OdbcError OdbcError::from_handle(SQLSMALLINT handle_type, SQLHANDLE handle, const std::string& context) {
    std::vector<OdbcDiagnostic> diagnostics;
    
    SQLSMALLINT rec = 1;

    // D62: guarded. This is the diagnostic path every failure in the tool runs
    // through, and both buffers are handed to the driver manager, which scans
    // what it is given - a driver that omits its terminator leaves the
    // six-byte SQLSTATE array with no zero in it at all. The guard ends in a
    // stopper, so even a bare strlen stops inside memory this object owns.
    //
    // The lengths declared to SQLGetDiagRec are unchanged.
    constexpr size_t kSqlstateCapacity = 6;
    constexpr size_t kMessageCapacity = SQL_MAX_MESSAGE_LENGTH;
    GuardedBuffer<SQLCHAR> sqlstate(kSqlstateCapacity, 0);
    GuardedBuffer<SQLCHAR> message(kMessageCapacity, 0);
    SQLINTEGER native_error = 0;
    SQLSMALLINT text_length = 0;

    while (SQL_SUCCEEDED(SQLGetDiagRec(handle_type, handle, rec,
                                       sqlstate.data(), &native_error,
                                       message.data(),
                                       static_cast<SQLSMALLINT>(kMessageCapacity),
                                       &text_length))) {
        OdbcDiagnostic diag;
        // D69: SQLGetDiagRec gives no length for its SQLSTATE, so this is
        // bounded to the five characters the spec defines rather than to a
        // terminator the driver may not have written.
        diag.sqlstate = sqlstate_string(sqlstate.data());
        diag.native_error = native_error;
        // D68: `text_length` is right there and this used to walk to a
        // terminator instead. Two consequences, and the second is the one
        // that makes this the most important site in the row: `message` is
        // declared once and reused for every record, so a driver that
        // returns a short unterminated message on record 2 hands the report
        // record 1's tail glued to it. And a driver that terminates nothing
        // at all runs the read off a 512-byte stack array - in the function
        // every failure path in the tool calls to find out what went wrong.
        diag.message = bounded_string(reinterpret_cast<char*>(message.data()),
                                      kMessageCapacity, text_length).value;
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
