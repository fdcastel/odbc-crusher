#include "mock_data.hpp"
#include "behaviors.hpp"
#include "../driver/config.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <sstream>
#include <regex>
#include <cmath>
#include <ctime>
#include <chrono>

namespace mock_odbc {

namespace {

// True when the column's SQL data type is one of the character varieties
// (CHAR / VARCHAR / LONGVARCHAR + W- and N- variants). MangleVarchar only
// touches these — keying off `std::get_if<std::string>` alone is wrong
// because the §1.1 canaries bind SQL_C_SLONG into VARCHAR columns and
// the mock stores those as `long long` in the cell variant. Without the
// schema-driven check, MangleVarchar would silently miss them.
bool is_character_sql_type(SQLSMALLINT t) {
    switch (t) {
        case SQL_CHAR:
        case SQL_VARCHAR:
        case SQL_LONGVARCHAR:
        case SQL_WCHAR:
        case SQL_WVARCHAR:
        case SQL_WLONGVARCHAR:
            return true;
        default:
            return false;
    }
}

// Stringify any numeric cell so MangleVarchar's append step has a string
// to mangle. Leaves std::string and monostate untouched.
void stringify_for_varchar(CellValue& cell) {
    if (auto* n = std::get_if<long long>(&cell)) {
        cell = std::to_string(*n);
    } else if (auto* d = std::get_if<double>(&cell)) {
        cell = std::to_string(*d);
    }
}

// Apply the active SilentCorruption mode to a row about to be stored.
// Returns false when the row should not be stored at all (DropInserts).
bool apply_silent_corruption(MockRow& row, const MockTable& table,
                             DriverConfig::SilentCorruptionMode mode) {
    using Mode = DriverConfig::SilentCorruptionMode;
    switch (mode) {
        case Mode::None:
            return true;

        // E5: these four used to fall off the end of the switch into the
        // trailing `return true`, which is the right answer and said nothing
        // about why. All four corrupt on the *fetch* path - they change what
        // a value looks like on the way out, not what is stored - so there is
        // nothing for this function to do. Written as cases so -Wswitch keeps
        // being the reminder it is when a fifth mode is added.
        case Mode::NullAsEmpty:
        case Mode::MangleUnicode:
        case Mode::SkewNumeric:
        case Mode::SkewNumericBound:
            return true;

        // D83: an UPDATE-path mode. This function is the INSERT path, and a
        // row arriving here is one being stored by an INSERT, so there is
        // nothing to do - dropping it here would make DropUpdates drop
        // inserts too, which is a mode we already have.
        case Mode::DropUpdates:
            return true;
        case Mode::DropInserts:
            return false;
        case Mode::MangleVarchar: {
            // Append a sentinel character to every stored character-column
            // value. Universal — works on numeric-as-string round-trips
            // ("5" → "5X") that the §1.1 canaries depend on, after we
            // stringify any numeric variant cell that landed in a VARCHAR
            // column (the mock doesn't auto-convert at bind time).
            size_t n = std::min(row.size(), table.columns.size());
            for (size_t i = 0; i < n; ++i) {
                if (!is_character_sql_type(table.columns[i].data_type)) continue;
                stringify_for_varchar(row[i]);
                if (auto* s = std::get_if<std::string>(&row[i])) {
                    s->push_back('X');
                }
            }
            return true;
        }
        case Mode::TruncateNumeric:
            for (auto& cell : row) {
                if (auto* d = std::get_if<double>(&cell)) {
                    *d = std::trunc(*d);
                }
            }
            return true;
    }
    return true;
}

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
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
                if (inner_expr == "?") {
                    // Found by A1 in Phase 3: only a *bare* `?` was
                    // recognised as a parameter marker, so
                    // `SELECT CAST(? AS INTEGER)` came back as the
                    // literal string "?" - the driver accepted the
                    // query, executed it, and returned the wrong value.
                    // The CAST form is the portable way to write a
                    // parameterised literal SELECT (a bare `SELECT ?`
                    // leaves some engines no type to infer), so it is
                    // the form probes reach for first.
                    //
                    // The declared CAST type stays as the marker type,
                    // which is exactly what the cast is there to say.
                    // This is the literal-SELECT corner of D10.
                    lit.is_parameter_marker = true;
                    lit.value = std::monostate{};
                } else if (upper_inner == "NULL") {
                    lit.value = std::monostate{};
                } else if (inner_expr.size() >= 2 && inner_expr.front() == '\''
                           && inner_expr.back() == '\'') {
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
                } else if (inner_expr.size() >= 3
                           && (inner_expr.find("N'") == 0 || inner_expr.find("n'") == 0)
                           && inner_expr.back() == '\'') {
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
        if (date_part.size() >= 2 && date_part.front() == '\''
            && date_part.back() == '\'') {
            date_part = date_part.substr(1, date_part.length() - 2);
        }
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

// Split a VALUES clause's interior (the substring between the first `(`
// after VALUES and the last `)`) into per-tuple slices.
//
// For single-tuple `(a, b)` the slice we get is `a, b` and we never see
// a `)` at depth 1 — the loop falls through with depth==1 and we emit
// the one tuple verbatim.
//
// For multi-tuple `(a, b), (c, d)` the slice is `a, b), (c, d` — we
// track paren depth (start at 1 because the leading `(` was stripped),
// pop on `)` back to depth 0, skip the inter-tuple `,`+whitespace, and
// re-enter at the next `(`. Single-quoted strings are honoured so a
// literal like `WHERE name = 'a),(b'` doesn't get sliced inside the
// quote (mock parser limitation: braces and double quotes are not
// honoured — no probe needs them).
static std::vector<std::string> split_value_tuples(const std::string& slice) {
    std::vector<std::string> tuples;
    std::string cur;
    int depth = 1;
    bool in_quote = false;
    for (char c : slice) {
        if (in_quote) {
            cur += c;
            if (c == '\'') in_quote = false;
            continue;
        }
        if (depth == 0) {
            // Inter-tuple gap: skip whitespace and ',' until next '('.
            if (c == '(') { depth = 1; }
            continue;
        }
        if (c == '\'') { in_quote = true; cur += c; continue; }
        if (c == '(') { depth++; cur += c; continue; }
        if (c == ')') {
            if (depth == 1) {
                tuples.push_back(trim(cur));
                cur.clear();
                depth = 0;
                continue;
            }
            depth--;
            cur += c;
            continue;
        }
        cur += c;
    }
    if (depth == 1) {
        // Single-tuple fall-through (caller stripped the trailing `)`).
        auto t = trim(cur);
        if (!t.empty()) tuples.push_back(t);
    }
    return tuples;
}

InsertValuesResult parse_insert_values(const std::string& values_str) {
    InsertValuesResult result;
    auto exprs = split_expressions(values_str);
    for (const auto& expr : exprs) {
        std::string trimmed = trim(expr);
        std::string upper = to_upper(trimmed);
        if (upper == "NULL") { result.values.push_back(std::monostate{}); result.param_markers.push_back(false); }
        else if (trimmed == "?") { result.values.push_back(std::monostate{}); result.param_markers.push_back(true); }
        else if (trimmed.empty()) {
            // `VALUES (1,,3)`. An empty expression is not a value; treat it
            // as NULL rather than indexing an empty string. The statement is
            // malformed either way - D12 keeps it from being *undefined*.
            result.values.push_back(std::monostate{});
            result.param_markers.push_back(false);
        }
        else if (trimmed.size() >= 2 && trimmed.front() == '\''
                 && trimmed.back() == '\'') {
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

// Build a row-filter from a WHERE clause. Understands:
//   <empty>                            -> always-true
//   <col> {= | != | <> | < | > | <= | >=} <literal>
//   <col> IS [NOT] NULL
//   <col> [NOT] IN (lit, lit, ...)
//   <literal> = <literal>              (constant predicates such as 1=0)
//   the above joined by AND / OR, and grouped with parentheses
//
// D12: every fallback used to return always-**true**, so a clause the mock
// could not read matched every row - and `DELETE FROM t WHERE <unparsable>`
// erased the table and reported success. A clause that cannot be evaluated is
// now an error the caller reports (42S22 for an unknown column, 42000 for
// syntax) and matches nothing, so a mock limitation can never look like a
// driver result. Known limitation, unchanged: a quoted string containing a
// comparison operator or the word AND (e.g. `name = 'a<=b'`) mis-parses.
struct WhereFilter {
    std::function<bool(const MockRow&)> match = [](const MockRow&) { return true; };
    bool understood = true;
    std::string sqlstate;
    std::string message;
};

namespace {

// Find `needle` in `hay` at paren depth 0 and outside string literals,
// searching from `from`. `word` requires non-identifier characters on both
// sides so that `IN` does not match inside `MAIN`.
size_t find_top_level(const std::string& hay, const std::string& upper_hay,
                      const std::string& needle, size_t from, bool word) {
    int depth = 0;
    bool in_quote = false;
    for (size_t i = from; i + needle.size() <= hay.size(); ++i) {
        const char c = hay[i];
        if (c == '\'') {
            if (in_quote && i + 1 < hay.size() && hay[i + 1] == '\'') { ++i; continue; }
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) continue;
        if (c == '(') { ++depth; continue; }
        if (c == ')') { if (depth > 0) --depth; continue; }
        if (depth != 0) continue;
        if (upper_hay.compare(i, needle.size(), needle) != 0) continue;
        if (word) {
            const auto ident = [](char ch) {
                return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
            };
            if (i > 0 && ident(hay[i - 1])) continue;
            const size_t after = i + needle.size();
            if (after < hay.size() && ident(hay[after])) continue;
        }
        return i;
    }
    return std::string::npos;
}

// Split on every top-level occurrence of `sep`.
std::vector<std::string> split_top_level(const std::string& clause,
                                         const std::string& sep) {
    std::vector<std::string> parts;
    const std::string upper = to_upper(clause);
    size_t from = 0;
    for (;;) {
        const size_t at = find_top_level(clause, upper, sep, from, true);
        if (at == std::string::npos) break;
        parts.push_back(clause.substr(from, at - from));
        from = at + sep.size();
    }
    parts.push_back(clause.substr(from));
    return parts;
}

// True when `s` is one parenthesised group covering the whole string.
bool is_wrapped(const std::string& s) {
    if (s.size() < 2 || s.front() != '(' || s.back() != ')') return false;
    int depth = 0;
    bool in_quote = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\'') {
            if (in_quote && i + 1 < s.size() && s[i + 1] == '\'') { ++i; continue; }
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) continue;
        if (c == '(') ++depth;
        else if (c == ')') {
            --depth;
            if (depth == 0 && i + 1 != s.size()) return false;
        }
    }
    return depth == 0;
}

} // namespace

// D83: the literal rules, in one place.
//
// This was a lambda inside make_where_filter. The SET parser needs the
// same two decisions - a doubled quote is an escaped apostrophe (D13),
// and a number is only a number when it is the *whole* token (D12) - and
// a second copy of them would drift from this one.
static CellValue parse_sql_literal(const std::string& v) {
    std::string s = trim(v);
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        // D13: the inner text was taken verbatim, so a doubled quote -
        // the SQL escape for a literal apostrophe - stayed doubled and
        // `WHERE V = 'it''s'` never matched the stored value `it's`.
        const std::string inner = s.substr(1, s.size() - 2);
        std::string out;
        out.reserve(inner.size());
        for (size_t i = 0; i < inner.size(); ++i) {
            out += inner[i];
            if (inner[i] == '\'' && i + 1 < inner.size()
                && inner[i + 1] == '\'') {
                ++i;   // skip the second of the pair
            }
        }
        return out;
    }
    // D12: stoll/stod without checking how much they consumed accepted
    // `1 AND b = 2` as the number 1 and silently dropped the rest of the
    // predicate. A literal has to be the *whole* token or it is a string.
    try {
        size_t used = 0;
        const long long n = std::stoll(s, &used);
        if (used == s.size()) return n;
    } catch (...) {}
    try {
        size_t used = 0;
        const double d = std::stod(s, &used);
        if (used == s.size()) return d;
    } catch (...) {}
    return s;
}

// D83: split `SET a = 1, b = 'x,y'` into assignments.
//
// Top-level commas only: a comma inside a quoted literal belongs to the value,
// and treating it as a separator turns one assignment into two malformed ones.
static std::vector<ParsedQuery::Assignment> parse_set_clause(
    const std::string& text) {
    std::vector<ParsedQuery::Assignment> out;

    std::vector<std::string> parts;
    std::string current;
    bool in_quote = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\'') {
            // A doubled quote inside a quoted string is an escaped
            // apostrophe, not the end of it - the same rule parse_sql_literal
            // applies to the value itself.
            if (in_quote && i + 1 < text.size() && text[i + 1] == '\'') {
                current += c;
                current += text[++i];
                continue;
            }
            in_quote = !in_quote;
            current += c;
            continue;
        }
        if (c == ',' && !in_quote) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!trim(current).empty()) parts.push_back(current);

