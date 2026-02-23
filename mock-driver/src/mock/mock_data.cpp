#include "mock_data.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>
#include <cmath>
#include <ctime>
#include <chrono>

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

// Count '?' parameter markers in SQL (outside of quoted strings)
int count_param_markers(const std::string& sql) {
    int count = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;
    for (size_t i = 0; i < sql.length(); ++i) {
        char c = sql[i];
        if (c == '\'' && !in_double_quote) {
            if (in_single_quote && i + 1 < sql.length() && sql[i + 1] == '\'') {
                ++i;
                continue;
            }
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '?' && !in_single_quote && !in_double_quote) {
            ++count;
        }
    }
    return count;
}

// Parse a SQL type name to SQL type constant
SQLSMALLINT parse_sql_type(const std::string& type_str, SQLULEN& column_size, SQLSMALLINT& decimal_digits) {
    std::string upper = to_upper(trim(type_str));
    column_size = 255;
    decimal_digits = 0;

    auto paren_pos = upper.find('(');
    std::string base_type = upper;
    if (paren_pos != std::string::npos) {
        base_type = trim(upper.substr(0, paren_pos));
        auto close_paren = upper.find(')', paren_pos);
        if (close_paren != std::string::npos) {
            std::string params = upper.substr(paren_pos + 1, close_paren - paren_pos - 1);
            auto comma = params.find(',');
            if (comma != std::string::npos) {
                try { column_size = std::stoul(trim(params.substr(0, comma))); } catch (...) {}
                try { decimal_digits = static_cast<SQLSMALLINT>(std::stoi(trim(params.substr(comma + 1)))); } catch (...) {}
            } else {
                try { column_size = std::stoul(trim(params)); } catch (...) {}
            }
        }
    }

    if (base_type == "INTEGER" || base_type == "INT" || base_type == "SIGNED") { column_size = 10; return SQL_INTEGER; }
    if (base_type == "SMALLINT") { column_size = 5; return SQL_SMALLINT; }
    if (base_type == "BIGINT") { column_size = 19; return SQL_BIGINT; }
    if (base_type == "TINYINT") { column_size = 3; return SQL_TINYINT; }
    if (base_type == "DECIMAL" || base_type == "NUMERIC") { if (paren_pos == std::string::npos) { column_size = 18; decimal_digits = 2; } return SQL_DECIMAL; }
    if (base_type == "REAL") { column_size = 7; return SQL_REAL; }
    if (base_type == "FLOAT") { column_size = 15; return SQL_FLOAT; }
    if (base_type == "DOUBLE" || base_type == "DOUBLE PRECISION") { column_size = 15; return SQL_DOUBLE; }
    if (base_type == "VARCHAR" || base_type == "CHAR VARYING") { return SQL_VARCHAR; }
    if (base_type == "CHAR" || base_type == "CHARACTER") { return SQL_CHAR; }
    if (base_type == "LONGVARCHAR" || base_type == "TEXT" || base_type == "CLOB") { column_size = 65535; return SQL_LONGVARCHAR; }
    if (base_type == "NVARCHAR" || base_type == "NATIONAL VARCHAR") { return SQL_WVARCHAR; }
    if (base_type == "NCHAR" || base_type == "NATIONAL CHAR") { return SQL_WCHAR; }
    if (base_type == "BINARY") { return SQL_BINARY; }
    if (base_type == "VARBINARY") { return SQL_VARBINARY; }
    if (base_type == "LONGVARBINARY" || base_type == "BLOB") { column_size = 65535; return SQL_LONGVARBINARY; }
    if (base_type == "DATE") { column_size = 10; return SQL_TYPE_DATE; }
    if (base_type == "TIME") { column_size = 8; return SQL_TYPE_TIME; }
    if (base_type == "TIMESTAMP") { column_size = 26; return SQL_TYPE_TIMESTAMP; }
    if (base_type == "BIT" || base_type == "BOOLEAN") { column_size = 1; return SQL_BIT; }
    if (base_type == "UNIQUEIDENTIFIER" || base_type == "UUID" || base_type == "GUID") { column_size = 36; return SQL_GUID; }
    return SQL_VARCHAR;
}

