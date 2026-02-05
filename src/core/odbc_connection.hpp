#pragma once

#include "odbc_environment.hpp"
#include <string>
#include <string_view>

namespace odbc_crusher::core {

// RAII wrapper for ODBC Connection handle
class OdbcConnection {
public:
    explicit OdbcConnection(OdbcEnvironment& env);
    ~OdbcConnection();
    
    // Non-copyable, non-movable (due to reference member)
    OdbcConnection(const OdbcConnection&) = delete;
    OdbcConnection& operator=(const OdbcConnection&) = delete;
    OdbcConnection(OdbcConnection&&) = delete;
    OdbcConnection& operator=(OdbcConnection&&) = delete;
    
    void connect(std::string_view connection_string);
    void disconnect();
    bool is_connected() const noexcept { return connected_; }
    
    SQLHDBC get_handle() const noexcept { return handle_; }
    OdbcEnvironment& get_environment() const noexcept { return env_; }
    
private:
    SQLHDBC handle_ = SQL_NULL_HDBC;
    OdbcEnvironment& env_;
    bool connected_ = false;
};

} // namespace odbc_crusher::core
