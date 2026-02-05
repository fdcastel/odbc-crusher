#pragma once

#include "odbc_connection.hpp"
#include <string>
#include <string_view>

namespace odbc_crusher::core {

// RAII wrapper for ODBC Statement handle
class OdbcStatement {
public:
    explicit OdbcStatement(OdbcConnection& conn);
    ~OdbcStatement();
    
    // Non-copyable, non-movable (due to reference member)
    OdbcStatement(const OdbcStatement&) = delete;
    OdbcStatement& operator=(const OdbcStatement&) = delete;
    OdbcStatement(OdbcStatement&&) = delete;
    OdbcStatement& operator=(OdbcStatement&&) = delete;
    
    void execute(std::string_view sql);
    void prepare(std::string_view sql);
    void execute_prepared();
    
    bool fetch();
    void close_cursor();
    
    SQLHSTMT get_handle() const noexcept { return handle_; }
    OdbcConnection& get_connection() const noexcept { return conn_; }
    
private:
    SQLHSTMT handle_ = SQL_NULL_HSTMT;
    OdbcConnection& conn_;
};

} // namespace odbc_crusher::core