    for (const auto& part : parts) {
        const auto eq = part.find('=');
        if (eq == std::string::npos) continue;
        ParsedQuery::Assignment a;
        a.column = unquote_identifier(trim(part.substr(0, eq)));
        const std::string value_text = trim(part.substr(eq + 1));
        if (a.column.empty() || value_text.empty()) continue;
        a.is_parameter_marker = (value_text == "?");
        if (!a.is_parameter_marker) a.value = parse_sql_literal(value_text);
        out.push_back(std::move(a));
    }
    return out;
}

static WhereFilter make_where_filter(const MockTable& table,
                                     const std::string& where_clause)
{
    WhereFilter result;
    auto trimmed = trim(where_clause);
    if (trimmed.empty()) return result;   // no WHERE means every row

    auto resolve_col = [&](const std::string& name) -> int {
        std::string up = to_upper(name);
        for (size_t ci = 0; ci < table.columns.size(); ++ci) {
            if (to_upper(table.columns[ci].name) == up) return static_cast<int>(ci);
        }
        return -1;
    };
    auto parse_literal = &parse_sql_literal;
    // A bare word that is not a number and not quoted is a column reference,
    // and an unresolved one is 42S22 rather than a string literal.
    auto looks_like_name = [](const std::string& s) {
        if (s.empty()) return false;
        if (s.front() == '\'') return false;
        if (std::isdigit(static_cast<unsigned char>(s.front())) || s.front() == '-'
            || s.front() == '+' || s.front() == '.') {
            return false;
        }
        for (char c : s) {
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') {
                return false;
            }
        }
        return true;
    };

