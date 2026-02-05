#include "mock_data.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>

namespace mock_odbc {

namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Simple name generator
std::string generate_name(int index) {
    static const char* first_names[] = {
        "John", "Jane", "Bob", "Alice", "Charlie", "Diana", "Eve", "Frank",
        "Grace", "Henry", "Ivy", "Jack", "Kate", "Leo", "Mia", "Noah"
    };
    return first_names[index % 16];
}

std::string generate_email(int index) {
    return "user" + std::to_string(index) + "@example.com";
}

std::string generate_product_name(int index) {
    static const char* products[] = {
        "Widget", "Gadget", "Gizmo", "Device", "Tool", "Appliance", "Machine",
        "Instrument", "Component", "Module", "Unit", "System", "Kit", "Set"
    };
    return std::string(products[index % 14]) + " " + std::to_string(index);
}

} // anonymous namespace

CellValue generate_value(const MockColumn& column, int row_index) {
    std::string upper_name = to_upper(column.name);
    
    switch (column.data_type) {
        case SQL_INTEGER:
        case SQL_BIGINT:
        case SQL_SMALLINT:
        case SQL_TINYINT:
            if (column.is_auto_increment || upper_name.find("_ID") != std::string::npos) {
                return static_cast<long long>(row_index + 1);
            }
            if (upper_name.find("QUANTITY") != std::string::npos) {
                return static_cast<long long>((row_index % 10) + 1);
            }
            if (upper_name.find("STOCK") != std::string::npos) {
                return static_cast<long long>((row_index * 7) % 100 + 10);
            }
            return static_cast<long long>(row_index * 10);
            
        case SQL_DECIMAL:
        case SQL_NUMERIC:
        case SQL_REAL:
        case SQL_FLOAT:
        case SQL_DOUBLE:
            if (upper_name.find("PRICE") != std::string::npos) {
                return 9.99 + (row_index % 100);
            }
            if (upper_name.find("AMOUNT") != std::string::npos || 
                upper_name.find("BALANCE") != std::string::npos) {
                return 100.00 + (row_index * 25.50);
            }
            return static_cast<double>(row_index) * 1.5;
            
        case SQL_VARCHAR:
        case SQL_CHAR:
        case SQL_LONGVARCHAR:
            if (upper_name.find("NAME") != std::string::npos && 
                upper_name.find("USER") != std::string::npos) {
                return generate_name(row_index);
            }
            if (upper_name == "USERNAME") {
                return "user" + std::to_string(row_index + 1);
            }
            if (upper_name.find("EMAIL") != std::string::npos) {
                return generate_email(row_index + 1);
            }
            if (upper_name == "NAME" || upper_name.find("PRODUCT") != std::string::npos) {
                return generate_product_name(row_index);
            }
            if (upper_name.find("DESCRIPTION") != std::string::npos) {
                return "Description for item " + std::to_string(row_index + 1);
            }
            if (upper_name.find("STATUS") != std::string::npos) {
                static const char* statuses[] = {"PENDING", "ACTIVE", "COMPLETED", "CANCELLED"};
                return std::string(statuses[row_index % 4]);
            }
            if (upper_name.find("CATEGORY") != std::string::npos) {
                static const char* categories[] = {"Electronics", "Clothing", "Food", "Books", "Toys"};
                return std::string(categories[row_index % 5]);
            }
            return "Value_" + std::to_string(row_index);
            
        case SQL_TYPE_DATE:
            return "2024-01-" + std::string(row_index % 28 + 1 < 10 ? "0" : "") + 
                   std::to_string(row_index % 28 + 1);
            
        case SQL_TYPE_TIME:
            return std::string(row_index % 24 < 10 ? "0" : "") + 
                   std::to_string(row_index % 24) + ":00:00";
            
        case SQL_TYPE_TIMESTAMP:
            return "2024-01-" + std::string(row_index % 28 + 1 < 10 ? "0" : "") + 
                   std::to_string(row_index % 28 + 1) + " 12:00:00";
            
        case SQL_BIT:
            return static_cast<long long>(row_index % 2);
            
        default:
            return std::string("Unknown");
    }
}

std::vector<MockRow> generate_mock_data(const MockTable& table, int row_count) {
    std::vector<MockRow> data;
    data.reserve(row_count);
    
    for (int i = 0; i < row_count; ++i) {
        MockRow row;
        row.reserve(table.columns.size());
        
        for (const auto& col : table.columns) {
            row.push_back(generate_value(col, i));
        }
        
        data.push_back(std::move(row));
    }
    
    return data;
}

