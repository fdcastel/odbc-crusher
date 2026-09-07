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
    enum class QueryType {
        Select, Insert, Update, Delete, CreateTable, DropTable, Call, Other
    };
    QueryType query_type = QueryType::Other;
    std::string table_name;
    std::vector<std::string> columns;  // For SELECT: requested columns (* = all)
    std::string where_clause;
    // D13: ORDER BY used to ride along in the tail of `where_clause`,
    // and `where_clause` was only set when the statement had a WHERE -
    // so `SELECT ... ORDER BY x` with no WHERE came back unsorted.
    std::string order_by;
    int affected_rows = 0;
    bool is_valid = false;
    bool is_literal_select = false;    // SELECT without FROM (literal values)
    bool is_count_query = false;       // SELECT COUNT(*) FROM table
    std::string error_message;

    // For CALL <name>(arg1, arg2, ...)
    std::string proc_name;
    std::vector<CellValue> proc_args;
    // D13: `{?=CALL fn(...)}` - the leading `?` is the function's return
    // value and occupies parameter 1, so the first argument is parameter
    // 2. Getting that offset wrong is the classic driver defect A17
    // exists to detect, which is why the mock has to model it exactly.
    bool has_return_value = false;

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
    //
    // Multi-tuple INSERTs (`VALUES (…),(…),…`) are stored flat: every
    // tuple's values are appended in order, and `insert_row_count`
    // records how many tuples there are. The executor slices the flat
    // vector into rows by `insert_values.size() / insert_row_count`.
    // Linear `?`-marker numbering across all tuples matches what
    // SQLBindParameter expects (param 1 is the first marker, param 2
    // the second, …), so substitute_params doesn't need to know about
    // tuple boundaries.
    std::vector<CellValue> insert_values;
    std::vector<bool> insert_param_markers;  // true for each insert_value that was a '?' marker
    std::vector<std::string> insert_columns;
    size_t insert_row_count = 1;             // ≥1; number of value tuples

    // Parameter count
    int param_count = 0;
};

ParsedQuery parse_sql(const std::string& sql);

// Count `?` parameter markers, ignoring any inside a string literal.
// D14: SQLNumParams used to do its own naive scan and counted the ones
// in `SELECT '?' FROM t WHERE a = ?` as two parameters.
int count_param_markers(const std::string& sql);

// Strip a matching pair of double quotes from an identifier - D38.
std::string unquote_identifier(const std::string& raw);

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

    // CALL-specific — when this was an `EXECUTE PROCEDURE`, holds the
    // procedure name (so SQLExecute can look up params for writeback) and
    // the output values to copy into bound OUT/INOUT parameter buffers.
    // Empty for non-CALL queries.
    std::string proc_name;
    std::vector<CellValue> proc_output_values;
};

QueryResult execute_query(const ParsedQuery& query, int result_set_size);

} // namespace mock_odbc