    auto fail = [&result](const char* state, const std::string& msg) {
        if (result.understood) {          // keep the first complaint
            result.understood = false;
            result.sqlstate = state;
            result.message = msg;
        }
        return std::function<bool(const MockRow&)>(
            [](const MockRow&) { return false; });
    };

    // Forward declaration for the recursive descent below.
    std::function<std::function<bool(const MockRow&)>(const std::string&)> parse_expr;

    auto parse_atom = [&](const std::string& raw)
        -> std::function<bool(const MockRow&)>
    {
        const std::string atom = trim(raw);
        if (atom.empty()) return fail("42000", "Empty predicate");
        if (is_wrapped(atom)) return parse_expr(atom.substr(1, atom.size() - 2));

        const std::string upper = to_upper(atom);

        // ── <col> IS [NOT] NULL ──
        {
            const size_t is_at = find_top_level(atom, upper, "IS", 0, true);
            if (is_at != std::string::npos) {
                const std::string col_name = trim(atom.substr(0, is_at));
                std::string rest = trim(to_upper(atom.substr(is_at + 2)));
                bool negated = false;
                if (rest.rfind("NOT", 0) == 0) {
                    negated = true;
                    rest = trim(rest.substr(3));
                }
                if (rest != "NULL") {
                    return fail("42000", "Unsupported IS predicate: " + atom);
                }
                const int col_idx = resolve_col(col_name);
                if (col_idx < 0) {
                    return fail("42S22", "Column not found: " + col_name);
                }
                return [col_idx, negated](const MockRow& row) -> bool {
                    if (col_idx >= static_cast<int>(row.size())) return negated;
                    const bool is_null =
                        std::holds_alternative<std::monostate>(row[col_idx]);
                    return negated ? !is_null : is_null;
                };
            }
        }

        // ── <col> [NOT] LIKE <pattern> [ESCAPE 'c'] ──
        //
        // D42: a LIKE predicate used to fall through the filter entirely, so
        // every row came back and the mock could not fail a probe testing the
        // LIKE escaping it advertises through SQL_LIKE_ESCAPE_CLAUSE. The
        // matcher already existed for the catalog functions.
        {
            const size_t like_at = find_top_level(atom, upper, "LIKE", 0, true);
            if (like_at != std::string::npos) {
                std::string left = trim(atom.substr(0, like_at));
                bool negated = false;
                const std::string left_upper = to_upper(left);
                if (left_upper.size() >= 3
                    && left_upper.compare(left_upper.size() - 3, 3, "NOT") == 0) {
                    negated = true;
                    left = trim(left.substr(0, left.size() - 3));
                }

                std::string rest = trim(atom.substr(like_at + 4));

                // `{escape 'c'}` reaches the parser as `ESCAPE 'c'`, and
                // standard SQL gives LIKE no escape character without it.
                char escape_char = '\0';
                const std::string rest_upper = to_upper(rest);
                const size_t esc_at = find_top_level(rest, rest_upper, "ESCAPE", 0, true);
                if (esc_at != std::string::npos) {
                    const std::string esc = trim(rest.substr(esc_at + 6));
                    if (esc.size() >= 3 && esc.front() == '\'' && esc.back() == '\'') {
                        escape_char = esc[1];
                    } else if (!esc.empty()) {
                        return fail("42000", "Malformed ESCAPE clause: " + esc);
                    }
                    rest = trim(rest.substr(0, esc_at));
                }

                const int col_idx = resolve_col(left);
                const CellValue pattern_lit = parse_literal(rest);
                if (!std::holds_alternative<std::string>(pattern_lit)) {
                    return fail("42000", "LIKE pattern must be a string: " + rest);
                }
                const std::string pattern = std::get<std::string>(pattern_lit);

                if (col_idx < 0) {
                    // `'xzy' LIKE 'x!_y'` - both sides literal, which is the
                    // shape the escape probe uses.
                    if (looks_like_name(left)) {
                        return fail("42S22", "Column not found: " + left);
                    }
                    const CellValue lhs = parse_literal(left);
                    if (!std::holds_alternative<std::string>(lhs)) {
                        return fail("42000", "LIKE needs a string on the left: " + left);
                    }
                    const bool hit = MockCatalog::matches_pattern(
                        std::get<std::string>(lhs), pattern, escape_char);
                    const bool want = negated ? !hit : hit;
                    return [want](const MockRow&) { return want; };
                }
                return [col_idx, pattern, escape_char, negated](const MockRow& row) -> bool {
                    if (col_idx >= static_cast<int>(row.size())) return false;
                    if (!std::holds_alternative<std::string>(row[col_idx])) return false;
                    const bool hit = MockCatalog::matches_pattern(
                        std::get<std::string>(row[col_idx]), pattern, escape_char);
                    return negated ? !hit : hit;
                };
            }
        }

        // ── <col> [NOT] IN (...) ──
        {
            const size_t in_at = find_top_level(atom, upper, "IN", 0, true);
            if (in_at != std::string::npos) {
                std::string left = trim(atom.substr(0, in_at));
                bool negated = false;
                const std::string left_upper = to_upper(left);
                if (left_upper.size() >= 3
                    && left_upper.compare(left_upper.size() - 3, 3, "NOT") == 0) {
                    negated = true;
                    left = trim(left.substr(0, left.size() - 3));
                }
                const int col_idx = resolve_col(left);
                const auto paren_open = atom.find('(', in_at);
                const auto paren_close = atom.rfind(')');
                if (paren_open == std::string::npos
                    || paren_close == std::string::npos
                    || paren_close <= paren_open) {
                    return fail("42000", "Malformed IN list: " + atom);
                }
                if (col_idx < 0) {
                    return fail("42S22", "Column not found: " + left);
                }
                auto val_list = split_expressions(
                    atom.substr(paren_open + 1, paren_close - paren_open - 1));
                std::vector<CellValue> values;
                for (auto& v : val_list) {
                    std::string tv = trim(v);
                    if (!tv.empty()) values.push_back(parse_literal(tv));
                }
                return [col_idx, values, negated](const MockRow& row) -> bool {
                    if (col_idx >= static_cast<int>(row.size())) return false;
                    for (const auto& v : values) {
                        if (row[col_idx] == v) return !negated;
                    }
                    return negated;
                };
            }
        }

        // ── <col-or-literal> OP <literal> ──
        // Multi-character operators first so "<=" is not split into "<".
        enum Op { EQ = 0, NE, LT, GT, LE, GE };
        struct Tok { const char* s; Op op; };
        static constexpr Tok ops[] = {
            {"<=", LE}, {">=", GE}, {"<>", NE}, {"!=", NE},
            {"=",  EQ}, {"<",  LT}, {">",  GT},
        };
        for (const auto& tok : ops) {
            const size_t pos = find_top_level(atom, upper, tok.s, 0, false);
            if (pos == std::string::npos) continue;
            const std::string col_name = trim(atom.substr(0, pos));
            const std::string val_str  =
                trim(atom.substr(pos + std::strlen(tok.s)));
            if (col_name.empty() || val_str.empty()) {
                return fail("42000", "Malformed comparison: " + atom);
            }
            const CellValue lit = parse_literal(val_str);
            const int col_idx = resolve_col(col_name);
            const Op op = tok.op;
            if (col_idx < 0) {
                // D41: `WHERE 1=0` has no column on the left; evaluate it.
                if (looks_like_name(col_name)) {
                    return fail("42S22", "Column not found: " + col_name);
                }
                const CellValue left = parse_literal(col_name);
                const bool eq = (left == lit);
                const bool want = (op == EQ) ? eq : (op == NE) ? !eq : false;
                return [want](const MockRow&) { return want; };
            }
            return [col_idx, lit, op](const MockRow& row) -> bool {
                if (col_idx >= static_cast<int>(row.size())) return false;
                const CellValue& cell = row[col_idx];
                if (op == EQ) return cell == lit;
                if (op == NE) return !(cell == lit);
                auto to_num = [](const CellValue& c, double& out) {
                    if (std::holds_alternative<long long>(c)) {
                        out = static_cast<double>(std::get<long long>(c)); return true;
                    }
                    if (std::holds_alternative<double>(c)) {
                        out = std::get<double>(c); return true;
                    }
                    return false;
                };
                double l = 0, r = 0;
                if (to_num(cell, l) && to_num(lit, r)) {
                    switch (op) {
                        case LT: return l <  r;
                        case GT: return l >  r;
                        case LE: return l <= r;
                        case GE: return l >= r;
                        default: return false;
                    }
                }
                if (std::holds_alternative<std::string>(cell)
                    && std::holds_alternative<std::string>(lit)) {
                    const auto& a = std::get<std::string>(cell);
                    const auto& b = std::get<std::string>(lit);
                    switch (op) {
                        case LT: return a <  b;
                        case GT: return a >  b;
                        case LE: return a <= b;
                        case GE: return a >= b;
                        default: return false;
                    }
                }
                return false;
            };
        }

        return fail("42000", "Unsupported predicate: " + atom);
    };

