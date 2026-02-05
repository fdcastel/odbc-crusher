#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::core {

// Diagnostic record from SQLGetDiagRec
struct OdbcDiagnostic {
    std::string sqlstate;           // 5-character SQLSTATE code
    SQLINTEGER native_error;        // Driver-specific error code
    std::string message;            // Error message
    SQLSMALLINT record_number;      // Diagnostic record number
};

// Exception class for ODBC errors
class OdbcError : public std::runtime_error {
public:
    // Extract all diagnostic records from a handle
    static OdbcError from_handle(SQLSMALLINT handle_type, SQLHANDLE handle, const std::string& context = "");
    
    explicit OdbcError(const std::string& message);
    OdbcError(const std::string& message, std::vector<OdbcDiagnostic> diagnostics);
    
    const std::vector<OdbcDiagnostic>& diagnostics() const noexcept { return diagnostics_; }
    
    std::string format_diagnostics() const;

private:
    std::vector<OdbcDiagnostic> diagnostics_;
};

// Check ODBC return code and throw on error
void check_odbc_result(SQLRETURN ret, SQLSMALLINT handle_type, SQLHANDLE handle, const std::string& context);

} // namespace odbc_crusher::core