// Split expression list by commas, respecting parentheses and quotes
std::vector<std::string> split_expressions(const std::string& str) {
    std::vector<std::string> result;
    int paren_depth = 0;
    bool in_single_quote = false;
    bool in_double_quote = false;
    std::string current;

    for (size_t i = 0; i < str.length(); ++i) {
        char c = str[i];
        if (c == '\'' && !in_double_quote) {
            if (in_single_quote && i + 1 < str.length() && str[i + 1] == '\'') {
                current += c;
                current += str[++i];
                continue;
            }
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (c == '(' && !in_single_quote && !in_double_quote) {
            ++paren_depth;
        } else if (c == ')' && !in_single_quote && !in_double_quote) {
            --paren_depth;
        } else if (c == ',' && paren_depth == 0 && !in_single_quote && !in_double_quote) {
            result.push_back(trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        result.push_back(trim(current));
    }
    return result;
}

// Parse a literal value from SQL expression
ParsedQuery::LiteralExpr parse_literal_expression(const std::string& expr_str) {
    ParsedQuery::LiteralExpr lit;
    std::string trimmed = trim(expr_str);
    std::string upper = to_upper(trimmed);

    // Strip trailing alias "AS name" at depth 0 for the purpose of value parsing
    // We handle alias extraction separately in the caller

    // CAST(expr AS type)
    if (upper.find("CAST(") == 0 || upper.find("CAST (") == 0) {
        auto open = trimmed.find('(');
        auto as_pos = upper.find(" AS ");
        if (open != std::string::npos && as_pos != std::string::npos) {
            // Find the AS that's at paren depth 1 (inside the CAST)
            int depth = 0;
            size_t cast_as = std::string::npos;
            for (size_t i = open; i < upper.length(); ++i) {
                if (upper[i] == '(') ++depth;
                else if (upper[i] == ')') { --depth; if (depth == 0) break; }
                else if (depth == 1 && upper.substr(i, 4) == " AS ") { cast_as = i; break; }
            }
            if (cast_as != std::string::npos) {
                std::string inner_expr = trim(trimmed.substr(open + 1, cast_as - open - 1));
                auto close = trimmed.find(')', cast_as);
                std::string type_str = (close != std::string::npos)
                    ? trim(trimmed.substr(cast_as + 4, close - cast_as - 4))
                    : trim(trimmed.substr(cast_as + 4));

                SQLULEN col_size = 255;
                SQLSMALLINT dec_digits = 0;
                lit.sql_type = parse_sql_type(type_str, col_size, dec_digits);
                lit.column_size = col_size;

                std::string upper_inner = to_upper(inner_expr);
                if (upper_inner == "NULL") {
                    lit.value = std::monostate{};
                } else if (inner_expr.front() == '\'' && inner_expr.back() == '\'') {
                    std::string val = inner_expr.substr(1, inner_expr.length() - 2);
                    std::string unescaped;
                    for (size_t i = 0; i < val.length(); ++i) {
                        if (val[i] == '\'' && i + 1 < val.length() && val[i + 1] == '\'') {
                            unescaped += '\'';
                            ++i;
                        } else {
                            unescaped += val[i];
                        }
                    }
                    lit.value = unescaped;
                } else if ((inner_expr.find("N'") == 0 || inner_expr.find("n'") == 0) && inner_expr.back() == '\'') {
                    std::string val = inner_expr.substr(2, inner_expr.length() - 3);
                    lit.value = val;
                    if (lit.sql_type == SQL_VARCHAR) lit.sql_type = SQL_WVARCHAR;
                } else if (inner_expr.find("0x") == 0 || inner_expr.find("0X") == 0) {
                    std::string hex = inner_expr.substr(2);
                    std::string binary;
                    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
                        try { binary += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)); } catch (...) {}
                    }
                    lit.value = binary;
                } else {
                    try {
                        if (inner_expr.find('.') != std::string::npos) {
                            lit.value = std::stod(inner_expr);
                        } else {
                            lit.value = static_cast<long long>(std::stoll(inner_expr));
                        }
                    } catch (...) {
                        lit.value = inner_expr;
                    }
                }
                return lit;
            }
        }
    }

    // NULL
    if (upper == "NULL") { lit.value = std::monostate{}; lit.sql_type = SQL_VARCHAR; lit.column_size = 255; return lit; }

    // Parameter marker
    if (trimmed == "?") { lit.value = std::monostate{}; lit.sql_type = SQL_VARCHAR; lit.column_size = 255; lit.is_parameter_marker = true; return lit; }

    // N'...' Unicode string literal
    if (trimmed.length() >= 3 && (trimmed[0] == 'N' || trimmed[0] == 'n') && trimmed[1] == '\'' && trimmed.back() == '\'') {
        std::string val = trimmed.substr(2, trimmed.length() - 3);
        lit.value = val; lit.sql_type = SQL_WVARCHAR;
        lit.column_size = std::max(static_cast<SQLULEN>(1), static_cast<SQLULEN>(val.length()));
        return lit;
    }

    // X'...' hex binary literal
    if (trimmed.length() >= 3 && (trimmed[0] == 'X' || trimmed[0] == 'x') && trimmed[1] == '\'' && trimmed.back() == '\'') {
        std::string hex = trimmed.substr(2, trimmed.length() - 3);
        std::string binary;
        for (size_t i = 0; i + 1 < hex.length(); i += 2) {
            try { binary += static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)); } catch (...) {}
        }
        lit.value = binary; lit.sql_type = SQL_VARBINARY;
        lit.column_size = static_cast<SQLULEN>(binary.length());
        return lit;
    }

    // DATE 'yyyy-mm-dd'
    if (upper.find("DATE ") == 0 && trimmed.length() > 6) {
        std::string date_part = trim(trimmed.substr(5));
        if (date_part.front() == '\'' && date_part.back() == '\'') date_part = date_part.substr(1, date_part.length() - 2);
        lit.value = date_part; lit.sql_type = SQL_TYPE_DATE; lit.column_size = 10;
        return lit;
    }

    // UUID() or GEN_UUID()
    if (upper == "UUID()" || upper == "GEN_UUID()") {
        lit.value = std::string("6F9619FF-8B86-D011-B42D-00C04FC964FF");
        lit.sql_type = SQL_GUID; lit.column_size = 36;
        return lit;
    }

    // Quoted string
    if (trimmed.length() >= 2 && trimmed.front() == '\'' && trimmed.back() == '\'') {
        std::string val = trimmed.substr(1, trimmed.length() - 2);
        std::string unescaped;
        for (size_t i = 0; i < val.length(); ++i) {
            if (val[i] == '\'' && i + 1 < val.length() && val[i + 1] == '\'') {
                unescaped += '\''; ++i;
            } else {
                unescaped += val[i];
            }
        }
        lit.value = unescaped; lit.sql_type = SQL_VARCHAR;
        lit.column_size = std::max(static_cast<SQLULEN>(1), static_cast<SQLULEN>(unescaped.length()));
        return lit;
    }

    // Numeric — float
    if (!trimmed.empty() && (std::isdigit(static_cast<unsigned char>(trimmed[0])) || trimmed[0] == '-' || trimmed[0] == '+')) {
        if (trimmed.find('.') != std::string::npos) {
            try { lit.value = std::stod(trimmed); lit.sql_type = SQL_DOUBLE; lit.column_size = 15; return lit; } catch (...) {}
        }
        try {
            long long val = std::stoll(trimmed);
            lit.value = val; lit.sql_type = SQL_INTEGER; lit.column_size = 10;
            if (val > 2147483647LL || val < -2147483648LL) { lit.sql_type = SQL_BIGINT; lit.column_size = 19; }
            return lit;
        } catch (...) {}
    }

    // Default: string
    lit.value = trimmed; lit.sql_type = SQL_VARCHAR;
    lit.column_size = static_cast<SQLULEN>(trimmed.length());
    return lit;
}

