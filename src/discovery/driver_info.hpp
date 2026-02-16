#pragma once

#include "core/odbc_connection.hpp"
#include <string>
#include <map>
#include <optional>
#include <vector>

namespace odbc_crusher::discovery {

// Driver and DBMS information collected via SQLGetInfo
class DriverInfo {
public:
    // Structured properties for reporting
    struct Properties {
        std::string driver_name;
        std::string driver_ver;
        std::string driver_odbc_ver;
        std::string odbc_ver;
        std::string dbms_name;
        std::string dbms_ver;
        std::string database_name;
        std::string server_name;
        std::string user_name;
        std::string sql_conformance;
        std::string catalog_term;
        std::string schema_term;
        std::string table_term;
        std::string procedure_term;
        std::string identifier_quote_char;
    };

    // Scalar function support info
    struct ScalarFunctionSupport {
        std::vector<std::string> string_functions;
        std::vector<std::string> numeric_functions;
        std::vector<std::string> timedate_functions;
        std::vector<std::string> system_functions;
        SQLUINTEGER string_bitmask = 0;
        SQLUINTEGER numeric_bitmask = 0;
        SQLUINTEGER timedate_bitmask = 0;
        SQLUINTEGER system_bitmask = 0;
        SQLUINTEGER convert_functions_bitmask = 0;
        SQLUINTEGER oj_capabilities = 0;
        SQLUINTEGER datetime_literals = 0;
        SQLUINTEGER timedate_add_intervals = 0;
        SQLUINTEGER timedate_diff_intervals = 0;
        // Type conversion matrix: info_type -> bitmask
        std::map<std::string, SQLUINTEGER> convert_matrix;
    };
    
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
    
    // Get structured properties for reporting
    Properties get_properties() const;

    // Get scalar function support info
    const ScalarFunctionSupport& get_scalar_functions() const { return scalar_functions_; }
    
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
    
    // Scalar function support
    ScalarFunctionSupport scalar_functions_;
    
    // All collected info
    std::map<std::string, std::string> info_map_;
    
    // Helper to get string info
    std::optional<std::string> get_info_string(SQLUSMALLINT info_type);
    
    // Helper to get integer info
    std::optional<SQLUINTEGER> get_info_uint(SQLUSMALLINT info_type);

    // Collect scalar function capabilities
    void collect_scalar_functions();
};

} // namespace odbc_crusher::discovery