ParsedQuery parse_sql(const std::string& sql) {
    ParsedQuery result;
    result.is_valid = false;
    
    std::string trimmed = trim(sql);
    if (trimmed.empty()) {
        result.error_message = "Empty SQL statement";
        return result;
    }
    
    std::string upper = to_upper(trimmed);
    
    // Determine query type
    if (upper.find("SELECT") == 0) {
        result.query_type = ParsedQuery::QueryType::Select;
        
        // Extract table name (simple parsing)
        auto from_pos = upper.find("FROM");
        if (from_pos != std::string::npos) {
            auto table_start = from_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) {
                ++table_start;
            }
            
            auto table_end = table_start;
            while (table_end < upper.length() && 
                   !std::isspace(upper[table_end]) && 
                   upper[table_end] != ';' &&
                   upper[table_end] != '(' &&
                   upper[table_end] != ')') {
                ++table_end;
            }
            
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            result.is_valid = true;
            
            // Check for WHERE clause
            auto where_pos = upper.find("WHERE");
            if (where_pos != std::string::npos) {
                result.where_clause = trimmed.substr(where_pos + 5);
            }
            
            // Parse columns
            auto select_end = from_pos;
            std::string cols_str = trim(trimmed.substr(6, select_end - 6));
            if (cols_str == "*") {
                result.columns.push_back("*");
            } else {
                std::istringstream iss(cols_str);
                std::string col;
                while (std::getline(iss, col, ',')) {
                    result.columns.push_back(trim(col));
                }
            }
        } else {
            result.error_message = "SELECT without FROM clause";
        }
    } else if (upper.find("INSERT") == 0) {
        result.query_type = ParsedQuery::QueryType::Insert;
        
        auto into_pos = upper.find("INTO");
        if (into_pos != std::string::npos) {
            auto table_start = into_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) {
                ++table_start;
            }
            
            auto table_end = table_start;
            while (table_end < upper.length() && 
                   !std::isspace(upper[table_end]) && 
                   upper[table_end] != '(') {
                ++table_end;
            }
            
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            result.is_valid = true;
            result.affected_rows = 1;
        } else {
            result.error_message = "INSERT without INTO clause";
        }
    } else if (upper.find("UPDATE") == 0) {
        result.query_type = ParsedQuery::QueryType::Update;
        
        auto table_start = 6;
        while (table_start < (int)upper.length() && std::isspace(upper[table_start])) {
            ++table_start;
        }
        
        auto table_end = table_start;
        while (table_end < (int)upper.length() && 
               !std::isspace(upper[table_end]) && 
               upper[table_end] != ';') {
            ++table_end;
        }
        
        result.table_name = trimmed.substr(table_start, table_end - table_start);
        result.is_valid = true;
        result.affected_rows = 1;  // Mock: 1 row affected
        
        auto where_pos = upper.find("WHERE");
        if (where_pos != std::string::npos) {
            result.where_clause = trimmed.substr(where_pos + 5);
        }
    } else if (upper.find("DELETE") == 0) {
        result.query_type = ParsedQuery::QueryType::Delete;
        
        auto from_pos = upper.find("FROM");
        if (from_pos != std::string::npos) {
            auto table_start = from_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) {
                ++table_start;
            }
            
            auto table_end = table_start;
            while (table_end < upper.length() && 
                   !std::isspace(upper[table_end]) && 
                   upper[table_end] != ';') {
                ++table_end;
            }
            
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            result.is_valid = true;
            result.affected_rows = 1;  // Mock: 1 row affected
        } else {
            result.error_message = "DELETE without FROM clause";
        }
    } else {
        result.query_type = ParsedQuery::QueryType::Other;
        result.error_message = "Unsupported SQL statement type";
    }
    
    return result;
}

QueryResult execute_query(const ParsedQuery& query, int result_set_size) {
    QueryResult result;
    
    if (!query.is_valid) {
        result.success = false;
        result.error_message = query.error_message;
        result.error_sqlstate = "42000";
        return result;
    }
    
    // Get catalog
    MockCatalog& catalog = MockCatalog::instance();
    const MockTable* table = catalog.find_table(query.table_name);
    
    if (!table) {
        result.success = false;
        result.error_message = "Table not found: " + query.table_name;
        result.error_sqlstate = "42S02";
        return result;
    }
    
    switch (query.query_type) {
        case ParsedQuery::QueryType::Select: {
            // Generate mock data
            result.success = true;
            
            // Determine which columns to return
            bool all_columns = query.columns.empty() || 
                               (query.columns.size() == 1 && query.columns[0] == "*");
            
            if (all_columns) {
                for (const auto& col : table->columns) {
                    result.column_names.push_back(col.name);
                    result.column_types.push_back(col.data_type);
                    result.column_sizes.push_back(col.column_size);
                }
            } else {
                for (const auto& col_name : query.columns) {
                    bool found = false;
                    for (const auto& col : table->columns) {
                        if (to_upper(col.name) == to_upper(col_name)) {
                            result.column_names.push_back(col.name);
                            result.column_types.push_back(col.data_type);
                            result.column_sizes.push_back(col.column_size);
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        result.success = false;
                        result.error_message = "Column not found: " + col_name;
                        result.error_sqlstate = "42S22";
                        return result;
                    }
                }
            }
            
            // Generate data
            result.data = generate_mock_data(*table, result_set_size);
            break;
        }
        
        case ParsedQuery::QueryType::Insert:
        case ParsedQuery::QueryType::Update:
        case ParsedQuery::QueryType::Delete:
            result.success = true;
            result.affected_rows = query.affected_rows;
            break;
            
        default:
            result.success = false;
            result.error_message = "Unsupported operation";
            result.error_sqlstate = "42000";
            break;
    }
    
    return result;
}

} // namespace mock_odbc
