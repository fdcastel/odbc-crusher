// Parameter binding — reading bound parameter values and writing outputs back.
//
// D18: this was ~540 lines inside statement_api.cpp, which also holds the row
// delivery loop and the ODBC entry points themselves - three unrelated jobs in
// one 2,500-line file. Reading and writing bound parameters is the most
// self-contained of the three, so it moves first.
//
// Everything here is internal to the driver: the entry points in
// statement_api.cpp are the only callers, and they see these through
// param_binding.hpp.

#include "odbc/param_binding.hpp"

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_data.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include "utils/buffer_copy.hpp"
#include "utils/c_types.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

using namespace mock_odbc;

namespace mock_odbc {

namespace {

// D18: the local copy of c_type_element_size that lived here is gone -
// utils/c_types.hpp has the shared one, and the split is what made the
// duplicate visible: the two definitions only became an ambiguous call
// once they were in the same translation unit.

// Read a CellValue from a parameter binding for parameter-set index 'row'.
// When param_bind_type == SQL_PARAM_BIND_BY_COLUMN (0), column-wise:
//   data_ptr  = base_data_ptr  + row * element_size
//   ind_ptr   = base_ind_ptr   + row
// When param_bind_type != 0 (row-wise):
//   data_ptr  = (char*)base_data_ptr + row * param_bind_type
//   ind_ptr   = (SQLLEN*)((char*)base_ind_ptr + row * param_bind_type)
}  // anonymous namespace

CellValue read_param_value(
    const StatementHandle::ParameterBinding& pb,
    SQLULEN row,
    SQLULEN param_bind_type)
{
    if (!pb.param_value) return std::monostate{};

    const char* base_data = static_cast<const char*>(pb.param_value);
    const SQLLEN* base_ind = pb.str_len_or_ind;

    const char* data_ptr;
    const SQLLEN* ind_ptr;

    if (param_bind_type == SQL_PARAM_BIND_BY_COLUMN) {
        // Column-wise: stride by element size for data, by sizeof(SQLLEN) for indicator
        SQLLEN elem_size = c_type_element_size(pb.value_type, pb.buffer_length);
        if (elem_size <= 0) return std::monostate{};
        data_ptr = base_data + row * static_cast<SQLULEN>(elem_size);
        ind_ptr  = base_ind ? (base_ind + row) : nullptr;
    } else {
        // Row-wise: stride by the struct size (param_bind_type) for both.
        // A zero stride would make every row alias param-set 0; reject.
        if (param_bind_type == 0) return std::monostate{};
        data_ptr = base_data + row * param_bind_type;
        ind_ptr  = base_ind
            ? reinterpret_cast<const SQLLEN*>(
                  reinterpret_cast<const char*>(base_ind) + row * param_bind_type)
            : nullptr;
    }

    // Check for SQL_NULL_DATA
    if (ind_ptr && *ind_ptr == SQL_NULL_DATA) {
        return std::monostate{};
    }

    switch (pb.value_type) {
        case SQL_C_SLONG:
        case SQL_C_LONG:
            return static_cast<long long>(*reinterpret_cast<const SQLINTEGER*>(data_ptr));
        case SQL_C_ULONG:
            return static_cast<long long>(*reinterpret_cast<const SQLUINTEGER*>(data_ptr));
        case SQL_C_SBIGINT:
            return static_cast<long long>(*reinterpret_cast<const SQLBIGINT*>(data_ptr));
        case SQL_C_UBIGINT:
            // CellValue's int variant is 64-bit signed; values >= 2^63 wrap.
            // No probe binds such magnitudes today.
            return static_cast<long long>(*reinterpret_cast<const SQLUBIGINT*>(data_ptr));
        case SQL_C_SSHORT:
            return static_cast<long long>(*reinterpret_cast<const SQLSMALLINT*>(data_ptr));
        case SQL_C_USHORT:
            return static_cast<long long>(*reinterpret_cast<const SQLUSMALLINT*>(data_ptr));
        case SQL_C_STINYINT:
            return static_cast<long long>(*reinterpret_cast<const SQLSCHAR*>(data_ptr));
        case SQL_C_UTINYINT:
            return static_cast<long long>(*reinterpret_cast<const SQLCHAR*>(data_ptr));
        case SQL_C_BIT:
            return static_cast<long long>(*reinterpret_cast<const SQLCHAR*>(data_ptr) ? 1 : 0);
        case SQL_C_DOUBLE:
            return static_cast<double>(*reinterpret_cast<const SQLDOUBLE*>(data_ptr));
        case SQL_C_FLOAT:
            return static_cast<double>(*reinterpret_cast<const SQLREAL*>(data_ptr));
        case SQL_C_TYPE_DATE: {
            const auto* d = reinterpret_cast<const DATE_STRUCT*>(data_ptr);
            // E5: was char[16], sized for a plausible date rather than a
            // possible one. `month` and `day` are SQLUSMALLINT and a bound
            // parameter may hold 65535 in either - five digits each - and
            // `year` is a signed SQLSMALLINT that can print six. The worst
            // case is 19 bytes. snprintf truncated rather than overflowed,
            // so this was silently wrong rather than unsafe, which for a
            // conformance fixture is the worse of the two.
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u",
                          d->year, d->month, d->day);
            return std::string(buf);
        }
        case SQL_C_TYPE_TIME: {
            const auto* t = reinterpret_cast<const TIME_STRUCT*>(data_ptr);
            char buf[32];          // E5: 18 in the worst case, see above
            std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                          t->hour, t->minute, t->second);
            return std::string(buf);
        }
        case SQL_C_TYPE_TIMESTAMP: {
            const auto* ts = reinterpret_cast<const TIMESTAMP_STRUCT*>(data_ptr);
            // E5: was char[40]. `fraction` is a SQLUINTEGER printed with
            // %09u, which is a floor and not a ceiling - ten digits fit in
            // the type - so the worst case is 48.
            char buf[64];
            if (ts->fraction == 0) {
                std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u:%02u",
                              ts->year, ts->month, ts->day,
                              ts->hour, ts->minute, ts->second);
            } else {
                std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u:%02u.%09u",
                              ts->year, ts->month, ts->day,
                              ts->hour, ts->minute, ts->second,
                              static_cast<unsigned>(ts->fraction));
            }
            return std::string(buf);
        }
        case SQL_C_BINARY: {
            // Raw bytes: indicator carries the byte length; SQL_NTS is
            // not meaningful here, so treat negative/SQL_NTS as buffer_length.
            SQLLEN len = (ind_ptr && *ind_ptr >= 0) ? *ind_ptr : pb.buffer_length;
            if (len < 0) len = 0;
            return std::string(data_ptr, static_cast<size_t>(len));
        }
        case SQL_C_NUMERIC: {
            // Read SQL_NUMERIC_STRUCT and convert to double
            const SQL_NUMERIC_STRUCT* ns = reinterpret_cast<const SQL_NUMERIC_STRUCT*>(data_ptr);
            unsigned long long int_val = 0;
            for (int b = SQL_MAX_NUMERIC_LEN - 1; b >= 0; --b) {
                int_val = (int_val << 8) | ns->val[b];
            }
            double result = static_cast<double>(int_val) / std::pow(10.0, ns->scale);
            if (ns->sign == 0) result = -result;
            return result;
        }
        case SQL_C_WCHAR: {
            // UTF-16 input — convert to UTF-8 for storage. Indicator length,
            // when not SQL_NTS, is in BYTES per ODBC spec. SQL_NTS means
            // walk until 0x0000. Drives the PORT plan port 7 round-trip
            // probe; correct conversion preserves non-ASCII codepoints.
            const SQLWCHAR* wsrc = reinterpret_cast<const SQLWCHAR*>(data_ptr);
            // D48: `bytes` started at buffer_length and was only overwritten
            // when the indicator held something other than SQL_NTS - so
            // SQL_NTS *with* a buffer_length fell through to the fixed-length
            // path and took the whole buffer, terminator included. Per
            // SQLBindParameter, a null indicator pointer means the same thing
            // as SQL_NTS for character data.
            const bool is_nts = !ind_ptr || *ind_ptr == SQL_NTS;
            SQLLEN bytes = is_nts ? SQL_NTS : *ind_ptr;
            constexpr size_t kSafetyCapChars = 1 << 19;  // 512 K SQLWCHARs
            SQLINTEGER char_count;
            if (bytes == SQL_NTS || bytes < 0) {
                size_t max_scan_chars = pb.buffer_length > 0
                    ? static_cast<size_t>(pb.buffer_length / sizeof(SQLWCHAR))
                    : kSafetyCapChars;
                size_t actual = 0;
                while (actual < max_scan_chars && wsrc[actual] != 0) ++actual;
                char_count = static_cast<SQLINTEGER>(actual);
            } else {
                char_count = static_cast<SQLINTEGER>(bytes / sizeof(SQLWCHAR));
            }
            return sqlw_to_string(wsrc, char_count);
        }
        case SQL_C_CHAR:
        default: {
            // D48: this used to seed `len` from buffer_length and replace it
            // only when the indicator was not SQL_NTS. SQL_NTS with a
            // buffer_length therefore meant "the whole buffer" rather than
            // "up to the terminator", so binding `"it's"` out of a `char[5]`
            // produced the 5-byte value `it's` plus a NUL, which matched nothing and
            // reported no error. A null indicator pointer means null-terminated
            // too - SQLBindParameter says so explicitly.
            const bool is_nts = !ind_ptr || *ind_ptr == SQL_NTS;
            SQLLEN len = is_nts ? SQL_NTS : *ind_ptr;
            // Guard against unbounded strlen on a non-NUL-terminated buffer:
            // cap the walk at buffer_length when it is set. Without this a
            // malformed caller that passes SQL_NTS with a raw byte buffer
            // could read past the end of its buffer.
            constexpr size_t kSafetyCapBytes = 1 << 20;  // 1 MB
            if (len == SQL_NTS || len < 0) {
                size_t max_scan = pb.buffer_length > 0
                    ? static_cast<size_t>(pb.buffer_length)
                    : kSafetyCapBytes;
                size_t actual = ::strnlen(data_ptr, max_scan);
                return std::string(data_ptr, actual);
            }
            return std::string(data_ptr, static_cast<size_t>(len));
        }
    }
}