    parse_expr = [&](const std::string& expr)
        -> std::function<bool(const MockRow&)>
    {
        const std::string text = trim(expr);
        if (text.empty()) return fail("42000", "Empty predicate");

        auto or_terms = split_top_level(text, "OR");
        if (or_terms.size() > 1) {
            std::vector<std::function<bool(const MockRow&)>> terms;
            terms.reserve(or_terms.size());
            for (const auto& t : or_terms) terms.push_back(parse_expr(t));
            return [terms](const MockRow& row) {
                for (const auto& t : terms) if (t(row)) return true;
                return false;
            };
        }

        auto and_terms = split_top_level(text, "AND");
        if (and_terms.size() > 1) {
            std::vector<std::function<bool(const MockRow&)>> terms;
            terms.reserve(and_terms.size());
            for (const auto& t : and_terms) terms.push_back(parse_atom(t));
            return [terms](const MockRow& row) {
                for (const auto& t : terms) if (!t(row)) return false;
                return true;
            };
        }

        return parse_atom(text);
    };

    // ORDER BY rides along in where_clause; the executor parses it separately.
    const std::string upper_all = to_upper(trimmed);
    const size_t order_at = find_top_level(trimmed, upper_all, "ORDER BY", 0, true);
    const std::string predicate_text =
        order_at == std::string::npos ? trimmed : trim(trimmed.substr(0, order_at));
    if (predicate_text.empty()) return result;