// Parse INSERT VALUES clause
struct InsertValuesResult {
    std::vector<CellValue> values;
    std::vector<bool> param_markers;  // parallel: true when the value was '?'
};

InsertValuesResult parse_insert_values(const std::string& values_str) {
    InsertValuesResult result;
    auto exprs = split_expressions(values_str);
    for (const auto& expr : exprs) {
        std::string trimmed = trim(expr);
        std::string upper = to_upper(trimmed);
        if (upper == "NULL") { result.values.push_back(std::monostate{}); result.param_markers.push_back(false); }
        else if (trimmed == "?") { result.values.push_back(std::monostate{}); result.param_markers.push_back(true); }
        else if (trimmed.front() == '\'' && trimmed.back() == '\'') {
            std::string val = trimmed.substr(1, trimmed.length() - 2);
            std::string unescaped;
            for (size_t i = 0; i < val.length(); ++i) {
                if (val[i] == '\'' && i + 1 < val.length() && val[i + 1] == '\'') { unescaped += '\''; ++i; }
                else { unescaped += val[i]; }
            }
            result.values.push_back(unescaped); result.param_markers.push_back(false);
        } else {
            try {
                if (trimmed.find('.') != std::string::npos) { result.values.push_back(std::stod(trimmed)); result.param_markers.push_back(false); }
                else { result.values.push_back(static_cast<long long>(std::stoll(trimmed))); result.param_markers.push_back(false); }
            } catch (...) { result.values.push_back(trimmed); result.param_markers.push_back(false); }
        }
    }
    return result;
}