// PORT plan port 3 — write a procedure output value back to a bound OUT/INOUT
// parameter buffer. Handles the C types most often used at the application
// layer; unrecognised target types are a no-op (the indicator is left
// untouched). `value` is the CellValue produced by the MockProcedure callback.
static void write_output_to_binding(
    const StatementHandle::ParameterBinding& pb,
    const CellValue& value)
{
    if (!pb.param_value) return;

    // NULL output — set indicator and bail.
    if (std::holds_alternative<std::monostate>(value)) {
        if (pb.str_len_or_ind) *pb.str_len_or_ind = SQL_NULL_DATA;
        return;
    }

    auto write_int = [&](long long v) {
        switch (pb.value_type) {
            case SQL_C_SLONG:
            case SQL_C_LONG:
                *static_cast<SQLINTEGER*>(pb.param_value) =
                    static_cast<SQLINTEGER>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLINTEGER);
                break;
            case SQL_C_ULONG:
                *static_cast<SQLUINTEGER*>(pb.param_value) =
                    static_cast<SQLUINTEGER>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLUINTEGER);
                break;
            case SQL_C_SBIGINT:
                *static_cast<SQLBIGINT*>(pb.param_value) =
                    static_cast<SQLBIGINT>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLBIGINT);
                break;
            case SQL_C_UBIGINT:
                *static_cast<SQLUBIGINT*>(pb.param_value) =
                    static_cast<SQLUBIGINT>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLUBIGINT);
                break;
            case SQL_C_SSHORT: {
                auto sv = static_cast<SQLSMALLINT>(v);
                *static_cast<SQLSMALLINT*>(pb.param_value) = sv;
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLSMALLINT);
                break;
            }
            case SQL_C_USHORT:
                *static_cast<SQLUSMALLINT*>(pb.param_value) =
                    static_cast<SQLUSMALLINT>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLUSMALLINT);
                break;
            case SQL_C_STINYINT:
                *static_cast<SQLSCHAR*>(pb.param_value) =
                    static_cast<SQLSCHAR>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLSCHAR);
                break;
            case SQL_C_UTINYINT:
                *static_cast<SQLCHAR*>(pb.param_value) =
                    static_cast<SQLCHAR>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLCHAR);
                break;
            case SQL_C_BIT:
                *static_cast<SQLCHAR*>(pb.param_value) =
                    static_cast<SQLCHAR>(v ? 1 : 0);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLCHAR);
                break;
            case SQL_C_FLOAT:
                *static_cast<SQLREAL*>(pb.param_value) =
                    static_cast<SQLREAL>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLREAL);
                break;
            case SQL_C_DOUBLE:
                *static_cast<SQLDOUBLE*>(pb.param_value) =
                    static_cast<SQLDOUBLE>(v);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLDOUBLE);
                break;
            case SQL_C_CHAR:
            default: {
                // D8(c): this wrote `[0] = '\0'` even when buffer_length was
                // 0 - a one-byte store into a buffer the caller said had no
                // room - and reported what *fitted* in the indicator rather
                // than what was *available*, so an application sizing a
                // second call from it never grew past the first.
                const std::string s = std::to_string(v);
                const BufferCopyResult res =
                    copy_chars(s, 0, pb.param_value, pb.buffer_length);
                if (pb.str_len_or_ind) *pb.str_len_or_ind = res.remaining;
                break;
            }
        }
    };

    auto write_string = [&](const std::string& s) {
        if (pb.value_type == SQL_C_WCHAR) {
            SQLSMALLINT wbytes = 0;
            copy_string_to_wbuffer(s, static_cast<SQLWCHAR*>(pb.param_value),
                                   static_cast<SQLINTEGER>(pb.buffer_length),
                                   &wbytes);
            if (pb.str_len_or_ind) *pb.str_len_or_ind = static_cast<SQLLEN>(wbytes);
        } else {
            // SQL_C_CHAR / default - D8(c), see write_number above.
            const BufferCopyResult res =
                copy_chars(s, 0, pb.param_value, pb.buffer_length);
            if (pb.str_len_or_ind) *pb.str_len_or_ind = res.remaining;
        }
    };

    if (std::holds_alternative<long long>(value)) {
        write_int(std::get<long long>(value));
    } else if (std::holds_alternative<double>(value)) {
        const double dv = std::get<double>(value);
        if (pb.value_type == SQL_C_DOUBLE) {
            *static_cast<SQLDOUBLE*>(pb.param_value) = dv;
            if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLDOUBLE);
        } else if (pb.value_type == SQL_C_FLOAT) {
            *static_cast<SQLREAL*>(pb.param_value) = static_cast<SQLREAL>(dv);
            if (pb.str_len_or_ind) *pb.str_len_or_ind = sizeof(SQLREAL);
        } else if (pb.value_type == SQL_C_CHAR || pb.value_type == SQL_C_WCHAR) {
            write_string(std::to_string(dv));
        }
    } else if (std::holds_alternative<std::string>(value)) {
        write_string(std::get<std::string>(value));
    }
}

