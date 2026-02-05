#pragma once

#include "../driver/common.hpp"
#include <string>
#include <vector>

namespace mock_odbc {

// SQL type information for SQLGetTypeInfo
struct MockTypeInfo {
    std::string type_name;
    SQLSMALLINT data_type;
    SQLINTEGER column_size;
    std::string literal_prefix;
    std::string literal_suffix;
    std::string create_params;
    SQLSMALLINT nullable;
    SQLSMALLINT case_sensitive;
    SQLSMALLINT searchable;
    SQLSMALLINT unsigned_attribute;
    SQLSMALLINT fixed_prec_scale;
    SQLSMALLINT auto_unique_value;
    std::string local_type_name;
    SQLSMALLINT minimum_scale;
    SQLSMALLINT maximum_scale;
    SQLSMALLINT sql_data_type;
    SQLSMALLINT sql_datetime_sub;
    SQLINTEGER num_prec_radix;
    SQLSMALLINT interval_precision;
};

// Get all supported types
std::vector<MockTypeInfo> get_mock_types(const std::string& preset = "AllTypes");

// Get type info for a specific SQL type
const MockTypeInfo* get_type_info(SQLSMALLINT data_type);

} // namespace mock_odbc
