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
    enum class QueryType { Select, Insert, Update, Delete, CreateTable, DropTable, Other };
    QueryType query_type = QueryType::Other;
    std::string table_name;
    std::vector<std::string> columns;  // For SELECT: requested columns (* = all)
    std::string where_clause;
    int affected_rows = 0;
    bool is_valid = false;
    bool is_literal_select = false;    // SELECT without FROM (literal values)
    bool is_count_query = false;       // SELECT COUNT(*) FROM table
    std::string error_message;

    // For literal SELECT: parsed expressions
    struct LiteralExpr {
        CellValue value;
        SQLSMALLINT sql_type = SQL_VARCHAR;
        SQLULEN column_size = 255;
        std::string alias;  // Column name (or EXPR_N if no alias)
        bool is_parameter_marker = false;
    };
    std::vector<LiteralExpr> literal_exprs;

    // For CREATE TABLE: column definitions
    struct ColumnDef {
        std::string name;
        SQLSMALLINT data_type = SQL_VARCHAR;
        SQLULEN column_size = 255;
        SQLSMALLINT decimal_digits = 0;
    };
    std::vector<ColumnDef> create_columns;

    // For INSERT: parsed values
    std::vector<CellValue> insert_values;
    std::vector<bool> insert_param_markers;  // true for each insert_value that was a '?' marker
    std::vector<std::string> insert_columns;

    // Parameter count
    int param_count = 0;
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