// PORT plan port 3 — after a CALL completes, copy the procedure's output
// values back to bound OUT/INOUT parameter buffers. params[i] maps to
// binding (i+1). SQL_PARAM_INPUT slots are skipped.
void apply_proc_output_writeback(
    StatementHandle* stmt,
    const std::string& proc_name,
    const std::vector<CellValue>& output_values)
{
    if (output_values.empty() || !stmt) return;
    auto proc = MockCatalog::instance().find_procedure(proc_name);
    if (!proc) return;
    for (size_t i = 0; i < proc->params.size() && i < output_values.size(); ++i) {
        const SQLSMALLINT dir = proc->params[i].direction;
        if (dir != SQL_PARAM_OUTPUT &&
            dir != SQL_PARAM_INPUT_OUTPUT &&
            dir != SQL_RETURN_VALUE) continue;
        auto it = stmt->parameter_bindings_.find(static_cast<SQLUSMALLINT>(i + 1));
        if (it == stmt->parameter_bindings_.end()) continue;
        write_output_to_binding(it->second, output_values[i]);
    }
}

// Render a bound value as the SQL literal text a WHERE clause expects - D10.
// Strings are single-quoted with embedded quotes doubled; NULL becomes the
// keyword, which the predicate builder already understands.
static std::string param_as_sql_literal(const CellValue& v) {
    if (std::holds_alternative<std::monostate>(v)) return "NULL";
    if (std::holds_alternative<long long>(v)) {
        return std::to_string(std::get<long long>(v));
    }
    if (std::holds_alternative<double>(v)) {
        return std::to_string(std::get<double>(v));
    }
    const std::string& s = std::get<std::string>(v);
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out + "'";
}