    result.match = parse_expr(predicate_text);
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

// D14: this lives outside the anonymous namespace now because
// SQLNumParams needs it. It was doing quote-aware counting all along;
// SQLNumParams had its own naive scan that counted a `?` inside a
// string literal as a parameter.

// Count '?' parameter markers in SQL (outside of quoted strings)
// D38: the driver advertises `"` through SQLGetInfo(SQL_IDENTIFIER_QUOTE_CHAR)
// and unicode_wrappers agrees, but the parser took the identifier as the raw
// token - quotes included - so `SELECT COUNT(*) FROM "ODBC_TEST_PARAM"` found
// no table. A matching pair is stripped and a doubled quote inside it is an
// escaped one. The mock folds identifiers to upper case either way, which is
// what it now reports as SQL_QUOTED_IDENTIFIER_CASE.
std::string unquote_identifier(const std::string& raw) {
    const std::string t = trim(raw);
    if (t.size() < 2 || t.front() != '"' || t.back() != '"') return t;
    const std::string inner = t.substr(1, t.size() - 2);
    std::string out;
    out.reserve(inner.size());
    for (size_t i = 0; i < inner.size(); ++i) {
        out += inner[i];
        if (inner[i] == '"' && i + 1 < inner.size() && inner[i + 1] == '"') ++i;
    }
    return out;
}

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
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
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
        if (v.size() >= 2 && v.front() == '\'') v = v.substr(1, v.length() - 2);
        try { return static_cast<long long>(std::stoi(v.substr(0, 4))); } catch (...) { return static_cast<long long>(0); }
    }
    if (func_name_upper == "MONTH") {
        std::string v = trim(args_str);
        std::string uv = to_upper(v);
        if (uv.find("DATE ") == 0) v = trim(v.substr(5));
        if (v.size() >= 2 && v.front() == '\'') v = v.substr(1, v.length() - 2);
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
        if (v.size() >= 2 && v.front() == '\'') v = v.substr(1, v.length() - 2);
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
    
    // D13: the outer scan used to treat every `{` as the start of an escape,
    // including one inside a string literal - so `SELECT '{fn NOW()}'` had its
    // *data* rewritten. find_close_brace already tracked quoting correctly;
    // the loop that called it did not.
    bool in_literal = false;
    for (size_t i = 0; i < sql.length(); ++i) {
        if (sql[i] == '\'') {
            // A doubled quote inside a literal is an escaped quote, not the end.
            if (in_literal && i + 1 < sql.length() && sql[i + 1] == '\'') {
                result += "''";
                ++i;
                continue;
            }
            in_literal = !in_literal;
            result += sql[i];
            continue;
        }
        if (in_literal) {
            result += sql[i];
            continue;
        }
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

// D13: `?=CALL fn(...)` / `? = CALL fn(...)`, the ODBC function-call form
// once preprocess_escape_sequences has stripped the braces. Anything between
// the `?` and `CALL` is whitespace or the `=`.
static bool is_function_call(const std::string& upper) {
    size_t i = 0;
    if (i >= upper.size() || upper[i] != '?') return false;
    ++i;
    while (i < upper.size() && std::isspace(static_cast<unsigned char>(upper[i]))) ++i;
    if (i >= upper.size() || upper[i] != '=') return false;
    ++i;
    while (i < upper.size() && std::isspace(static_cast<unsigned char>(upper[i]))) ++i;
    return upper.compare(i, 5, "CALL ") == 0;
}

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
        result.table_name = to_upper(unquote_identifier(trim(trimmed.substr(name_start, name_end - name_start))));
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
        result.table_name = to_upper(unquote_identifier(trim(trimmed.substr(name_start, name_end - name_start))));
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
            result.table_name = unquote_identifier(trimmed.substr(table_start, table_end - table_start));
            
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
            
            // WHERE clause, then ORDER BY - D13. Both are extracted here
            // so the executor never has to dig one out of the other.
            const auto order_pos = upper.find("ORDER BY", table_end);
            if (order_pos != std::string::npos) {
                result.order_by = trim(trimmed.substr(order_pos + 8));
            }
            auto where_pos = upper.find("WHERE", table_end);
            if (where_pos != std::string::npos) {
                const size_t where_start = where_pos + 5;
                result.where_clause =
                    order_pos != std::string::npos && order_pos > where_start
                        ? trimmed.substr(where_start, order_pos - where_start)
                        : trimmed.substr(where_start);
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
            // D44: `SELECT 1 WHERE <predicate>` used to parse the whole tail as
            // one select-list expression, so the predicate was neither
            // evaluated nor reported - the row came back whatever it said.
            // That is the portable no-table shape a literal predicate probe
            // needs, so the WHERE is split off here and evaluated below.
            {
                const std::string expr_upper = to_upper(expr_str);
                const size_t w = find_top_level(expr_str, expr_upper, "WHERE", 0, true);
                if (w != std::string::npos) {
                    result.where_clause = trim(expr_str.substr(w + 5));
                    expr_str = trim(expr_str.substr(0, w));
                }
            }
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
            result.table_name = unquote_identifier(trimmed.substr(table_start, table_end - table_start));
            
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
            
            // Parse VALUES — supports both single-tuple `(…)` and
            // multi-tuple `(…),(…),…`. Tuples are concatenated into the
            // flat insert_values vector and `insert_row_count` records
            // how many tuples there were; the executor slices.
            if (values_pos != std::string::npos) {
                auto val_open = trimmed.find('(', values_pos);
                auto val_close = trimmed.rfind(')');
                if (val_open != std::string::npos && val_close != std::string::npos && val_close > val_open) {
                    auto slice = trimmed.substr(val_open + 1, val_close - val_open - 1);
                    auto tuples = split_value_tuples(slice);
                    if (tuples.empty()) tuples.push_back(slice);
                    result.insert_row_count = tuples.size();
                    for (const auto& tup : tuples) {
                        auto ivr = parse_insert_values(tup);
                        for (auto& v : ivr.values) result.insert_values.push_back(std::move(v));
                        for (bool m : ivr.param_markers) result.insert_param_markers.push_back(m);
                    }
                }
            }

            result.is_valid = true;
            result.affected_rows = static_cast<int>(result.insert_row_count);
        } else {
            result.error_message = "INSERT without INTO clause";
        }
    } else if (upper.find("UPDATE") == 0) {
        result.query_type = ParsedQuery::QueryType::Update;
        auto table_start = 6;
        while (table_start < (int)upper.length() && std::isspace(upper[table_start])) ++table_start;
        auto table_end = table_start;
        while (table_end < (int)upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != ';') ++table_end;
        result.table_name = unquote_identifier(trimmed.substr(table_start, table_end - table_start));
        result.is_valid = true;
        // affected_rows is computed by the executor walking MockCatalog
        // — no hard-coded stub here.
        auto where_pos = upper.find("WHERE");
        if (where_pos != std::string::npos) result.where_clause = trimmed.substr(where_pos + 5);

        // D83: the SET clause, which nothing used to look at.
        const auto set_pos = upper.find(" SET ");
        if (set_pos != std::string::npos) {
            const size_t from = set_pos + 5;
            const size_t to = (where_pos != std::string::npos && where_pos > from)
                                  ? where_pos : trimmed.size();
            result.set_clauses = parse_set_clause(trimmed.substr(from, to - from));
        }
    } else if (upper.find("DELETE") == 0) {
        result.query_type = ParsedQuery::QueryType::Delete;
        auto from_pos = upper.find("FROM");
        if (from_pos != std::string::npos) {
            auto table_start = from_pos + 4;
            while (table_start < upper.length() && std::isspace(upper[table_start])) ++table_start;
            auto table_end = table_start;
            while (table_end < upper.length() && !std::isspace(upper[table_end]) && upper[table_end] != ';') ++table_end;
            // Stop the table-name slice at the start of WHERE, not just whitespace,
            // so `result.table_name` doesn't accidentally absorb the predicate.
            auto where_pos = upper.find("WHERE", table_start);
            if (where_pos != std::string::npos && where_pos < table_end) table_end = where_pos;
            // Trim trailing whitespace introduced by the WHERE adjustment.
            while (table_end > table_start && std::isspace(upper[table_end - 1])) --table_end;
            result.table_name = unquote_identifier(trimmed.substr(table_start, table_end - table_start));
            result.is_valid = true;
            // affected_rows is computed by the executor (count + erase).
            if (where_pos != std::string::npos) {
                result.where_clause = trimmed.substr(where_pos + 5);
            }
        } else {
            result.error_message = "DELETE without FROM clause";
        }
    } else if (upper.find("CALL ") == 0 || upper.find("EXECUTE PROCEDURE ") == 0
               || is_function_call(upper)) {
        // Stored procedure or function invocation:
        //   CALL <name>(arg1, arg2, ...)
        //   EXECUTE PROCEDURE <name>(arg1, arg2, ...)   (Firebird-style)
        //   ?=CALL <name>(arg1, ...)                    (function, D13)
        result.query_type = ParsedQuery::QueryType::Call;
        std::string call_text = trimmed;
        if (is_function_call(upper)) {
            // D13: strip the return-value marker and remember it. Everything
            // downstream then sees an ordinary CALL, except that parameter
            // numbering starts at 2.
            result.has_return_value = true;
            const size_t call_at = to_upper(call_text).find("CALL ");
            call_text = trim(call_text.substr(call_at));
        }
        const std::string call_upper = to_upper(call_text);
        const size_t skip = (call_upper.find("CALL ") == 0) ? 5 : 18;
        std::string body = trim(call_text.substr(skip));
        auto open = body.find('(');
        if (open == std::string::npos) {
            // Bare procedure name with no args
            result.proc_name = body;
            result.is_valid = true;
        } else {
            result.proc_name = trim(body.substr(0, open));
            auto close = body.rfind(')');
            if (close != std::string::npos && close > open) {
                std::string args = body.substr(open + 1, close - open - 1);
                auto ivr = parse_insert_values(args);
                result.proc_args = std::move(ivr.values);
                // Track which args are parameter markers so the executor's
                // substitute_params path can replace them later.
                result.insert_param_markers = std::move(ivr.param_markers);
                result.is_valid = true;
            } else {
                result.error_message = "CALL: unbalanced parentheses";
            }
        }
    } else {
        result.query_type = ParsedQuery::QueryType::Other;
        result.error_message = "Unsupported SQL statement type";
    }
    
    return result;
}

