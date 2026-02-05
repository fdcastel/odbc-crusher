#pragma once

#ifdef _WIN32
#include <windows.h>
#endif

#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::core {

// RAII wrapper for ODBC Environment handle
class OdbcEnvironment {
public:
    OdbcEnvironment();
    ~OdbcEnvironment();
    
    // Non-copyable
    OdbcEnvironment(const OdbcEnvironment&) = delete;
    OdbcEnvironment& operator=(const OdbcEnvironment&) = delete;
    
    // Movable
    OdbcEnvironment(OdbcEnvironment&& other) noexcept;
    OdbcEnvironment& operator=(OdbcEnvironment&& other) noexcept;
    
    SQLHENV get_handle() const noexcept { return handle_; }
    SQLHENV get() const noexcept { return handle_; } // Alias for consistency
    
private:
    SQLHENV handle_ = SQL_NULL_HENV;
};

} // namespace odbc_crusher::core