// Replace each `?` in `clause` with the next bound value, continuing the
// statement's parameter numbering from `next_param` - D10.
//
// Markers inside string literals are not markers; the scan tracks quoting for
// the same reason the parser does.
static void substitute_where_markers(
    std::string& clause,
    const std::unordered_map<SQLUSMALLINT, StatementHandle::ParameterBinding>& bindings,
    SQLULEN row,
    SQLULEN param_bind_type,
    SQLUSMALLINT& next_param)
{
    if (clause.find('?') == std::string::npos) return;

    std::string out;
    out.reserve(clause.size() + 16);
    bool in_quote = false;
    for (size_t i = 0; i < clause.size(); ++i) {
        const char c = clause[i];
        if (c == '\'') {
            // A doubled quote inside a literal is an escaped quote.
            if (in_quote && i + 1 < clause.size() && clause[i + 1] == '\'') {
                out += "''";
                ++i;
                continue;
            }
            in_quote = !in_quote;
            out += c;
            continue;
        }
        if (c == '?' && !in_quote) {
            ++next_param;
            auto it = bindings.find(next_param);
            if (it != bindings.end()) {
                out += param_as_sql_literal(
                    read_param_value(it->second, row, param_bind_type));
            } else {
                // Unbound marker: leave it, so the predicate builder fails to
                // match rather than matching the literal text "?".
                out += c;
            }
            continue;
        }
        out += c;
    }
    clause.swap(out);
}