// ── I6: reads and writes that respect the transaction ─────────────────────
//
// Before this the mock had no uncommitted state at all: every write landed in
// the process-global store immediately, and ROLLBACK "undid" it by deleting
// every row of every table for every connection.
//
// `visible_rows` is committed rows, then (at READ UNCOMMITTED) other
// connections' pending ops, then this connection's own - own last, so a
// connection always sees its own writes whatever the isolation level.
static std::vector<MockRow> visible_rows(MockCatalog& catalog,
                                         const std::string& table,
                                         const TxnContext& txn) {
    auto rows = catalog.snapshot_inserted_rows(table);
    const std::string key = to_upper(table);
    if (txn.read_uncommitted) {
        apply_ops(rows, TxnRegistry::instance().peer_ops(txn.conn_id, key), key);
    }
    if (txn.buffered && txn.writes) {
        apply_ops(rows, txn.writes->ops_for(key), key);
    }
    return rows;
}

static void write_insert(MockCatalog& catalog, const std::string& table,
                         MockRow row, const TxnContext& txn) {
    const std::string key = to_upper(table);
    if (txn.buffered && txn.writes) {
        WriteOp op;
        op.kind = WriteOp::Kind::Insert;
        op.table = key;
        op.row = std::move(row);
        txn.writes->record(std::move(op));
        return;
    }
    catalog.insert_row(key, std::move(row));
}

// Returns the number of rows the statement claims to have removed, counted
// over what this connection can see - so a DELETE inside a transaction reports
// on rows the transaction itself inserted.
static size_t write_delete(MockCatalog& catalog, const std::string& table,
                           const std::function<bool(const MockRow&)>& match,
                           const TxnContext& txn) {
    const std::string key = to_upper(table);
    if (!txn.buffered || !txn.writes) {
        return catalog.erase_matching_rows(key, match);
    }
    // Count against the transaction's own view before recording the tombstone,
    // because the committed store has not changed and cannot answer.
    auto rows = visible_rows(catalog, table, txn);
    size_t n = 0;
    for (const auto& row : rows) {
        if (match(row)) ++n;
    }
    WriteOp op;
    op.kind = WriteOp::Kind::Delete;
    op.table = key;
    op.match = match;
    txn.writes->record(std::move(op));
    return n;
}

// D83: apply an UPDATE, buffered or committed.
//
// Returns the rows changed, counted over what this connection can see, so an
// UPDATE inside a transaction counts rows the transaction itself inserted.
static size_t write_update(
    MockCatalog& catalog, const std::string& table,
    const std::function<bool(const MockRow&)>& match,
    const std::vector<std::pair<size_t, CellValue>>& assignments,
    const TxnContext& txn) {
    const std::string key = to_upper(table);

    auto rows = visible_rows(catalog, table, txn);
    size_t n = 0;
    for (const auto& row : rows) {
        if (match(row)) ++n;
    }
    if (n == 0 || assignments.empty()) return n;

    WriteOp op;
    op.kind = WriteOp::Kind::Update;
    op.table = key;
    op.match = match;
    op.assignments = assignments;

    if (txn.buffered && txn.writes) {
        txn.writes->record(std::move(op));
    } else {
        catalog.apply_write_ops({op});
    }
    return n;
}

