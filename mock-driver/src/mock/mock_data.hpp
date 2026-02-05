#pragma once

#include "../driver/common.hpp"
#include "mock_catalog.hpp"
#include <string>
#include <vector>
#include <variant>

namespace mock_odbc {

// Cell value type
using CellValue = std::variant<std::monostate, long long, double, std::string>;

// Mock data row
using MockRow = std::vector<CellValue>;

// Generate mock data for a table
std::vector<MockRow> generate_mock_data(const MockTable& table, int row_count);

// Generate a specific value for a column
CellValue generate_value(const MockColumn& column, int row_index);

// Parse simple SQL and determine result
struct ParsedQuery {
    enum class QueryType { Select, Insert, Update, Delete, Other };
    QueryType query_type = QueryType::Other;
    std::string table_name;
    std::vector<std::string> columns;  // For SELECT: requested columns (* = all)
    std::string where_clause;
    int affected_rows = 0;
    bool is_valid = false;
    std::string error_message;
};

ParsedQuery parse_sql(const std::string& sql);

// Execute a parsed query and get results
struct QueryResult {
    bool success = false;
    std::string error_message;
    std::string error_sqlstate;
    std::vector<std::string> column_names;
    std::vector<SQLSMALLINT> column_types;
    std::vector<SQLULEN> column_sizes;
    std::vector<MockRow> data;
    SQLLEN affected_rows = 0;
};

QueryResult execute_query(const ParsedQuery& query, int result_set_size);

} // namespace mock_odbc
