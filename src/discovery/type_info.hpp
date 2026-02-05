#pragma once

#include "core/odbc_connection.hpp"
#include <string>
#include <vector>

namespace odbc_crusher::discovery {

// Information about a single data type
struct DataTypeInfo {
    std::string type_name;          // Data type name (e.g., "VARCHAR")
    SQLSMALLINT data_type;          // SQL data type code
    SQLINTEGER column_size;         // Maximum column size
    std::string literal_prefix;     // Literal prefix (e.g., "'")
    std::string literal_suffix;     // Literal suffix (e.g., "'")
    std::string create_params;      // Create parameters (e.g., "length")
    SQLSMALLINT nullable;           // Nullable (SQL_NO_NULLS, SQL_NULLABLE, SQL_NULLABLE_UNKNOWN)
    SQLSMALLINT case_sensitive;     // Case sensitive (SQL_TRUE, SQL_FALSE)
    SQLSMALLINT searchable;         // Searchable
    SQLSMALLINT unsigned_attribute; // Unsigned
    SQLSMALLINT fixed_prec_scale;   // Fixed precision/scale
    SQLSMALLINT auto_unique_value;  // Auto-incrementing
    std::string local_type_name;    // Localized type name
    SQLSMALLINT minimum_scale;      // Minimum scale
    SQLSMALLINT maximum_scale;      // Maximum scale
    SQLSMALLINT sql_data_type;      // SQL data type
    SQLSMALLINT sql_datetime_sub;   // SQL datetime subcode
    SQLINTEGER num_prec_radix;      // Numeric precision radix
};

// Type information collected via SQLGetTypeInfo
class TypeInfo {
public:
    explicit TypeInfo(core::OdbcConnection& conn);
    
    // Collect all type information
    void collect();
    
    // Get all types
    const std::vector<DataTypeInfo>& types() const { return types_; }
    
    // Get types count
    size_t count() const { return types_.size(); }
    
    // Display summary
    std::string format_summary() const;
    
private:
    core::OdbcConnection& conn_;
    std::vector<DataTypeInfo> types_;
};

} // namespace odbc_crusher::discovery