QueryResult execute_query(const ParsedQuery& query, int result_set_size,
                          const TxnContext& txn) {
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

    // ---- CALL <proc>(args) ----
    if (query.query_type == ParsedQuery::QueryType::Call) {
        auto proc = catalog.find_procedure(query.proc_name);
        if (!proc) {
            result.success = false;
            result.error_message = "Procedure not found: " + query.proc_name;
            result.error_sqlstate = "42000";
            return result;
        }
        if (proc->input_param_count != 0 &&
            static_cast<SQLSMALLINT>(query.proc_args.size()) !=
                proc->input_param_count) {
            result.success = false;
            result.error_message = "Procedure " + query.proc_name +
                                   " expects " +
                                   std::to_string(proc->input_param_count) +
                                   " arguments, got " +
                                   std::to_string(query.proc_args.size());
            result.error_sqlstate = "42000";
            return result;
        }
        // D13: a function's params[0] is its SQL_RETURN_VALUE slot; the
        // arguments the caller wrote line up from params[1]. The callback is
        // handed the arguments only - the return value is what it produces.
        MockProcedureResult pr = proc->callback(catalog, query.proc_args);
        result.success = pr.success;
        result.error_message = pr.error_message;
        result.error_sqlstate =
            pr.error_sqlstate.empty() ? "42000" : pr.error_sqlstate;
        result.affected_rows = pr.affected_rows;
        // PORT plan port 3 — propagate procedure name + output values so
        // SQLExecute can write them back into bound OUT/INOUT parameters.
        result.proc_name = query.proc_name;
        result.proc_output_values = std::move(pr.output_values);
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
        // D44: a literal SELECT can carry a constant predicate - it is the
        // portable way to ask a driver whether it evaluates one, with no table
        // involved. There are no columns to resolve, so an empty table is the
        // right context; an unreadable predicate is reported rather than
        // ignored, exactly as it is for a table query (D12).
        if (!query.where_clause.empty()) {
            static const MockTable kNoColumns{};
            auto filter = make_where_filter(kNoColumns, query.where_clause);
            if (!filter.understood) {
                result.success = false;
                result.error_sqlstate = filter.sqlstate;
                result.error_message = filter.message;
                return result;
            }
            if (!filter.match(row)) return result;   // predicate is false: no rows
        }
        result.data.push_back(std::move(row));
        return result;
    }
    
    // ---- Table-based queries ----
    // D5: a copy, not a pointer into the catalog's vector. The executor
    // below holds this across its whole body, and another connection's
    // CREATE TABLE reallocates that vector - which made the old pointer a
    // use-after-free rather than merely a race.
    const auto table_copy = catalog.find_table(query.table_name);
    const MockTable* table = table_copy ? &*table_copy : nullptr;
    if (!table) {
        result.success = false;
        result.error_message = "Table not found: " + query.table_name;
        result.error_sqlstate = "42S02";
        return result;
    }
    
    // D41: a select-list item that is a literal rather than a column.
    //
    // `SELECT 1 FROM T` used to be rejected with 42S22 "Column not found: 1",
    // which made two things impossible against the reference driver: the
    // `SELECT 1 FROM <table> WHERE 1=0` existence probe that both
    // create_test_table() helpers use before reusing a table (so **A15**'s
    // reuse path was unreachable), and every dialect variant **A2** builds -
    // `SELECT <expr> FROM RDB$DATABASE`, `SELECT <expr> FROM DUAL` - which
    // are all constants selected from a table.
    //
    // Recognises integers, decimals and single-quoted strings. Anything else
    // still falls through to the column lookup and its 42S22, which is the
    // right answer for a genuine typo.
    struct SelectLiteral {
        bool is_literal = false;
        CellValue value;
        SQLSMALLINT sql_type = SQL_VARCHAR;
        SQLULEN size = 0;
    };
    auto parse_select_literal = [](const std::string& raw) -> SelectLiteral {
        SelectLiteral out;
        const std::string t = trim(raw);
        if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'') {
            out.is_literal = true;
            out.value = t.substr(1, t.size() - 2);
            out.sql_type = SQL_VARCHAR;
            out.size = static_cast<SQLULEN>(t.size());
            return out;
        }
        if (t.empty()) return out;
        try {
            size_t used = 0;
            long long i = std::stoll(t, &used);
            if (used == t.size()) {
                out.is_literal = true;
                out.value = i;
                out.sql_type = SQL_INTEGER;
                out.size = 10;
                return out;
            }
        } catch (...) {
        }
        try {
            size_t used = 0;
            double d = std::stod(t, &used);
            if (used == t.size()) {
                out.is_literal = true;
                out.value = d;
                out.sql_type = SQL_DOUBLE;
                out.size = 15;
                return out;
            }
        } catch (...) {
        }
        return out;
    };

    switch (query.query_type) {
        case ParsedQuery::QueryType::Select: {
            // COUNT(*)
            if (query.is_count_query) {
                result.success = true;
                result.column_names.push_back("COUNT");
                result.column_types.push_back(SQL_INTEGER);
                result.column_sizes.push_back(10);

                // D13: materialise the same way the row-returning path does,
                // so COUNT(*) and SELECT * agree about what is in the table.
                if (!catalog.has_row_store(query.table_name) &&
                    table->remarks != "User-created table") {
                    catalog.materialize_rows(
                        query.table_name,
                        generate_mock_data(*table, result_set_size));
                }
                auto rows = visible_rows(catalog, query.table_name, txn);

                // D65: this used to return here, so the count was the number
                // of rows in the table whatever the WHERE clause said -
                // `COUNT(*) ... WHERE ID = 4242` answering "how many rows are
                // there". The wrong question answered confidently, which a
                // caller cannot detect. The same filter the row path uses now
                // runs here, refusal included: a count derived from a
                // predicate nobody evaluated is precisely this bug again.
                if (!query.where_clause.empty()) {
                    auto filter = make_where_filter(*table, query.where_clause);
                    if (!filter.understood) {
                        result.success = false;
                        result.error_sqlstate = filter.sqlstate;
                        result.error_message = filter.message;
                        return result;
                    }
                    std::vector<MockRow> matched;
                    matched.reserve(rows.size());
                    for (const auto& r : rows) {
                        if (filter.match(r)) matched.push_back(r);
                    }
                    rows.swap(matched);
                }

                MockRow row;
                row.push_back(static_cast<long long>(rows.size()));
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
                        // D41: a constant in the select list is not a missing
                        // column. Name the result column after the expression,
                        // which is what most engines do when there is no alias.
                        SelectLiteral lit = parse_select_literal(col_name);
                        if (lit.is_literal) {
                            result.column_names.push_back(trim(col_name));
                            result.column_types.push_back(lit.sql_type);
                            result.column_sizes.push_back(lit.size);
                            continue;
                        }
                        result.success = false;
                        result.error_message = "Column not found: " + col_name;
                        result.error_sqlstate = "42S22";
                        return result;
                    }
                }
            }
            
            // D13: this used to ask "are there any inserted rows?" and
            // generate a fresh set whenever the answer was no - so a stock
            // table emptied by DELETE sprang back to life on the next SELECT,
            // and UPDATE/DELETE (which only ever saw inserted_data_) reported
            // 0 rows for a table SELECT said had ten. There is one row store
            // now: a stock table materialises into it on first use, and every
            // statement reads and writes the same rows.
            if (!catalog.has_row_store(query.table_name)
                && table->remarks != "User-created table") {
                catalog.materialize_rows(query.table_name,
                                         generate_mock_data(*table, result_set_size));
            }
            result.data = visible_rows(catalog, query.table_name, txn);
            
            // D13: this used to re-implement WHERE filtering rather than
            // call the shared predicate builder, and the copy was weaker in three
            // ways that mattered. It supported only `=` and `IN`; its
            // `find("=")` matched inside `<=`, `>=` and `!=`, so
            // `WHERE ID >= 5` resolved a column named "ID >" and returned
            // **every** row unfiltered; and it parsed string literals without
            // un-doubling embedded quotes, so `WHERE V = \'it\'\'s\'` matched
            // nothing. One predicate builder, used by SELECT, UPDATE and
            // DELETE alike.
            if (!query.where_clause.empty()) {
                auto filter = make_where_filter(*table, query.where_clause);
                if (!filter.understood) {
                    // D12: reporting the clause the mock could not read beats
                    // returning rows chosen by a predicate nobody evaluated.
                    result.success = false;
                    result.error_sqlstate = filter.sqlstate;
                    result.error_message = filter.message;
                    return result;
                }
                std::vector<MockRow> filtered;
                filtered.reserve(result.data.size());
                for (const auto& r : result.data) {
                    if (filter.match(r)) filtered.push_back(r);
                }
                result.data.swap(filtered);
            }


            // ── Basic ORDER BY ──
            // Supports: "ORDER BY column [ASC|DESC]"
            {
                if (!query.order_by.empty()) {
                    const std::string order_spec = query.order_by;
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
                // D41: one entry per select-list item, so a constant keeps its
                // position. The old code built a column-index list and simply
                // dropped anything that was not a column, which silently
                // shifted every later column one place left.
                struct Projection {
                    int index = -1;          // >= 0: a table column
                    bool literal = false;
                    CellValue value;
                };
                std::vector<Projection> plan;
                for (const auto& col_name : query.columns) {
                    Projection p;
                    for (size_t j = 0; j < table->columns.size(); ++j) {
                        if (to_upper(table->columns[j].name) == to_upper(col_name)) {
                            p.index = static_cast<int>(j);
                            break;
                        }
                    }
                    if (p.index < 0) {
                        SelectLiteral lit = parse_select_literal(col_name);
                        if (lit.is_literal) {
                            p.literal = true;
                            p.value = lit.value;
                        }
                    }
                    plan.push_back(std::move(p));
                }
                std::vector<MockRow> projected;
                for (const auto& row : result.data) {
                    MockRow proj_row;
                    for (const auto& p : plan) {
                        if (p.literal) {
                            proj_row.push_back(p.value);
                        } else if (p.index >= 0 &&
                                   p.index < static_cast<int>(row.size())) {
                            proj_row.push_back(row[p.index]);
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
            // Multi-tuple `INSERT … VALUES (…),(…),…` lands here as a
            // flat insert_values vector with insert_row_count tuples.
            // We slice by stride and emit one MockRow per tuple.
            result.success = true;
            const size_t row_count = std::max<size_t>(query.insert_row_count, 1);
            const size_t total_vals = query.insert_values.size();
            // D12: `INSERT INTO t (a, b) VALUES (1,,3)` names two columns
            // and supplies three values. The mismatch was absorbed by the
            // stride fall-back below, which built a row out of whatever
            // lined up - a malformed statement reported as success.
            if (!query.insert_columns.empty() && total_vals > 0
                && total_vals % query.insert_columns.size() != 0) {
                result.success = false;
                result.error_sqlstate = "21S01";
                result.error_message =
                    "Insert value list does not match column list: "
                    + std::to_string(total_vals) + " values for "
                    + std::to_string(query.insert_columns.size()) + " columns";
                break;
            }
            const size_t stride = (row_count > 0 && total_vals > 0)
                ? total_vals / row_count : 0;
            // Defensive: if the tuple count and value count don't divide evenly
            // we fall back to a single-row insert (shouldn't happen with a
            // well-formed VALUES clause).
            if (stride == 0 || stride * row_count != total_vals) {
                if (!query.insert_values.empty()) {
                    MockRow row;
                    if (!query.insert_columns.empty()
                        && query.insert_columns.size() == query.insert_values.size()) {
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
                        while (row.size() < table->columns.size())
                            row.push_back(std::monostate{});
                    }
                    auto corruption = BehaviorController::instance().config().silent_corruption;
                    if (apply_silent_corruption(row, *table, corruption)) {
                        write_insert(catalog, query.table_name, std::move(row), txn);
                    }
                }
                result.affected_rows = static_cast<long long>(query.insert_row_count);
                break;
            }

            const auto corruption = BehaviorController::instance().config().silent_corruption;
            long long inserted = 0;
            for (size_t r = 0; r < row_count; ++r) {
                std::vector<CellValue> tuple_values(
                    query.insert_values.begin() + r * stride,
                    query.insert_values.begin() + (r + 1) * stride);

                MockRow row;
                if (!query.insert_columns.empty()
                    && query.insert_columns.size() == tuple_values.size()) {
                    row.resize(table->columns.size(), std::monostate{});
                    for (size_t i = 0; i < query.insert_columns.size(); ++i) {
                        for (size_t j = 0; j < table->columns.size(); ++j) {
                            if (to_upper(table->columns[j].name) == query.insert_columns[i]) {
                                row[j] = tuple_values[i];
                                break;
                            }
                        }
                    }
                } else {
                    row = std::move(tuple_values);
                    while (row.size() < table->columns.size())
                        row.push_back(std::monostate{});
                }
                if (apply_silent_corruption(row, *table, corruption)) {
                    write_insert(catalog, query.table_name, std::move(row), txn);
                    ++inserted;
                }
            }
            result.affected_rows = inserted;
            break;
        }
        
        case ParsedQuery::QueryType::Update: {
            // SQLRowCount must reflect the real number of matched rows
            // (the parser used to hard-code 1; that was wrong even for
            // the simple no-WHERE case the §1.8 probes exercise). The
            // SET clause itself isn't applied — the mock has no SET
            // evaluator, and no probe today reads back UPDATEd values.
            result.success = true;
            auto filter = make_where_filter(*table, query.where_clause);
            if (!filter.understood) {
                result.success = false;
                result.error_sqlstate = filter.sqlstate;
                result.error_message = filter.message;
                break;
            }
            // D13: same row store as SELECT - see the note there.
            if (!catalog.has_row_store(query.table_name)
                && table->remarks != "User-created table") {
                catalog.materialize_rows(query.table_name,
                                         generate_mock_data(*table, result_set_size));
            }
            // D83: resolve each SET target to a column index once, here,
            // where the table is in hand. An assignment naming a column the
            // table does not have is 42S22 - the same answer a SELECT of an
            // unknown column gives, rather than a silently ignored clause.
            std::vector<std::pair<size_t, CellValue>> assignments;
            bool bad_column = false;
            for (const auto& set : query.set_clauses) {
                const std::string want = to_upper(set.column);
                size_t index = table->columns.size();
                for (size_t ci = 0; ci < table->columns.size(); ++ci) {
                    if (to_upper(table->columns[ci].name) == want) {
                        index = ci;
                        break;
                    }
                }
                if (index == table->columns.size()) {
                    result.success = false;
                    result.error_sqlstate = "42S22";
                    result.error_message = "Column not found: " + set.column;
                    bad_column = true;
                    break;
                }
                assignments.emplace_back(index, set.value);
            }
            if (bad_column) break;

            // D83: SilentCorruption=DropUpdates. Resolving the SET targets
            // happens first and is not skipped, so an UPDATE naming a column
            // the table has not got is still 42S22 in this mode - the driver
            // being modelled loses the write, it does not stop parsing. Then
            // the assignments are dropped, and write_update counts the
            // matched rows and writes nothing: the right SQLRowCount over
            // unchanged data.
            if (BehaviorController::instance().config().silent_corruption ==
                DriverConfig::SilentCorruptionMode::DropUpdates) {
                assignments.clear();
            }

            result.affected_rows = static_cast<long long>(
                write_update(catalog, query.table_name, filter.match,
                             assignments, txn));
            break;
        }
        case ParsedQuery::QueryType::Delete: {
            result.success = true;
            auto filter = make_where_filter(*table, query.where_clause);
            if (!filter.understood) {
                // The one that mattered: this used to erase the whole table.
                result.success = false;
                result.error_sqlstate = filter.sqlstate;
                result.error_message = filter.message;
                break;
            }
            // D13: same row store as SELECT - see the note there.
            if (!catalog.has_row_store(query.table_name)
                && table->remarks != "User-created table") {
                catalog.materialize_rows(query.table_name,
                                         generate_mock_data(*table, result_set_size));
            }
            result.affected_rows = static_cast<long long>(
                write_delete(catalog, query.table_name, filter.match, txn));
            break;
        }
            
        default:
            result.success = false;
            result.error_message = "Unsupported operation";
            result.error_sqlstate = "42000";
            break;
    }
    
    return result;
}

} // namespace mock_odbc
