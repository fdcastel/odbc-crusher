#include "mock_types.hpp"
#include <algorithm>

namespace mock_odbc {

namespace {

std::vector<MockTypeInfo> all_types = {
    // Character types
    {"CHAR", SQL_CHAR, 255, "'", "'", "length", SQL_NULLABLE, SQL_TRUE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "CHAR", 0, 0, SQL_CHAR, 0, 0, 0},
    {"VARCHAR", SQL_VARCHAR, 65535, "'", "'", "max length", SQL_NULLABLE, SQL_TRUE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "VARCHAR", 0, 0, SQL_VARCHAR, 0, 0, 0},
    {"LONGVARCHAR", SQL_LONGVARCHAR, 2147483647, "'", "'", "", SQL_NULLABLE, SQL_TRUE, SQL_PRED_CHAR, SQL_FALSE, SQL_FALSE, SQL_FALSE, "TEXT", 0, 0, SQL_LONGVARCHAR, 0, 0, 0},
    
    // Unicode types
    {"WCHAR", SQL_WCHAR, 255, "N'", "'", "length", SQL_NULLABLE, SQL_TRUE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "NCHAR", 0, 0, SQL_WCHAR, 0, 0, 0},
    {"WVARCHAR", SQL_WVARCHAR, 65535, "N'", "'", "max length", SQL_NULLABLE, SQL_TRUE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "NVARCHAR", 0, 0, SQL_WVARCHAR, 0, 0, 0},
    {"WLONGVARCHAR", SQL_WLONGVARCHAR, 2147483647, "N'", "'", "", SQL_NULLABLE, SQL_TRUE, SQL_PRED_CHAR, SQL_FALSE, SQL_FALSE, SQL_FALSE, "NTEXT", 0, 0, SQL_WLONGVARCHAR, 0, 0, 0},
    
    // Binary types
    {"BINARY", SQL_BINARY, 255, "0x", "", "length", SQL_NULLABLE, SQL_FALSE, SQL_PRED_NONE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "BINARY", 0, 0, SQL_BINARY, 0, 0, 0},
    {"VARBINARY", SQL_VARBINARY, 65535, "0x", "", "max length", SQL_NULLABLE, SQL_FALSE, SQL_PRED_NONE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "VARBINARY", 0, 0, SQL_VARBINARY, 0, 0, 0},
    {"LONGVARBINARY", SQL_LONGVARBINARY, 2147483647, "0x", "", "", SQL_NULLABLE, SQL_FALSE, SQL_PRED_NONE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "BLOB", 0, 0, SQL_LONGVARBINARY, 0, 0, 0},
    
    // Exact numeric types
    {"DECIMAL", SQL_DECIMAL, 38, "", "", "precision,scale", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_TRUE, SQL_FALSE, "DECIMAL", 0, 38, SQL_DECIMAL, 0, 10, 0},
    {"NUMERIC", SQL_NUMERIC, 38, "", "", "precision,scale", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_TRUE, SQL_FALSE, "NUMERIC", 0, 38, SQL_NUMERIC, 0, 10, 0},
    {"SMALLINT", SQL_SMALLINT, 5, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_TRUE, SQL_FALSE, "SMALLINT", 0, 0, SQL_SMALLINT, 0, 10, 0},
    {"INTEGER", SQL_INTEGER, 10, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_TRUE, SQL_TRUE, "INTEGER", 0, 0, SQL_INTEGER, 0, 10, 0},
    {"BIGINT", SQL_BIGINT, 19, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_TRUE, SQL_TRUE, "BIGINT", 0, 0, SQL_BIGINT, 0, 10, 0},
    {"TINYINT", SQL_TINYINT, 3, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_TRUE, SQL_TRUE, SQL_FALSE, "TINYINT", 0, 0, SQL_TINYINT, 0, 10, 0},
    
    // Approximate numeric types
    {"REAL", SQL_REAL, 7, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "REAL", 0, 0, SQL_REAL, 0, 2, 0},
    {"FLOAT", SQL_FLOAT, 15, "", "", "precision", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "FLOAT", 0, 0, SQL_FLOAT, 0, 2, 0},
    {"DOUBLE", SQL_DOUBLE, 15, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DOUBLE PRECISION", 0, 0, SQL_DOUBLE, 0, 2, 0},
    
    // Date/time types
    {"DATE", SQL_TYPE_DATE, 10, "'", "'", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "DATE", 0, 0, SQL_DATETIME, SQL_CODE_DATE, 0, 0},
    {"TIME", SQL_TYPE_TIME, 8, "'", "'", "precision", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "TIME", 0, 6, SQL_DATETIME, SQL_CODE_TIME, 0, 0},
    {"TIMESTAMP", SQL_TYPE_TIMESTAMP, 26, "'", "'", "precision", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "TIMESTAMP", 0, 6, SQL_DATETIME, SQL_CODE_TIMESTAMP, 0, 0},
    
    // Boolean type
    {"BIT", SQL_BIT, 1, "", "", "", SQL_NULLABLE, SQL_FALSE, SQL_PRED_BASIC, SQL_FALSE, SQL_TRUE, SQL_FALSE, "BOOLEAN", 0, 0, SQL_BIT, 0, 0, 0},
    
    // GUID type
    {"GUID", SQL_GUID, 36, "'", "'", "", SQL_NULLABLE, SQL_FALSE, SQL_SEARCHABLE, SQL_FALSE, SQL_FALSE, SQL_FALSE, "UNIQUEIDENTIFIER", 0, 0, SQL_GUID, 0, 0, 0}
};

} // anonymous namespace

std::vector<MockTypeInfo> get_mock_types(const std::string& preset) {
    if (preset == "BasicTypes") {
        std::vector<MockTypeInfo> basic;
        for (const auto& type : all_types) {
            if (type.data_type == SQL_INTEGER || 
                type.data_type == SQL_VARCHAR ||
                type.data_type == SQL_TYPE_DATE) {
                basic.push_back(type);
            }
        }
        return basic;
    } else if (preset == "NumericOnly") {
        std::vector<MockTypeInfo> numeric;
        for (const auto& type : all_types) {
            if (type.data_type == SQL_SMALLINT ||
                type.data_type == SQL_INTEGER ||
                type.data_type == SQL_BIGINT ||
                type.data_type == SQL_DECIMAL ||
                type.data_type == SQL_NUMERIC ||
                type.data_type == SQL_REAL ||
                type.data_type == SQL_FLOAT ||
                type.data_type == SQL_DOUBLE) {
                numeric.push_back(type);
            }
        }
        return numeric;
    }
    
    return all_types;
}

const MockTypeInfo* get_type_info(SQLSMALLINT data_type) {
    for (const auto& type : all_types) {
        if (type.data_type == data_type) {
            return &type;
        }
    }
    return nullptr;
}

} // namespace mock_odbc