// D22: SQL_DIAG_DYNAMIC_FUNCTION / _CODE name the SQL statement the driver
// just executed. Both fields were declared, read by SQLGetDiagField, and
// written by nobody, so every executed statement reported an empty name and
// code 0 (SQL_DIAG_UNKNOWN_STATEMENT) - which is what a driver reports when it
// does not know, not what it reports for `SELECT`.
//
// SQL_DIAG_CURSOR_ROW_COUNT goes the same way: it is the number of rows in the
// cursor, defined only after a cursor-opening statement, and it was always 0.

// Substitute bound parameter values into a ParsedQuery for param-set 'row'.
// Handles both INSERT (insert_values) and literal SELECT (literal_exprs).
void substitute_params(
    ParsedQuery& parsed,
    const std::unordered_map<SQLUSMALLINT, StatementHandle::ParameterBinding>& bindings,
    SQLULEN row,
    SQLULEN param_bind_type)
{
    if (bindings.empty() || parsed.param_count == 0) return;

    // D10: a table SELECT, UPDATE or DELETE matched none of the three
    // branches below, so its `?` survived into `where_clause` and was
    // compared as the literal two-character string "?" - `DELETE FROM t
    // WHERE id = ?` matched nothing and reported 0 rows affected, with no
    // error anywhere. For SELECT and DELETE the WHERE holds every marker the
    // statement has, so its numbering starts at 1.
    if (parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.query_type == ParsedQuery::QueryType::Delete) {
        SQLUSMALLINT where_param = 0;
        substitute_where_markers(parsed.where_clause, bindings, row,
                                 param_bind_type, where_param);
    }

    // D83: UPDATE numbers the SET clause's markers first, then continues into
    // the WHERE - one sequence across the statement.
    //
    // D10 left this branch out on purpose ("the mock has no SET evaluator
    // yet"), and while the SET clause was discarded that was neutral. It is
    // not neutral now: an unfilled marker in `set_clauses` holds monostate,
    // and the executor would write NULL over the cell. The deferral ends with
    // the evaluator that caused it.
    if (parsed.query_type == ParsedQuery::QueryType::Update) {
        SQLUSMALLINT param_idx = 0;
        for (auto& assignment : parsed.set_clauses) {
            if (!assignment.is_parameter_marker) continue;
            ++param_idx;
            auto it = bindings.find(param_idx);
            if (it != bindings.end()) {
                // An *unbound* marker keeps its monostate and so writes NULL,
                // which is what an unbound marker in an INSERT's value list
                // already does a few lines below. One answer, not two.
                assignment.value =
                    read_param_value(it->second, row, param_bind_type);
            }
        }
        substitute_where_markers(parsed.where_clause, bindings, row,
                                 param_bind_type, param_idx);
    }

    // INSERT parameter substitution — only substitute for '?' markers
    if (parsed.query_type == ParsedQuery::QueryType::Insert) {
        SQLUSMALLINT param_idx = 0;
        for (size_t vi = 0; vi < parsed.insert_values.size(); ++vi) {
            bool is_marker = vi < parsed.insert_param_markers.size()
                             && parsed.insert_param_markers[vi];
            if (is_marker) {
                param_idx++;
                auto it = bindings.find(param_idx);
                if (it != bindings.end()) {
                    parsed.insert_values[vi] = read_param_value(it->second, row, param_bind_type);
                }
            }
        }
        return;
    }

    // CALL <proc>(args) — same shape as INSERT but populates `proc_args`.
    if (parsed.query_type == ParsedQuery::QueryType::Call) {
        // D13: `{?=CALL fn(?)}` binds the return value as parameter 1, so the
        // first *argument* marker is parameter 2. Starting the count at 0 here
        // is what makes a driver read the return-value binding as the first
        // argument - the defect A17 is written to detect.
        SQLUSMALLINT param_idx = parsed.has_return_value ? 1 : 0;
        for (size_t ai = 0; ai < parsed.proc_args.size(); ++ai) {
            bool is_marker = ai < parsed.insert_param_markers.size()
                             && parsed.insert_param_markers[ai];
            if (is_marker) {
                param_idx++;
                auto it = bindings.find(param_idx);
                if (it != bindings.end()) {
                    parsed.proc_args[ai] = read_param_value(it->second, row, param_bind_type);
                }
            }
        }
        return;
    }

    // Literal SELECT parameter substitution
    if (parsed.is_literal_select) {
        SQLUSMALLINT param_idx = 0;
        for (auto& lit : parsed.literal_exprs) {
            // D10: `param_idx++` used to happen for *every* select-list
            // expression, so `SELECT 'a', 'b', ?` looked for parameter 3 when
            // SQLBindParameter had bound parameter 1. Parameter numbers count
            // markers, not expressions, and the two coincide only when every
            // expression is a marker - which is exactly the shape the
            // existing probes use, and why this survived.
            if (lit.is_parameter_marker) {
                param_idx++;
                auto it = bindings.find(param_idx);
                if (it != bindings.end()) {
                    CellValue cv = read_param_value(it->second, row, param_bind_type);
                    if (std::holds_alternative<std::monostate>(cv)) {
                        lit.value = std::monostate{};
                        lit.sql_type = SQL_VARCHAR;
                    } else if (std::holds_alternative<long long>(cv)) {
                        lit.value = std::get<long long>(cv);
                        lit.sql_type = SQL_INTEGER;
                        lit.column_size = 10;
                    } else if (std::holds_alternative<double>(cv)) {
                        lit.value = std::get<double>(cv);
                        lit.sql_type = SQL_DOUBLE;
                        lit.column_size = 15;
                    } else {
                        lit.value = std::get<std::string>(cv);
                        lit.sql_type = SQL_VARCHAR;
                        lit.column_size = 255;
                    }
                }
            }
        }
    }
}

}  // namespace mock_odbc
