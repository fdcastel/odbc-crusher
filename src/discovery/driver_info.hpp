#pragma once

#include "core/odbc_connection.hpp"
#include <string>
#include <map>
#include <optional>

namespace odbc_crusher::discovery {

// Driver and DBMS information collected via SQLGetInfo
class DriverInfo {
public:
    explicit DriverInfo(core::OdbcConnection& conn);
    
    // Collect all information
    void collect();
    
    // Driver information
    std::optional<std::string> driver_name() const { return driver_name_; }
    std::optional<std::string> driver_version() const { return driver_version_; }
    std::optional<std::string> driver_odbc_version() const { return driver_odbc_version_; }
    
    // DBMS information  
    std::optional<std::string> dbms_name() const { return dbms_name_; }
    std::optional<std::string> dbms_version() const { return dbms_version_; }
    
    // Capabilities
    std::optional<std::string> sql_conformance() const { return sql_conformance_; }
    std::optional<std::string> odbc_interface_conformance() const { return odbc_interface_conformance_; }
    
    // Get all collected info
    const std::map<std::string, std::string>& all_info() const { return info_map_; }
    
    // Display summary
    std::string format_summary() const;
    
private:
    core::OdbcConnection& conn_;
    
    // Cached information
    std::optional<std::string> driver_name_;
    std::optional<std::string> driver_version_;
    std::optional<std::string> driver_odbc_version_;
    std::optional<std::string> dbms_name_;
    std::optional<std::string> dbms_version_;
    std::optional<std::string> sql_conformance_;
    std::optional<std::string> odbc_interface_conformance_;
    
    // All collected info
    std::map<std::string, std::string> info_map_;
    
    // Helper to get string info
    std::optional<std::string> get_info_string(SQLUSMALLINT info_type);
    
    // Helper to get integer info
    std::optional<SQLUINTEGER> get_info_uint(SQLUSMALLINT info_type);
};

} // namespace odbc_crusher::discovery
