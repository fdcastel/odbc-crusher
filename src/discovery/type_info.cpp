#include "type_info.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace odbc_crusher::discovery {

TypeInfo::TypeInfo(core::OdbcConnection& conn)
    : conn_(conn) {
}

void TypeInfo::collect() {
    types_.clear();
    
    core::OdbcStatement stmt(conn_);
    
    // Call SQLGetTypeInfo for all types
    SQLRETURN ret = SQLGetTypeInfo(stmt.get_handle(), SQL_ALL_TYPES);
    
    // Check if the call succeeded
    if (!SQL_SUCCEEDED(ret)) {
        // Extract diagnostics and throw
        throw core::OdbcError::from_handle(SQL_HANDLE_STMT, stmt.get_handle(), 
                                          "SQLGetTypeInfo failed");
    }
    
    // Bind columns - use SQLGetData instead of SQLBindCol for better compatibility
    DataTypeInfo type_info;
    SQLCHAR type_name[128] = {0};
    SQLCHAR literal_prefix[10] = {0};
    SQLCHAR literal_suffix[10] = {0};
    SQLCHAR create_params[128] = {0};
    SQLCHAR local_type_name[128] = {0};
    
    // Fetch all rows using SQLGetData instead of SQLBindCol
    while (stmt.fetch()) {
        SQLLEN indicator = 0;
        
        // Column 1: TYPE_NAME (required)
        SQLGetData(stmt.get_handle(), 1, SQL_C_CHAR, type_name, sizeof(type_name), &indicator);
        type_info.type_name = (indicator != SQL_NULL_DATA && indicator != SQL_NO_TOTAL) 
            ? reinterpret_cast<char*>(type_name) : "";
        
        // Column 2: DATA_TYPE (required)
        SQLGetData(stmt.get_handle(), 2, SQL_C_SSHORT, &type_info.data_type, 0, nullptr);
        
        // Column 3: COLUMN_SIZE  
        SQLGetData(stmt.get_handle(), 3, SQL_C_SLONG, &type_info.column_size, 0, nullptr);
        
        // Column 4: LITERAL_PREFIX
        SQLGetData(stmt.get_handle(), 4, SQL_C_CHAR, literal_prefix, sizeof(literal_prefix), &indicator);
        type_info.literal_prefix = (indicator != SQL_NULL_DATA && indicator != SQL_NO_TOTAL)
            ? reinterpret_cast<char*>(literal_prefix) : "";
        
        // Column 5: LITERAL_SUFFIX
        SQLGetData(stmt.get_handle(), 5, SQL_C_CHAR, literal_suffix, sizeof(literal_suffix), &indicator);
        type_info.literal_suffix = (indicator != SQL_NULL_DATA && indicator != SQL_NO_TOTAL)
            ? reinterpret_cast<char*>(literal_suffix) : "";
        
        // Column 6: CREATE_PARAMS
        SQLGetData(stmt.get_handle(), 6, SQL_C_CHAR, create_params, sizeof(create_params), &indicator);
        type_info.create_params = (indicator != SQL_NULL_DATA && indicator != SQL_NO_TOTAL)
            ? reinterpret_cast<char*>(create_params) : "";
        
        // Column 7: NULLABLE
        SQLGetData(stmt.get_handle(), 7, SQL_C_SSHORT, &type_info.nullable, 0, nullptr);
        
        // Column 8: CASE_SENSITIVE
        SQLGetData(stmt.get_handle(), 8, SQL_C_SSHORT, &type_info.case_sensitive, 0, nullptr);
        
        // Column 9: SEARCHABLE
        SQLGetData(stmt.get_handle(), 9, SQL_C_SSHORT, &type_info.searchable, 0, nullptr);
        
        // Column 10: UNSIGNED_ATTRIBUTE
        SQLGetData(stmt.get_handle(), 10, SQL_C_SSHORT, &type_info.unsigned_attribute, 0, nullptr);
        
        // Column 11: FIXED_PREC_SCALE
        SQLGetData(stmt.get_handle(), 11, SQL_C_SSHORT, &type_info.fixed_prec_scale, 0, nullptr);
        
        // Column 12: AUTO_UNIQUE_VALUE
        SQLGetData(stmt.get_handle(), 12, SQL_C_SSHORT, &type_info.auto_unique_value, 0, nullptr);
        
        // Column 13: LOCAL_TYPE_NAME
        SQLGetData(stmt.get_handle(), 13, SQL_C_CHAR, local_type_name, sizeof(local_type_name), &indicator);
        type_info.local_type_name = (indicator != SQL_NULL_DATA && indicator != SQL_NO_TOTAL)
            ? reinterpret_cast<char*>(local_type_name) : "";
        
        // Column 14: MINIMUM_SCALE
        SQLGetData(stmt.get_handle(), 14, SQL_C_SSHORT, &type_info.minimum_scale, 0, nullptr);
        
        // Column 15: MAXIMUM_SCALE
        SQLGetData(stmt.get_handle(), 15, SQL_C_SSHORT, &type_info.maximum_scale, 0, nullptr);
        
        // Column 16: SQL_DATA_TYPE
        SQLGetData(stmt.get_handle(), 16, SQL_C_SSHORT, &type_info.sql_data_type, 0, nullptr);
        
        // Column 17: SQL_DATETIME_SUB
        SQLGetData(stmt.get_handle(), 17, SQL_C_SSHORT, &type_info.sql_datetime_sub, 0, nullptr);
        
        // Column 18: NUM_PREC_RADIX
        SQLGetData(stmt.get_handle(), 18, SQL_C_SLONG, &type_info.num_prec_radix, 0, nullptr);
        
        types_.push_back(type_info);
        
        // Clear buffers for next row
        memset(type_name, 0, sizeof(type_name));
        memset(literal_prefix, 0, sizeof(literal_prefix));
        memset(literal_suffix, 0, sizeof(literal_suffix));
        memset(create_params, 0, sizeof(create_params));
        memset(local_type_name, 0, sizeof(local_type_name));
    }
}

std::string TypeInfo::format_summary() const {
    std::ostringstream oss;
    
    oss << "\nSupported Data Types (" << types_.size() << " types):\n";
    oss << "===============================================\n";
    
    for (const auto& type : types_) {
        oss << std::left << std::setw(20) << type.type_name
            << " | SQL Type: " << std::setw(5) << type.data_type
            << " | Size: " << std::setw(10) << type.column_size
            << " | Nullable: " << (type.nullable == SQL_NULLABLE ? "Yes" : "No")
            << "\n";
    }
    
    return oss.str();
}
std::vector<TypeInfo::DataType> TypeInfo::get_types() const {
    std::vector<DataType> result;
    result.reserve(types_.size());
    
    for (const auto& t : types_) {
        DataType dt;
        dt.type_name = t.type_name;
        dt.sql_data_type = t.sql_data_type;
        dt.column_size = t.column_size;
        dt.nullable = (t.nullable == SQL_NULLABLE);
        
        if (t.auto_unique_value == SQL_TRUE) {
            dt.auto_unique_value = true;
        } else if (t.auto_unique_value == SQL_FALSE) {
            dt.auto_unique_value = false;
        } else {
            dt.auto_unique_value = std::nullopt;
        }
        
        result.push_back(dt);
    }
    
    return result;
}
} // namespace odbc_crusher::discovery