// Parse column definitions for CREATE TABLE
std::vector<ParsedQuery::ColumnDef> parse_column_defs(const std::string& defs_str) {
    std::vector<ParsedQuery::ColumnDef> result;
    auto cols = split_expressions(defs_str);
    for (const auto& col_str : cols) {
        std::string trimmed = trim(col_str);
        if (trimmed.empty()) continue;
        auto first_space = trimmed.find(' ');
        if (first_space == std::string::npos) continue;
        ParsedQuery::ColumnDef def;
        def.name = to_upper(trim(trimmed.substr(0, first_space)));
        std::string rest = trim(trimmed.substr(first_space + 1));
        std::string upper_rest = to_upper(rest);
        size_t constraint_pos = std::string::npos;
        for (const auto& kw : {"NOT NULL", "PRIMARY KEY", "DEFAULT", "UNIQUE", "CHECK", "REFERENCES"}) {
            auto pos = upper_rest.find(kw);
            if (pos != std::string::npos && (constraint_pos == std::string::npos || pos < constraint_pos)) constraint_pos = pos;
        }
        std::string type_part = (constraint_pos != std::string::npos) ? trim(rest.substr(0, constraint_pos)) : rest;
        def.data_type = parse_sql_type(type_part, def.column_size, def.decimal_digits);
        result.push_back(def);
    }
    return result;
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
        case SQL_WVARCHAR:
        case SQL_WCHAR:
            if (upper_name.find("NAME") != std::string::npos && 
                upper_name.find("USER") != std::string::npos) {
                return generate_name(row_index);
            }
            if (upper_name == "USERNAME") {
                return "user" + std::to_string(row_index + 1);
            }
            if (upper_name == "NAME") {
                return generate_name(row_index);
            }
            if (upper_name.find("EMAIL") != std::string::npos) {
                return generate_email(row_index + 1);
            }
            if (upper_name.find("PRODUCT") != std::string::npos) {
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

namespace {

// Find matching brace from position (after the opening brace)
size_t find_close_brace(const std::string& sql, size_t pos) {
    int depth = 1;
    bool in_sq = false;
    for (size_t i = pos + 1; i < sql.length(); ++i) {
        if (sql[i] == '\'' && !in_sq) { in_sq = true; continue; }
        if (sql[i] == '\'' && in_sq) {
            if (i + 1 < sql.length() && sql[i + 1] == '\'') { ++i; continue; }
            in_sq = false; continue;
        }
        if (in_sq) continue;
        if (sql[i] == '{') ++depth;
        else if (sql[i] == '}') { --depth; if (depth == 0) return i; }
    }
    return std::string::npos;
}

// Strip surrounding single-quotes from a SQL string literal, e.g. 'hello' -> hello
std::string unquote_sql_string(const std::string& s) {
    std::string t = trim(s);
    if (t.length() >= 2 && t.front() == '\'' && t.back() == '\'') {
        std::string inner = t.substr(1, t.length() - 2);
        // Unescape doubled single quotes: '' -> '
        std::string result;
        for (size_t i = 0; i < inner.length(); ++i) {
            if (inner[i] == '\'' && i + 1 < inner.length() && inner[i + 1] == '\'') {
                result += '\''; ++i;
            } else {
                result += inner[i];
            }
        }
        return result;
    }
    return t;
}

// Evaluate a scalar function expression and return the result
CellValue evaluate_scalar_function(const std::string& func_name_upper, const std::string& args_str) {
    if (func_name_upper == "UCASE" || func_name_upper == "UPPER") {
        return to_upper(unquote_sql_string(args_str));
    }
    if (func_name_upper == "LCASE" || func_name_upper == "LOWER") {
        std::string r = unquote_sql_string(args_str);
        std::transform(r.begin(), r.end(), r.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return r;
    }
    if (func_name_upper == "LENGTH" || func_name_upper == "LEN" || func_name_upper == "CHAR_LENGTH") {
        std::string v = unquote_sql_string(args_str);
        return static_cast<long long>(v.length());
    }
    if (func_name_upper == "LTRIM") {
        std::string v = unquote_sql_string(args_str);
        auto pos = v.find_first_not_of(' ');
        return pos == std::string::npos ? std::string("") : v.substr(pos);
    }
    if (func_name_upper == "RTRIM") {
        std::string v = unquote_sql_string(args_str);
        auto pos = v.find_last_not_of(' ');
        return pos == std::string::npos ? std::string("") : v.substr(0, pos + 1);
    }
    if (func_name_upper == "CONCAT") {
        auto parts = split_expressions(args_str);
        std::string result;
        for (const auto& p : parts) result += unquote_sql_string(p);
        return result;
    }
    if (func_name_upper == "SUBSTRING" || func_name_upper == "SUBSTR") {
        auto parts = split_expressions(args_str);
        if (parts.size() >= 2) {
            std::string str = unquote_sql_string(parts[0]);
            int start = 0;
            try { start = std::stoi(trim(parts[1])) - 1; } catch (...) {}
            int len = static_cast<int>(str.length());
            if (parts.size() >= 3) { try { len = std::stoi(trim(parts[2])); } catch (...) {} }
            if (start < 0) start = 0;
            if (start >= static_cast<int>(str.length())) return std::string("");
            return str.substr(start, len);
        }
        return std::string("");
    }
    if (func_name_upper == "ABS") {
        try {
            std::string v = trim(args_str);
            if (v.find('.') != std::string::npos) return std::abs(std::stod(v));
            return static_cast<long long>(std::abs(std::stoll(v)));
        } catch (...) { return static_cast<long long>(0); }
    }
    if (func_name_upper == "MOD") {
        auto parts = split_expressions(args_str);
        if (parts.size() >= 2) {
            try {
                long long a = std::stoll(trim(parts[0]));
                long long b = std::stoll(trim(parts[1]));
                return b != 0 ? a % b : static_cast<long long>(0);
            } catch (...) {}
        }
        return static_cast<long long>(0);
    }
    if (func_name_upper == "FLOOR") {
        try { return static_cast<double>(std::floor(std::stod(trim(args_str)))); } catch (...) { return 0.0; }
    }
    if (func_name_upper == "CEILING" || func_name_upper == "CEIL") {
        try { return static_cast<double>(std::ceil(std::stod(trim(args_str)))); } catch (...) { return 0.0; }
    }
    if (func_name_upper == "SQRT") {
        try { return std::sqrt(std::stod(trim(args_str))); } catch (...) { return 0.0; }
    }
    if (func_name_upper == "ROUND") {
        auto parts = split_expressions(args_str);
        if (parts.size() >= 1) {
            try {
                double val = std::stod(trim(parts[0]));
                int digits = 0;
                if (parts.size() >= 2) digits = std::stoi(trim(parts[1]));
                double factor = std::pow(10.0, digits);
                return std::round(val * factor) / factor;
            } catch (...) {}
        }
        return 0.0;
    }
    if (func_name_upper == "CURDATE") {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
        return std::string(buf);
    }
    if (func_name_upper == "CURTIME") {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
        return std::string(buf);
    }
    if (func_name_upper == "NOW") {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
        return std::string(buf);
    }
    if (func_name_upper == "YEAR") {
        // Extract year from a date string
        std::string v = trim(args_str);
        // Remove DATE prefix if present
        std::string uv = to_upper(v);
        if (uv.find("DATE ") == 0) v = trim(v.substr(5));
        if (v.front() == '\'') v = v.substr(1, v.length() - 2);
        try { return static_cast<long long>(std::stoi(v.substr(0, 4))); } catch (...) { return static_cast<long long>(0); }
    }
    if (func_name_upper == "MONTH") {
        std::string v = trim(args_str);
        std::string uv = to_upper(v);
        if (uv.find("DATE ") == 0) v = trim(v.substr(5));
        if (v.front() == '\'') v = v.substr(1, v.length() - 2);
        auto dash = v.find('-');
        if (dash != std::string::npos) {
            auto dash2 = v.find('-', dash + 1);
            if (dash2 != std::string::npos) {
                try { return static_cast<long long>(std::stoi(v.substr(dash + 1, dash2 - dash - 1))); } catch (...) {}
            }
        }
        return static_cast<long long>(0);
    }
    if (func_name_upper == "DAYOFWEEK") {
        std::string v = trim(args_str);
        std::string uv = to_upper(v);
        if (uv.find("DATE ") == 0) v = trim(v.substr(5));
        if (v.front() == '\'') v = v.substr(1, v.length() - 2);
        try {
            int y = std::stoi(v.substr(0, 4));
            int m = std::stoi(v.substr(5, 2));
            int d = std::stoi(v.substr(8, 2));
            struct tm t = {};
            t.tm_year = y - 1900;
            t.tm_mon = m - 1;
            t.tm_mday = d;
            mktime(&t);
            return static_cast<long long>(t.tm_wday + 1); // ODBC: 1=Sunday
        } catch (...) {}
        return static_cast<long long>(0);
    }
    if (func_name_upper == "DATABASE") {
        return std::string("MockDatabase");
    }
    if (func_name_upper == "USER") {
        return std::string("MockUser");
    }
    // Unknown function — return args as string
    return args_str;
}

// Preprocess SQL to translate ODBC escape sequences into forms parseable by mock parser
std::string preprocess_escape_sequences(const std::string& sql) {
    std::string result;
    result.reserve(sql.length());
    
    for (size_t i = 0; i < sql.length(); ++i) {
        if (sql[i] == '{') {
            size_t close = find_close_brace(sql, i);
            if (close == std::string::npos) {
                result += sql[i];
                continue;
            }
            
            std::string inner = trim(sql.substr(i + 1, close - i - 1));
            std::string upper_inner = to_upper(inner);
            
            if (upper_inner.find("FN ") == 0) {
                // {fn FUNC(args)} — evaluate the scalar function
                std::string func_body = trim(inner.substr(3));
                // Recursively preprocess nested escapes
                func_body = preprocess_escape_sequences(func_body);
                
                // Parse function name and arguments
                auto paren_pos = func_body.find('(');
                if (paren_pos != std::string::npos) {
                    std::string func_name = to_upper(trim(func_body.substr(0, paren_pos)));
                    auto close_paren = func_body.rfind(')');
                    std::string args;
                    if (close_paren != std::string::npos && close_paren > paren_pos) {
                        args = func_body.substr(paren_pos + 1, close_paren - paren_pos - 1);
                    }
                    
                    CellValue val = evaluate_scalar_function(func_name, args);
                    
                    // Convert result to SQL literal
                    if (std::holds_alternative<std::string>(val)) {
                        result += "'" + std::get<std::string>(val) + "'";
                    } else if (std::holds_alternative<long long>(val)) {
                        result += std::to_string(std::get<long long>(val));
                    } else if (std::holds_alternative<double>(val)) {
                        std::ostringstream oss;
                        oss << std::get<double>(val);
                        result += oss.str();
                    } else {
                        result += "NULL";
                    }
                } else {
                    // No-arg function like DATABASE() or USER()
                    std::string func_name = to_upper(trim(func_body));
                    // Remove trailing ()
                    if (func_name.length() > 2 && func_name.substr(func_name.length() - 2) == "()") {
                        func_name = func_name.substr(0, func_name.length() - 2);
                    }
                    CellValue val = evaluate_scalar_function(func_name, "");
                    if (std::holds_alternative<std::string>(val)) {
                        result += "'" + std::get<std::string>(val) + "'";
                    } else if (std::holds_alternative<long long>(val)) {
                        result += std::to_string(std::get<long long>(val));
                    } else if (std::holds_alternative<double>(val)) {
                        std::ostringstream oss;
                        oss << std::get<double>(val);
                        result += oss.str();
                    } else {
                        result += "NULL";
                    }
                }
            } else if (upper_inner.find("D '") == 0) {
                // {d 'yyyy-mm-dd'} -> 'yyyy-mm-dd'
                std::string date_str = trim(inner.substr(2));
                result += date_str;
            } else if (upper_inner.find("T '") == 0) {
                // {t 'hh:mm:ss'} -> 'hh:mm:ss'
                std::string time_str = trim(inner.substr(2));
                result += time_str;
            } else if (upper_inner.find("TS '") == 0) {
                // {ts 'yyyy-mm-dd hh:mm:ss'} -> 'yyyy-mm-dd hh:mm:ss'
                std::string ts_str = trim(inner.substr(3));
                result += ts_str;
            } else if (upper_inner.find("OJ ") == 0) {
                // {oj ...} -> ...
                result += preprocess_escape_sequences(trim(inner.substr(3)));
            } else if (upper_inner.find("ESCAPE ") == 0) {
                // {escape '\'} -> ESCAPE '\'
                result += inner;
            } else if (upper_inner.find("CALL ") == 0) {
                // {CALL proc(...)} -> CALL proc(...)
                result += inner;
            } else if (upper_inner.find("?=CALL ") == 0 || upper_inner.find("? = CALL ") == 0) {
                result += inner;
            } else if (upper_inner.find("INTERVAL ") == 0) {
                result += trim(inner);
            } else {
                // Unknown escape — pass through
                result += sql.substr(i, close - i + 1);
            }
            
            i = close;
        } else {
            result += sql[i];
        }
    }
    
    return result;
}

} // anonymous namespace for escape sequence helpers

ParsedQuery parse_sql(const std::string& sql) {
    ParsedQuery result;
    result.is_valid = false;
    
    // Preprocess ODBC escape sequences before parsing
    std::string preprocessed = preprocess_escape_sequences(sql);
    std::string trimmed = trim(preprocessed);
    if (trimmed.empty()) {
        result.error_message = "Empty SQL statement";
        return result;
    }
    
    // Count parameter markers
    result.param_count = count_param_markers(trimmed);
    
    std::string upper = to_upper(trimmed);
    
    // ---- CREATE TABLE ----
    if (upper.find("CREATE") == 0 && upper.find("TABLE") != std::string::npos) {
        result.query_type = ParsedQuery::QueryType::CreateTable;
        auto table_pos = upper.find("TABLE");
        auto name_start = table_pos + 5;
        while (name_start < upper.length() && std::isspace(upper[name_start])) ++name_start;
        auto name_end = name_start;
        while (name_end < upper.length() && !std::isspace(upper[name_end]) && upper[name_end] != '(') ++name_end;
        result.table_name = to_upper(trim(trimmed.substr(name_start, name_end - name_start)));
        auto open_paren = trimmed.find('(', name_end);
        auto close_paren = trimmed.rfind(')');
        if (open_paren != std::string::npos && close_paren != std::string::npos && close_paren > open_paren) {
            result.create_columns = parse_column_defs(trimmed.substr(open_paren + 1, close_paren - open_paren - 1));
        }
        result.is_valid = true;
        return result;
    }
    
    // ---- DROP TABLE ----
    if (upper.find("DROP") == 0 && upper.find("TABLE") != std::string::npos) {
        result.query_type = ParsedQuery::QueryType::DropTable;
        auto table_pos = upper.find("TABLE");
        auto name_start = table_pos + 5;
        while (name_start < upper.length() && std::isspace(upper[name_start])) ++name_start;
        auto name_end = name_start;
        while (name_end < upper.length() && !std::isspace(upper[name_end]) && upper[name_end] != ';') ++name_end;
        result.table_name = to_upper(trim(trimmed.substr(name_start, name_end - name_start)));
        result.is_valid = true;
        return result;
    }
    
    // ---- SELECT ----
    if (upper.find("SELECT") == 0) {
        result.query_type = ParsedQuery::QueryType::Select;
        
        // Find FROM clause (not inside parentheses or quotes)
        size_t from_pos = std::string::npos;
        {
            int depth = 0;
            bool in_sq = false, in_dq = false;
            for (size_t i = 6; i + 4 <= upper.length(); ++i) {
                char c = upper[i];
                if (c == '\'' && !in_dq) { if (in_sq && i+1 < upper.length() && upper[i+1] == '\'') { ++i; continue; } in_sq = !in_sq; }
                else if (c == '"' && !in_sq) { in_dq = !in_dq; }
                else if (c == '(' && !in_sq && !in_dq) ++depth;
                else if (c == ')' && !in_sq && !in_dq) --depth;
                else if (depth == 0 && !in_sq && !in_dq && i > 6 && std::isspace(upper[i-1]) &&
                         upper.substr(i, 4) == "FROM" &&
                         (i + 4 >= upper.length() || std::isspace(upper[i+4]))) {
                    from_pos = i;
                    break;
                }
            }
        }
        
        if (from_pos != std::string::npos) {
            // Table-based SELECT
            auto table_start = from_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) ++table_start;
            auto table_end = table_start;
            while (table_end < upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != ';' && upper[table_end] != '(' && upper[table_end] != ')') ++table_end;
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            
            // Skip system pseudo-tables used by Firebird/Oracle
            std::string upper_table = to_upper(result.table_name);
            if (upper_table == "RDB$DATABASE" || upper_table == "DUAL") {
                // Treat as literal select — the FROM clause is just a database-specific idiom
                result.is_literal_select = true;
                result.table_name.clear();
                std::string expr_str = trim(trimmed.substr(6, from_pos - 7));
                while (!expr_str.empty() && expr_str.back() == ';') { expr_str.pop_back(); expr_str = trim(expr_str); }
                auto expressions = split_expressions(expr_str);
                int expr_idx = 1;
                for (const auto& expr : expressions) {
                    auto lit = parse_literal_expression(expr);
                    lit.alias = "EXPR_" + std::to_string(expr_idx++);
                    result.literal_exprs.push_back(std::move(lit));
                }
                result.is_valid = true;
                return result;
            }
            
            // WHERE clause
            auto where_pos = upper.find("WHERE", table_end);
            if (where_pos != std::string::npos) {
                result.where_clause = trimmed.substr(where_pos + 5);
            }
            
            // Parse column list
            std::string cols_str = trim(trimmed.substr(6, from_pos - 7));
            std::string upper_cols = to_upper(cols_str);
            
            // COUNT(*)
            if (upper_cols.find("COUNT(*)") != std::string::npos || upper_cols.find("COUNT (*)") != std::string::npos) {
                result.is_count_query = true;
                result.is_valid = true;
                return result;
            }
            
            if (cols_str == "*") {
                result.columns.push_back("*");
            } else {
                auto col_exprs = split_expressions(cols_str);
                for (const auto& c : col_exprs) {
                    result.columns.push_back(trim(c));
                }
            }
            result.is_valid = true;
        } else {
            // No FROM clause — literal SELECT
            result.is_literal_select = true;
            std::string expr_str = trim(trimmed.substr(6));
            while (!expr_str.empty() && expr_str.back() == ';') { expr_str.pop_back(); expr_str = trim(expr_str); }
            auto expressions = split_expressions(expr_str);
            int expr_idx = 1;
            for (const auto& expr : expressions) {
                auto lit = parse_literal_expression(expr);
                lit.alias = "EXPR_" + std::to_string(expr_idx++);
                // Check for AS alias (outside CAST parentheses)
                std::string upper_expr = to_upper(expr);
                size_t as_pos = std::string::npos;
                int depth = 0;
                for (size_t i = 0; i + 4 <= upper_expr.length(); ++i) {
                    if (upper_expr[i] == '(') ++depth;
                    else if (upper_expr[i] == ')') --depth;
                    else if (depth == 0 && upper_expr.substr(i, 4) == " AS ") { as_pos = i; }
                }
                if (as_pos != std::string::npos) {
                    lit.alias = trim(expr.substr(as_pos + 4));
                }
                result.literal_exprs.push_back(std::move(lit));
            }
            result.is_valid = true;
        }
    } else if (upper.find("INSERT") == 0) {
        result.query_type = ParsedQuery::QueryType::Insert;
        auto into_pos = upper.find("INTO");
        if (into_pos != std::string::npos) {
            auto table_start = into_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) ++table_start;
            auto table_end = table_start;
            while (table_end < upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != '(') ++table_end;
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            
            // Parse column names
            auto col_open = trimmed.find('(', table_end);
            auto values_pos = upper.find("VALUES");
            if (col_open != std::string::npos && (values_pos == std::string::npos || col_open < values_pos)) {
                auto col_close = trimmed.find(')', col_open);
                if (col_close != std::string::npos) {
                    auto col_list = split_expressions(trimmed.substr(col_open + 1, col_close - col_open - 1));
                    for (const auto& c : col_list) result.insert_columns.push_back(to_upper(trim(c)));
                }
            }
            
            // Parse VALUES
            if (values_pos != std::string::npos) {
                auto val_open = trimmed.find('(', values_pos);
                auto val_close = trimmed.rfind(')');
                if (val_open != std::string::npos && val_close != std::string::npos && val_close > val_open) {
                    auto ivr = parse_insert_values(trimmed.substr(val_open + 1, val_close - val_open - 1));
                    result.insert_values = std::move(ivr.values);
                    result.insert_param_markers = std::move(ivr.param_markers);
                }
            }
            
            result.is_valid = true;
            result.affected_rows = 1;
        } else {
            result.error_message = "INSERT without INTO clause";
        }
    } else if (upper.find("UPDATE") == 0) {
        result.query_type = ParsedQuery::QueryType::Update;
        auto table_start = 6;
        while (table_start < (int)upper.length() && std::isspace(upper[table_start])) ++table_start;
        auto table_end = table_start;
        while (table_end < (int)upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != ';') ++table_end;
        result.table_name = trimmed.substr(table_start, table_end - table_start);
        result.is_valid = true;
        result.affected_rows = 1;
        auto where_pos = upper.find("WHERE");
        if (where_pos != std::string::npos) result.where_clause = trimmed.substr(where_pos + 5);
    } else if (upper.find("DELETE") == 0) {
        result.query_type = ParsedQuery::QueryType::Delete;
        auto from_pos = upper.find("FROM");
        if (from_pos != std::string::npos) {
            auto table_start = from_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) ++table_start;
            auto table_end = table_start;
            while (table_end < upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != ';') ++table_end;
            result.table_name = trimmed.substr(table_start, table_end - table_start);
            result.is_valid = true;
            result.affected_rows = 1;
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
    
    MockCatalog& catalog = MockCatalog::instance();
    
    // ---- CREATE TABLE ----
    if (query.query_type == ParsedQuery::QueryType::CreateTable) {
        if (catalog.find_table(query.table_name)) {
            result.success = false;
            result.error_message = "Table already exists: " + query.table_name;
            result.error_sqlstate = "42S01";
            return result;
        }
        MockTable new_table;
        new_table.catalog = "";
        new_table.schema = "";
        new_table.name = to_upper(query.table_name);
        new_table.type = "TABLE";
        new_table.remarks = "User-created table";
        for (const auto& def : query.create_columns) {
            MockColumn col;
            col.name = def.name;
            col.data_type = def.data_type;
            col.column_size = def.column_size;
            col.decimal_digits = def.decimal_digits;
            col.nullable = SQL_NULLABLE;
            col.is_primary_key = false;
            col.is_auto_increment = false;
            new_table.columns.push_back(col);
        }
        catalog.add_table(new_table);
        result.success = true;
        result.affected_rows = 0;
        return result;
    }
    
    // ---- DROP TABLE ----
    if (query.query_type == ParsedQuery::QueryType::DropTable) {
        if (!catalog.find_table(query.table_name)) {
            result.success = false;
            result.error_message = "Table not found: " + query.table_name;
            result.error_sqlstate = "42S02";
            return result;
        }
        catalog.remove_table(query.table_name);
        result.success = true;
        result.affected_rows = 0;
        return result;
    }
    
    // ---- Literal SELECT ----
    if (query.is_literal_select) {
        result.success = true;
        MockRow row;
        for (const auto& lit : query.literal_exprs) {
            result.column_names.push_back(lit.alias);
            result.column_types.push_back(lit.sql_type);
            result.column_sizes.push_back(lit.column_size);
            row.push_back(lit.value);
        }
        result.data.push_back(std::move(row));
        return result;
    }
    
    // ---- Table-based queries ----
    const MockTable* table = catalog.find_table(query.table_name);
    if (!table) {
        result.success = false;
        result.error_message = "Table not found: " + query.table_name;
        result.error_sqlstate = "42S02";
        return result;
    }
    
    switch (query.query_type) {
        case ParsedQuery::QueryType::Select: {
            // COUNT(*)
            if (query.is_count_query) {
                result.success = true;
                result.column_names.push_back("COUNT");
                result.column_types.push_back(SQL_INTEGER);
                result.column_sizes.push_back(10);
                long long count = 0;
                auto& inserted = catalog.inserted_data();
                std::string upper_name = to_upper(query.table_name);
                auto it = inserted.find(upper_name);
                if (it != inserted.end()) {
                    count = static_cast<long long>(it->second.size());
                } else if (table->remarks != "User-created table") {
                    count = static_cast<long long>(result_set_size);
                }
                MockRow row;
                row.push_back(count);
                result.data.push_back(std::move(row));
                return result;
            }
            
            result.success = true;
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
            
            // Return inserted data if available, otherwise generate mock data
            auto& inserted = catalog.inserted_data();
            std::string upper_name = to_upper(query.table_name);
            auto it = inserted.find(upper_name);
            if (it != inserted.end() && !it->second.empty()) {
                result.data = it->second;
            } else if (table->remarks == "User-created table") {
                // User-created table with no data — empty result
            } else {
                result.data = generate_mock_data(*table, result_set_size);
            }
            
            // ── Basic WHERE filtering ──
            // Supports: "column IN (v1, v2, ...)" and "column = value"
            if (!query.where_clause.empty() && !result.data.empty()) {
                std::string wc = trim(query.where_clause);
                std::string wcu = to_upper(wc);
                
                // Find the target column index
                auto in_pos = wcu.find(" IN ");
                auto eq_pos = wcu.find(" = ");
                auto eq2_pos = wcu.find("=");
                
                std::string filter_col;
                std::vector<CellValue> filter_values;
                
                if (in_pos != std::string::npos) {
                    // "COLUMN IN (v1, v2, ...)"
                    filter_col = to_upper(trim(wc.substr(0, in_pos)));
                    auto paren_open = wc.find('(', in_pos);
                    auto paren_close = wc.find(')', paren_open);
                    if (paren_open != std::string::npos && paren_close != std::string::npos) {
                        auto val_list = split_expressions(
                            wc.substr(paren_open + 1, paren_close - paren_open - 1));
                        for (auto& v : val_list) {
                            std::string tv = trim(v);
                            if (tv.empty()) continue;
                            if (tv.front() == '\'' && tv.back() == '\'') {
                                filter_values.push_back(tv.substr(1, tv.size() - 2));
                            } else {
                                try { filter_values.push_back(static_cast<long long>(std::stoll(tv))); }
                                catch (...) { filter_values.push_back(tv); }
                            }
                        }
                    }
                } else if (eq_pos != std::string::npos || eq2_pos != std::string::npos) {
                    // "COLUMN = value"
                    auto pos = (eq_pos != std::string::npos) ? eq_pos : eq2_pos;
                    filter_col = to_upper(trim(wc.substr(0, pos)));
                    std::string val = trim(wc.substr(pos + ((eq_pos != std::string::npos) ? 3 : 1)));
                    if (val.front() == '\'' && val.back() == '\'') {
                        filter_values.push_back(val.substr(1, val.size() - 2));
                    } else {
                        try { filter_values.push_back(static_cast<long long>(std::stoll(val))); }
                        catch (...) { filter_values.push_back(val); }
                    }
                }
                
                if (!filter_col.empty() && !filter_values.empty()) {
                    // Find column index in table
                    int col_idx = -1;
                    for (size_t ci = 0; ci < table->columns.size(); ++ci) {
                        if (to_upper(table->columns[ci].name) == filter_col) {
                            col_idx = static_cast<int>(ci);
                            break;
                        }
                    }
                    if (col_idx >= 0) {
                        std::vector<MockRow> filtered;
                        for (const auto& row : result.data) {
                            if (col_idx < static_cast<int>(row.size())) {
                                for (const auto& fv : filter_values) {
                                    if (row[col_idx] == fv) {
                                        filtered.push_back(row);
                                        break;
                                    }
                                }
                            }
                        }
                        result.data = std::move(filtered);
                    }
                }
            }
            
            // ── Basic ORDER BY ──
            // Supports: "ORDER BY column [ASC|DESC]"
            {
                std::string wcu = to_upper(query.where_clause.empty()
                    ? "" : query.where_clause);
                // Also check original SQL for ORDER BY after WHERE
                std::string full_upper = to_upper(query.table_name); // check sql later
                // Parse ORDER BY from where_clause tail or from sql
                auto order_pos = wcu.find("ORDER BY");
                if (order_pos != std::string::npos) {
                    std::string order_spec = trim(
                        query.where_clause.substr(order_pos + 8));
                    bool desc = (to_upper(order_spec).find("DESC") != std::string::npos);
                    // Extract column name
                    auto space = order_spec.find(' ');
                    std::string order_col = to_upper(
                        space != std::string::npos ? order_spec.substr(0, space) : order_spec);
                    
                    int col_idx = -1;
                    for (size_t ci = 0; ci < table->columns.size(); ++ci) {
                        if (to_upper(table->columns[ci].name) == order_col) {
                            col_idx = static_cast<int>(ci);
                            break;
                        }
                    }
                    if (col_idx >= 0 && result.data.size() > 1) {
                        std::sort(result.data.begin(), result.data.end(),
                            [col_idx, desc](const MockRow& a, const MockRow& b) {
                                if (col_idx >= static_cast<int>(a.size())) return !desc;
                                if (col_idx >= static_cast<int>(b.size())) return desc;
                                // Compare variants
                                return desc ? (b[col_idx] < a[col_idx])
                                            : (a[col_idx] < b[col_idx]);
                            });
                    }
                }
            }
            
            // If specific columns were requested, project only those columns
            if (!all_columns && !result.data.empty()) {
                std::vector<int> col_indices;
                for (const auto& col_name : query.columns) {
                    for (size_t j = 0; j < table->columns.size(); ++j) {
                        if (to_upper(table->columns[j].name) == to_upper(col_name)) {
                            col_indices.push_back(static_cast<int>(j));
                            break;
                        }
                    }
                }
                std::vector<MockRow> projected;
                for (const auto& row : result.data) {
                    MockRow proj_row;
                    for (int idx : col_indices) {
                        if (idx < static_cast<int>(row.size())) {
                            proj_row.push_back(row[idx]);
                        } else {
                            proj_row.push_back(std::monostate{});
                        }
                    }
                    projected.push_back(std::move(proj_row));
                }
                result.data = std::move(projected);
            }
            
            break;
        }
        
        case ParsedQuery::QueryType::Insert: {
            result.success = true;
            result.affected_rows = query.affected_rows;
            if (!query.insert_values.empty()) {
                MockRow row;
                if (!query.insert_columns.empty() && query.insert_columns.size() == query.insert_values.size()) {
                    row.resize(table->columns.size(), std::monostate{});
                    for (size_t i = 0; i < query.insert_columns.size(); ++i) {
                        for (size_t j = 0; j < table->columns.size(); ++j) {
                            if (to_upper(table->columns[j].name) == query.insert_columns[i]) {
                                row[j] = query.insert_values[i];
                                break;
                            }
                        }
                    }
                } else {
                    row = query.insert_values;
                    while (row.size() < table->columns.size()) row.push_back(std::monostate{});
                }
                catalog.insert_row(to_upper(query.table_name), std::move(row));
            }
            break;
        }
        
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
