// Statement API - SQLExecDirect, SQLPrepare, SQLExecute, SQLFetch, etc.

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_data.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include "utils/buffer_copy.hpp"
#include "utils/c_types.hpp"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cctype>
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

namespace {

// Get the size of the C data element for column-wise offset computation
static SQLLEN c_type_element_size(SQLSMALLINT value_type, SQLLEN buffer_length) {
    switch (value_type) {
        case SQL_C_SLONG:
        case SQL_C_LONG:
        case SQL_C_ULONG:
            return static_cast<SQLLEN>(sizeof(SQLINTEGER));
        case SQL_C_SBIGINT:
        case SQL_C_UBIGINT:
            return static_cast<SQLLEN>(sizeof(SQLBIGINT));
        case SQL_C_SSHORT:
        case SQL_C_USHORT:
            return static_cast<SQLLEN>(sizeof(SQLSMALLINT));
        case SQL_C_STINYINT:
        case SQL_C_UTINYINT:
        case SQL_C_BIT:
            return 1;
        case SQL_C_DOUBLE:
            return static_cast<SQLLEN>(sizeof(SQLDOUBLE));
        case SQL_C_FLOAT:
            return static_cast<SQLLEN>(sizeof(SQLREAL));
        case SQL_C_NUMERIC:
            return static_cast<SQLLEN>(sizeof(SQL_NUMERIC_STRUCT));
        case SQL_C_TYPE_DATE:
            return static_cast<SQLLEN>(sizeof(DATE_STRUCT));
        case SQL_C_TYPE_TIME:
            return static_cast<SQLLEN>(sizeof(TIME_STRUCT));
        case SQL_C_TYPE_TIMESTAMP:
            return static_cast<SQLLEN>(sizeof(TIMESTAMP_STRUCT));
        case SQL_C_CHAR:
        case SQL_C_WCHAR:
        case SQL_C_BINARY:
        default:
            return buffer_length > 0 ? buffer_length : 1;
    }
}

// Read a CellValue from a parameter binding for parameter-set index 'row'.
// When param_bind_type == SQL_PARAM_BIND_BY_COLUMN (0), column-wise:
//   data_ptr  = base_data_ptr  + row * element_size
//   ind_ptr   = base_ind_ptr   + row
// When param_bind_type != 0 (row-wise):
//   data_ptr  = (char*)base_data_ptr + row * param_bind_type
//   ind_ptr   = (SQLLEN*)((char*)base_ind_ptr + row * param_bind_type)
static CellValue read_param_value(
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
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u",
                          d->year, d->month, d->day);
            return std::string(buf);
        }
        case SQL_C_TYPE_TIME: {
            const auto* t = reinterpret_cast<const TIME_STRUCT*>(data_ptr);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                          t->hour, t->minute, t->second);
            return std::string(buf);
        }
        case SQL_C_TYPE_TIMESTAMP: {
            const auto* ts = reinterpret_cast<const TIMESTAMP_STRUCT*>(data_ptr);
            char buf[40];
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
            // produced the 5-byte value `it's `, which matched nothing and
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
static void apply_proc_output_writeback(
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
    fprintf(stderr, "[T] before='%s'\n", clause.c_str());

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
    fprintf(stderr, "[T] after='%s'\n", clause.c_str());
}

// Substitute bound parameter values into a ParsedQuery for param-set 'row'.
// Handles both INSERT (insert_values) and literal SELECT (literal_exprs).
static void substitute_params(
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
    // error anywhere. The markers in the WHERE are substituted first for
    // SELECT and DELETE, where they are the only ones; for UPDATE the SET
    // clause's markers come first in the numbering, and the mock has no SET
    // evaluator yet, so its WHERE numbering is left alone rather than
    // guessed at.
    if (parsed.query_type == ParsedQuery::QueryType::Select ||
        parsed.query_type == ParsedQuery::QueryType::Delete) {
        SQLUSMALLINT where_param = 0;
        substitute_where_markers(parsed.where_clause, bindings, row,
                                 param_bind_type, where_param);
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
        SQLUSMALLINT param_idx = 0;
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

} // anonymous namespace

extern "C" {

SQLRETURN SQL_API SQLExecDirect(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    // Check for failure injection
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLExecDirect")) {
        for (int i = 0; i < config.error_count; ++i) {
            stmt->add_diagnostic(config.error_code, i + 1,
                "Simulated execution failure (record " + std::to_string(i + 1) + " of " + std::to_string(config.error_count) + ")");
        }
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Parse and execute SQL
    stmt->sql_ = sql_to_string(szSqlStr, static_cast<SQLSMALLINT>(cbSqlStr));
    auto parsed = parse_sql(stmt->sql_);
    
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    auto result = execute_query(parsed, config.result_set_size);

    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }

    // PORT plan port 3 — write OUT/INOUT/RETURN values back to bound params.
    if (!result.proc_name.empty()) {
        apply_proc_output_writeback(stmt, result.proc_name, result.proc_output_values);
    }

    // Store result
    stmt->executed_ = true;
    stmt->prepared_ = false;
    stmt->cursor_open_ = !result.data.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    // Propagate the executor's affected_rows verbatim when it set one — this
    // includes the spec-baseline -1 from `EXECUTE PROCEDURE` (affected count
    // unknown). Only fall back to the result-set size when `affected_rows`
    // is exactly 0 (no DML happened, the rows are query results).
    stmt->row_count_ = result.affected_rows != 0
                       ? result.affected_rows
                       : static_cast<SQLLEN>(result.data.size());
    
    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->result_data_.clear();
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLPrepare(
    SQLHSTMT hstmt,
    SQLCHAR* szSqlStr,
    SQLINTEGER cbSqlStr) MOCK_ENTRY_TRY {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLPrepare")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated prepare failure");
        return SQL_ERROR;
    }
    
    stmt->sql_ = sql_to_string(szSqlStr, static_cast<SQLSMALLINT>(cbSqlStr));
    
    // Validate SQL syntax
    auto parsed = parse_sql(stmt->sql_);
    if (!parsed.is_valid) {
        stmt->add_diagnostic(sqlstate::SYNTAX_ERROR, 0, parsed.error_message);
        return SQL_ERROR;
    }
    
    stmt->prepared_ = true;
    stmt->executed_ = false;
    stmt->cursor_open_ = false;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLExecute(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    if (!stmt->prepared_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Statement not prepared");
        return SQL_ERROR;
    }
    
    auto* conn = stmt->connection();
    if (!conn || !conn->is_connected()) {
        stmt->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                            "Connection not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLExecute")) {
        // For array params, fill status array with errors
        if (stmt->paramset_size_ > 1 && stmt->param_status_ptr_) {
            for (SQLULEN i = 0; i < stmt->paramset_size_; ++i) {
                stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
            }
        }
        if (stmt->params_processed_ptr_) {
            *stmt->params_processed_ptr_ = stmt->paramset_size_;
        }
        stmt->add_diagnostic(config.error_code, 0, "Simulated execute failure");
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    // Parse SQL once
    auto parsed = parse_sql(stmt->sql_);
    
    // --- Array parameter execution ---
    if (stmt->paramset_size_ > 1) {
        SQLULEN success_count = 0;
        SQLULEN error_count = 0;
        SQLULEN processed = 0;
        SQLLEN total_affected = 0;
        
        // Accumulate result data from all parameter sets
        std::vector<std::string> result_col_names;
        std::vector<SQLSMALLINT> result_col_types;
        std::vector<std::vector<std::variant<std::monostate, long long, double, std::string>>> all_result_data;
        
        for (SQLULEN i = 0; i < stmt->paramset_size_; ++i) {
            processed = i + 1;
            
            // Check operation array for SQL_PARAM_IGNORE
            if (stmt->param_operation_ptr_ && 
                stmt->param_operation_ptr_[i] == SQL_PARAM_IGNORE) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_UNUSED;
                }
                continue;
            }
            
            // PORT plan port 6 canary — ArrayBindRowFailsAt: synthesize a
            // SQLSTATE 23000 error for the configured 1-indexed row, leave
            // surrounding rows to execute normally. Drives the per-row
            // status array probe.
            if (config.array_bind_row_fails_at > 0 &&
                static_cast<int>(i + 1) == config.array_bind_row_fails_at) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
                }
                error_count++;
                stmt->add_diagnostic("23000", 0,
                    "Parameter set " + std::to_string(i + 1) +
                    ": injected ArrayBindRowFailsAt failure");
                continue;
            }

            // Execute with current parameter set — substitute bound param values
            ParsedQuery row_parsed = parsed;
            substitute_params(row_parsed, stmt->parameter_bindings_, i, stmt->param_bind_type_);
            auto result = execute_query(row_parsed, config.result_set_size);

            if (result.success) {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_SUCCESS;
                }
                success_count++;
                total_affected += result.affected_rows > 0 ? result.affected_rows : 
                                  static_cast<SQLLEN>(result.data.size());
                
                // Capture column metadata from first successful execution
                if (result_col_names.empty() && !result.column_names.empty()) {
                    result_col_names = result.column_names;
                    for (auto t : result.column_types) {
                        result_col_types.push_back(t);
                    }
                }
                
                // Accumulate result data
                for (const auto& row : result.data) {
                    std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
                    for (const auto& cell : row) {
                        converted_row.push_back(cell);
                    }
                    all_result_data.push_back(std::move(converted_row));
                }
            } else {
                if (stmt->param_status_ptr_) {
                    stmt->param_status_ptr_[i] = SQL_PARAM_ERROR;
                }
                error_count++;
                stmt->add_diagnostic(result.error_sqlstate, 0, 
                    "Parameter set " + std::to_string(i + 1) + ": " + result.error_message);
            }
        }
        
        // Set params processed count
        if (stmt->params_processed_ptr_) {
            *stmt->params_processed_ptr_ = processed;
        }
        
        // Set statement state
        stmt->executed_ = true;
        stmt->cursor_open_ = !all_result_data.empty();
        stmt->current_row_ = -1;
        stmt->row_count_ = total_affected;
        stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result_col_names.size());
        stmt->column_names_ = std::move(result_col_names);
        stmt->column_types_ = std::move(result_col_types);
        stmt->result_data_ = std::move(all_result_data);
        
        // Determine return code based on success/error counts
        if (error_count == 0) {
            return SQL_SUCCESS;
        } else if (success_count == 0) {
            return SQL_ERROR;
        } else {
            // Mixed results - some succeeded, some failed
            return SQL_SUCCESS_WITH_INFO;
        }
    }
    
    // --- Single parameter set execution (original path) ---
    
    // Substitute bound parameter values into the parsed query (INSERT and literal SELECT)
    substitute_params(parsed, stmt->parameter_bindings_, 0, stmt->param_bind_type_);
    
    auto result = execute_query(parsed, config.result_set_size);

    if (!result.success) {
        stmt->add_diagnostic(result.error_sqlstate, 0, result.error_message);
        return SQL_ERROR;
    }

    // PORT plan port 3 — write OUT/INOUT/RETURN values back to bound params.
    if (!result.proc_name.empty()) {
        apply_proc_output_writeback(stmt, result.proc_name, result.proc_output_values);
    }

    // Set params processed for single execution too
    if (stmt->params_processed_ptr_) {
        *stmt->params_processed_ptr_ = 1;
    }
    if (stmt->param_status_ptr_) {
        stmt->param_status_ptr_[0] = SQL_PARAM_SUCCESS;
    }

    stmt->executed_ = true;
    stmt->cursor_open_ = !result.data.empty();
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(result.column_names.size());
    // Same `affected_rows != 0` semantics as SQLExecDirect — propagates -1
    // from EXECUTE PROCEDURE verbatim into SQLRowCount.
    stmt->row_count_ = result.affected_rows != 0
                       ? result.affected_rows
                       : static_cast<SQLLEN>(result.data.size());

    stmt->column_names_ = std::move(result.column_names);
    stmt->column_types_.clear();
    for (auto t : result.column_types) {
        stmt->column_types_.push_back(t);
    }
    stmt->result_data_.clear();
    for (const auto& row : result.data) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> converted_row;
        for (const auto& cell : row) {
            converted_row.push_back(cell);
        }
        stmt->result_data_.push_back(std::move(converted_row));
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLFetch(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    
    stmt->clear_diagnostics();
    
    if (!stmt->executed_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor is not open");
        return SQL_ERROR;
    }
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLFetch")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated fetch failure");
        return SQL_ERROR;
    }
    
    // Move to next row
    stmt->current_row_++;
    
    if (stmt->current_row_ >= static_cast<SQLLEN>(stmt->result_data_.size())) {
        stmt->cursor_open_ = false;
        return SQL_NO_DATA;
    }
    
    // Transfer data to bound columns
    const auto& row = stmt->result_data_[stmt->current_row_];
    
    for (const auto& [col_num, binding] : stmt->column_bindings_) {
        if (col_num < 1 || col_num > static_cast<SQLUSMALLINT>(row.size())) {
            continue;
        }
        
        const auto& cell = row[col_num - 1];
        
        // Handle NULL
        if (std::holds_alternative<std::monostate>(cell)) {
            if (binding.str_len_or_ind) {
                *binding.str_len_or_ind = SQL_NULL_DATA;
            }
            continue;
        }
        
        // D11/D18: one delivery path for every numeric C type.
        //
        // This used to be two switches that between them handled SQL_C_SLONG,
        // SQL_C_SBIGINT, SQL_C_SSHORT, SQL_C_DOUBLE and SQL_C_FLOAT, with
        // everything else falling through to `default:` and being written as
        // an **ANSI decimal string**. An application binding a column as
        // SQL_C_ULONG, SQL_C_UBIGINT, SQL_C_UTINYINT, SQL_C_BIT or
        // SQL_C_WCHAR got the characters of the number laid over its buffer
        // and an indicator claiming a string length - SQL_SUCCESS returned,
        // garbage delivered. write_numeric_as knows every C type in the
        // table, so a type added there is handled here without an edit.
        const bool is_float_cell = std::holds_alternative<double>(cell);
        if (is_float_cell || std::holds_alternative<long long>(cell)) {
            // D34
            const long long ival = is_float_cell
                ? 0
                : apply_numeric_skew(std::get<long long>(cell),
                                     FetchPath::BoundColumn);
            const double dval = is_float_cell
                ? apply_numeric_skew(std::get<double>(cell),
                                     FetchPath::BoundColumn)
                : 0.0;

            const SQLRETURN wrote = write_numeric_as(
                binding.target_type, ival, dval, is_float_cell,
                binding.target_value, binding.buffer_length,
                binding.str_len_or_ind);

            if (wrote == SQL_ERROR) {
                // A numeric cell requested as a date, an interval or an
                // unknown C type. 07006 is the spec's answer for a
                // conversion it does not define, and it is a great deal more
                // useful than silently writing digits.
                stmt->add_diagnostic(sqlstate::DATA_TYPE_ATTRIBUTE_VIOLATION, 0,
                                     "Restricted data type attribute "
                                     "violation for column " +
                                     std::to_string(col_num));
                return SQL_ERROR;
            }
            if (wrote == SQL_SUCCESS_WITH_INFO) {
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                     "String data, right truncated");
            }
        
        } else if (std::holds_alternative<std::string>(cell)) {
            const std::string& value = std::get<std::string>(cell);
            
            if (binding.target_value && binding.buffer_length > 0) {
                size_t copy_len = std::min(value.length(),
                                           static_cast<size_t>(binding.buffer_length - 1));
                std::memcpy(binding.target_value, value.c_str(), copy_len);
                static_cast<char*>(binding.target_value)[copy_len] = '\0';
            }
            if (binding.str_len_or_ind) {
                *binding.str_len_or_ind = static_cast<SQLLEN>(value.length());
            }
        }
    }
    
    // D35: a driver that warns on every row it returns. The application must
    // keep fetching until SQL_NO_DATA; a loop written `== SQL_SUCCESS` stops
    // here instead, mid-result-set.
    if (config.fetch_returns_warning) {
        stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                             "String data, right truncated");
        return SQL_SUCCESS_WITH_INFO;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetData(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // D36 (down payment): SQLGetData was not faultable, so a probe that
    // ignored its return code could not be caught. A27 is exactly that bug -
    // two transaction probes read COUNT(*) into a variable initialised to 0
    // and never checked the call, and 0 was one of them's PASS condition.
    {
        const auto& config = BehaviorController::instance().config();
        if (config.should_fail("SQLGetData")) {
            stmt->add_diagnostic(config.error_code, 0,
                                 "Simulated SQLGetData failure");
            return SQL_ERROR;
        }
    }

    if (!stmt->executed_ || stmt->current_row_ < 0) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "No current row");
        return SQL_ERROR;
    }
    
    if (stmt->result_data_.empty() || 
        static_cast<size_t>(stmt->current_row_) >= stmt->result_data_.size()) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Invalid row position");
        return SQL_ERROR;
    }
    
    if (icol < 1 || icol > static_cast<SQLUSMALLINT>(stmt->result_data_[stmt->current_row_].size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    // D37: a continuation belongs to one (column, row). Asking for a
    // different column, or fetching, restarts the value - which is what the
    // spec says and what a caller looping on 01004 depends on.
    if (stmt->getdata_col_ != icol || stmt->getdata_row_ != stmt->current_row_) {
        stmt->getdata_col_ = icol;
        stmt->getdata_row_ = stmt->current_row_;
        stmt->getdata_offset_ = 0;
    }

    const auto& cell = stmt->result_data_[stmt->current_row_][icol - 1];

    // Handle NULL
    if (std::holds_alternative<std::monostate>(cell)) {
        // SilentCorruption=NullAsEmpty — Oracle-style "empty string and NULL
        // are the same thing" misbehaviour: for character target types the
        // mock returns an empty buffer with indicator=0 instead of
        // SQL_NULL_DATA. Drives the PORT plan port 2 e2e canary; correct
        // drivers/applications must distinguish the two.
        const auto& config = BehaviorController::instance().config();
        if (config.silent_corruption ==
                DriverConfig::SilentCorruptionMode::NullAsEmpty &&
            (fCType == SQL_C_CHAR || fCType == SQL_C_WCHAR ||
             fCType == SQL_C_DEFAULT || fCType == SQL_ARD_TYPE)) {
            if (rgbValue && cbValueMax > 0) {
                if (fCType == SQL_C_WCHAR) {
                    auto* w = static_cast<SQLWCHAR*>(rgbValue);
                    w[0] = 0;
                } else {
                    static_cast<char*>(rgbValue)[0] = '\0';
                }
            }
            if (pcbValue) *pcbValue = 0;
            return SQL_SUCCESS;
        }
        if (pcbValue) *pcbValue = SQL_NULL_DATA;
        return SQL_SUCCESS;
    }
    
    // Handle SQL_C_DEFAULT: map to appropriate type based on cell content
    SQLSMALLINT effective_type = fCType;
    if (fCType == SQL_C_DEFAULT || fCType == SQL_ARD_TYPE) {
        if (std::holds_alternative<long long>(cell)) effective_type = SQL_C_SBIGINT;
        else if (std::holds_alternative<double>(cell)) effective_type = SQL_C_DOUBLE;
        else effective_type = SQL_C_CHAR;
    }

    // Convert based on target type
    if (std::holds_alternative<long long>(cell)) {
        // D34
        long long value = apply_numeric_skew(std::get<long long>(cell),
                                             FetchPath::GetData);
        
        switch (effective_type) {
            case SQL_C_SLONG:
            case SQL_C_LONG:
                if (rgbValue) *static_cast<SQLINTEGER*>(rgbValue) = static_cast<SQLINTEGER>(value);
                if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
                break;
                
            case SQL_C_SBIGINT:
                if (rgbValue) *static_cast<SQLBIGINT*>(rgbValue) = value;
                if (pcbValue) *pcbValue = sizeof(SQLBIGINT);
                break;
                
            case SQL_C_SSHORT:
                if (rgbValue) *static_cast<SQLSMALLINT*>(rgbValue) = static_cast<SQLSMALLINT>(value);
                if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
                break;
            
            case SQL_C_DOUBLE:
                if (rgbValue) *static_cast<SQLDOUBLE*>(rgbValue) = static_cast<SQLDOUBLE>(value);
                if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                break;
                
            case SQL_C_NUMERIC: {
                // Convert integer to SQL_NUMERIC_STRUCT
                if (rgbValue) {
                    SQL_NUMERIC_STRUCT* ns = static_cast<SQL_NUMERIC_STRUCT*>(rgbValue);
                    std::memset(ns, 0, sizeof(SQL_NUMERIC_STRUCT));
                    ns->precision = 18;
                    ns->scale = 0;
                    ns->sign = (value >= 0) ? 1 : 0;
                    unsigned long long abs_val = (value >= 0) ? static_cast<unsigned long long>(value)
                                                              : static_cast<unsigned long long>(-value);
                    for (int b = 0; b < SQL_MAX_NUMERIC_LEN && abs_val > 0; ++b) {
                        ns->val[b] = static_cast<SQLCHAR>(abs_val & 0xFF);
                        abs_val >>= 8;
                    }
                }
                if (pcbValue) *pcbValue = sizeof(SQL_NUMERIC_STRUCT);
                break;
            }
                
            case SQL_C_WCHAR:
                break;  // Handled by SQL_C_WCHAR catch-all below
                
            case SQL_C_CHAR:
            default: {
                std::string str = std::to_string(value);
                if (rgbValue && cbValueMax > 0) {
                    size_t copy_len = std::min(str.length(), static_cast<size_t>(cbValueMax - 1));
                    std::memcpy(rgbValue, str.c_str(), copy_len);
                    static_cast<char*>(rgbValue)[copy_len] = '\0';
                }
                if (pcbValue) *pcbValue = static_cast<SQLLEN>(str.length());
                break;
            }
        }
    } else if (std::holds_alternative<double>(cell)) {
        // D34
        double value = apply_numeric_skew(std::get<double>(cell),
                                          FetchPath::GetData);
        
        switch (effective_type) {
            case SQL_C_DOUBLE:
                if (rgbValue) *static_cast<SQLDOUBLE*>(rgbValue) = value;
                if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                break;
                
            case SQL_C_FLOAT:
                if (rgbValue) *static_cast<SQLREAL*>(rgbValue) = static_cast<SQLREAL>(value);
                if (pcbValue) *pcbValue = sizeof(SQLREAL);
                break;
                
            case SQL_C_NUMERIC: {
                // Convert double to SQL_NUMERIC_STRUCT
                if (rgbValue) {
                    SQL_NUMERIC_STRUCT* ns = static_cast<SQL_NUMERIC_STRUCT*>(rgbValue);
                    std::memset(ns, 0, sizeof(SQL_NUMERIC_STRUCT));
                    ns->sign = (value >= 0) ? 1 : 0;
                    double abs_val = std::abs(value);
                    // Determine scale from decimal places
                    ns->scale = 0;
                    ns->precision = 18;
                    // Use provided descriptor scale if available (via descriptor)
                    // Default: detect decimal digits
                    double int_part;
                    double frac_part = std::modf(abs_val, &int_part);
                    SQLSCHAR scale = 0;
                    if (frac_part > 0.0) {
                        // Find appropriate scale (up to 10)
                        for (scale = 1; scale <= 10; ++scale) {
                            double scaled = abs_val * std::pow(10.0, scale);
                            double rounded = std::round(scaled);
                            if (std::abs(scaled - rounded) < 1e-6) break;
                        }
                    }
                    ns->scale = scale;
                    // Compute val[] = abs_val * 10^scale as little-endian integer
                    unsigned long long int_val = static_cast<unsigned long long>(
                        std::round(abs_val * std::pow(10.0, scale)));
                    for (int b = 0; b < SQL_MAX_NUMERIC_LEN && int_val > 0; ++b) {
                        ns->val[b] = static_cast<SQLCHAR>(int_val & 0xFF);
                        int_val >>= 8;
                    }
                }
                if (pcbValue) *pcbValue = sizeof(SQL_NUMERIC_STRUCT);
                break;
            }
                
            case SQL_C_WCHAR:
                break;  // Handled by SQL_C_WCHAR catch-all below
                
            case SQL_C_CHAR:
            default: {
                std::string str = std::to_string(value);
                if (rgbValue && cbValueMax > 0) {
                    size_t copy_len = std::min(str.length(), static_cast<size_t>(cbValueMax - 1));
                    std::memcpy(rgbValue, str.c_str(), copy_len);
                    static_cast<char*>(rgbValue)[copy_len] = '\0';
                }
                if (pcbValue) *pcbValue = static_cast<SQLLEN>(str.length());
                break;
            }
        }
    } else if (std::holds_alternative<std::string>(cell)) {
        const std::string& cell_value = std::get<std::string>(cell);
        // SilentCorruption=MangleUnicode — replace every non-ASCII byte (which
        // covers the leading bytes of any multibyte UTF-8 codepoint) with '?'.
        // Drives the PORT plan port 7 e2e canary; correct drivers preserve
        // every codepoint regardless of the system codepage. Only touches
        // char/wchar fetches — leaves date/time parsing alone.
        const auto& cfg = BehaviorController::instance().config();
        const bool mangle_unicode =
            cfg.silent_corruption ==
                DriverConfig::SilentCorruptionMode::MangleUnicode &&
            (effective_type == SQL_C_WCHAR ||
             effective_type == SQL_C_CHAR  ||
             effective_type == SQL_C_DEFAULT ||
             effective_type == SQL_ARD_TYPE);
        std::string mangled_buf;
        if (mangle_unicode) {
            mangled_buf = cell_value;
            for (auto& b : mangled_buf) {
                if (static_cast<unsigned char>(b) > 0x7F) b = '?';
            }
        }
        const std::string& value = mangle_unicode ? mangled_buf : cell_value;

        if (effective_type == SQL_C_WCHAR) {
            // Convert UTF-8 string to UTF-16 (SQLWCHAR)
            SQLSMALLINT wbytes = 0;
            SQLRETURN r = copy_string_to_wbuffer(value,
                              static_cast<SQLWCHAR*>(rgbValue),
                              static_cast<SQLINTEGER>(cbValueMax), &wbytes);
            // pcbValue reports total bytes needed (excl NUL), regardless of truncation
            if (pcbValue) *pcbValue = static_cast<SQLLEN>(wbytes);
            if (r == SQL_SUCCESS_WITH_INFO) {
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                    "String data, right truncated");
                return SQL_SUCCESS_WITH_INFO;
            }
        } else if (effective_type == SQL_C_TYPE_DATE) {
            // Parse date string "YYYY-MM-DD" into SQL_DATE_STRUCT
            SQL_DATE_STRUCT ds = {0, 0, 0};
            if (value.length() >= 10 && value[4] == '-' && value[7] == '-') {
                try {
                    ds.year = static_cast<SQLSMALLINT>(std::stoi(value.substr(0, 4)));
                    ds.month = static_cast<SQLUSMALLINT>(std::stoi(value.substr(5, 2)));
                    ds.day = static_cast<SQLUSMALLINT>(std::stoi(value.substr(8, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_DATE_STRUCT*>(rgbValue) = ds;
            if (pcbValue) *pcbValue = sizeof(SQL_DATE_STRUCT);
        } else if (effective_type == SQL_C_TYPE_TIME) {
            // Parse time string "HH:MM:SS" into SQL_TIME_STRUCT
            SQL_TIME_STRUCT ts = {0, 0, 0};
            if (value.length() >= 8 && value[2] == ':' && value[5] == ':') {
                try {
                    ts.hour = static_cast<SQLUSMALLINT>(std::stoi(value.substr(0, 2)));
                    ts.minute = static_cast<SQLUSMALLINT>(std::stoi(value.substr(3, 2)));
                    ts.second = static_cast<SQLUSMALLINT>(std::stoi(value.substr(6, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_TIME_STRUCT*>(rgbValue) = ts;
            if (pcbValue) *pcbValue = sizeof(SQL_TIME_STRUCT);
        } else if (effective_type == SQL_C_TYPE_TIMESTAMP) {
            // Parse timestamp string "YYYY-MM-DD HH:MM:SS" into SQL_TIMESTAMP_STRUCT
            SQL_TIMESTAMP_STRUCT tss = {0, 0, 0, 0, 0, 0, 0};
            if (value.length() >= 19) {
                try {
                    tss.year = static_cast<SQLSMALLINT>(std::stoi(value.substr(0, 4)));
                    tss.month = static_cast<SQLUSMALLINT>(std::stoi(value.substr(5, 2)));
                    tss.day = static_cast<SQLUSMALLINT>(std::stoi(value.substr(8, 2)));
                    tss.hour = static_cast<SQLUSMALLINT>(std::stoi(value.substr(11, 2)));
                    tss.minute = static_cast<SQLUSMALLINT>(std::stoi(value.substr(14, 2)));
                    tss.second = static_cast<SQLUSMALLINT>(std::stoi(value.substr(17, 2)));
                } catch (...) { /* leave as zeros */ }
            }
            if (rgbValue) *static_cast<SQL_TIMESTAMP_STRUCT*>(rgbValue) = tss;
            if (pcbValue) *pcbValue = sizeof(SQL_TIMESTAMP_STRUCT);
        } else if (effective_type == SQL_C_SLONG || effective_type == SQL_C_LONG ||
                   effective_type == SQL_C_SBIGINT || effective_type == SQL_C_SSHORT ||
                   effective_type == SQL_C_SHORT || effective_type == SQL_C_STINYINT ||
                   effective_type == SQL_C_DOUBLE || effective_type == SQL_C_FLOAT) {
            // Found by A21 in Phase 2: a character cell requested as a numeric
            // C type used to fall into the ANSI branch below, which memcpy'd
            // the raw bytes into the caller's 4-byte SQLINTEGER — SELECT '123'
            // read back as 3355185 (0x333231, the ASCII digits). SQL_CHAR to
            // SQL_C_SLONG is a *required Core* conversion, so this is the mock
            // silently corrupting data on a path the spec makes mandatory.
            //
            // Narrow fix for the integer and floating targets; the wider
            // C-type gaps in this switch remain D11's, and unifying the four
            // independent C-type switches remains D18's.
            const std::string trimmed = [&] {
                auto b = value.find_first_not_of(" 	");
                auto e = value.find_last_not_of(" 	");
                return (b == std::string::npos) ? std::string()
                                                : value.substr(b, e - b + 1);
            }();

            bool ok = false;
            double dbl = 0.0;
            long long ival = 0;
            try {
                size_t consumed = 0;
                if (effective_type == SQL_C_DOUBLE || effective_type == SQL_C_FLOAT) {
                    dbl = std::stod(trimmed, &consumed);
                    ok = consumed == trimmed.size() && !trimmed.empty();
                } else {
                    ival = std::stoll(trimmed, &consumed);
                    ok = consumed == trimmed.size() && !trimmed.empty();
                }
            } catch (...) {
                ok = false;
            }

            if (!ok) {
                // 22018 is what the spec requires for a character value that
                // cannot be cast to the requested type — not a wrong number.
                stmt->add_diagnostic(sqlstate::INVALID_CHARACTER_VALUE, 0,
                                     "Invalid character value for cast specification");
                return SQL_ERROR;
            }

            if (rgbValue) {
                switch (effective_type) {
                    case SQL_C_SLONG:
                    case SQL_C_LONG:
                        *static_cast<SQLINTEGER*>(rgbValue) =
                            static_cast<SQLINTEGER>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLINTEGER);
                        break;
                    case SQL_C_SBIGINT:
                        *static_cast<SQLBIGINT*>(rgbValue) =
                            static_cast<SQLBIGINT>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLBIGINT);
                        break;
                    case SQL_C_SSHORT:
                    case SQL_C_SHORT:
                        *static_cast<SQLSMALLINT*>(rgbValue) =
                            static_cast<SQLSMALLINT>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLSMALLINT);
                        break;
                    case SQL_C_STINYINT:
                        *static_cast<SQLSCHAR*>(rgbValue) =
                            static_cast<SQLSCHAR>(ival);
                        if (pcbValue) *pcbValue = sizeof(SQLSCHAR);
                        break;
                    case SQL_C_DOUBLE:
                        *static_cast<SQLDOUBLE*>(rgbValue) = dbl;
                        if (pcbValue) *pcbValue = sizeof(SQLDOUBLE);
                        break;
                    case SQL_C_FLOAT:
                        *static_cast<SQLREAL*>(rgbValue) = static_cast<SQLREAL>(dbl);
                        if (pcbValue) *pcbValue = sizeof(SQLREAL);
                        break;
                    default:
                        break;
                }
            }
        } else {
            // SQL_C_CHAR or default - return ANSI.
            //
            // D18: the copy, the terminator, the "bytes still available"
            // report and the 01004 all come from copy_chars now, so this
            // branch is the *policy* (where the offset lives, when the
            // sequence ends) and none of the mechanics.
            //
            // D37: the offset is why this is not a one-shot copy. A caller
            // retrieving a long value calls repeatedly; each call continues
            // where the last one stopped, and the sequence ends with
            // SQL_NO_DATA rather than by repeating the first bytes forever.
            const size_t offset = std::min(stmt->getdata_offset_,
                                           value.length());
            const bool exhausted = offset >= value.length();

            if (offset > 0 && exhausted) {
                // The previous call returned the last of the value, and the
                // spec is explicit that a further call yields SQL_NO_DATA.
                stmt->getdata_col_ = 0;
                stmt->getdata_row_ = -1;
                stmt->getdata_offset_ = 0;
                return SQL_NO_DATA;
            }

            const BufferCopyResult res =
                copy_chars(value, offset, rgbValue, cbValueMax);

            // Only advance when bytes were actually delivered. A zero-length
            // ask reports the size and consumes nothing, so the next call
            // starts from the same place.
            stmt->getdata_offset_ = offset + res.copied;

            // Bytes still available *as of this call*, excluding the
            // terminator - not the length of the whole column.
            if (pcbValue) *pcbValue = res.remaining;

            if (res.truncated) {
                // D24: posted here rather than at each call site, so an
                // application can always tell truncation from any other
                // warning by its SQLSTATE.
                stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                     "String data, right truncated");
                return SQL_SUCCESS_WITH_INFO;
            }
            // The value is complete. A further call for this column takes the
            // `exhausted` branch above and reports SQL_NO_DATA.
        }
    }
    
    // For integer/double cells requested as SQL_C_WCHAR, convert via string
    if (fCType == SQL_C_WCHAR && !std::holds_alternative<std::string>(cell) &&
        !std::holds_alternative<std::monostate>(cell)) {
        std::string str;
        if (std::holds_alternative<long long>(cell)) {
            str = std::to_string(std::get<long long>(cell));
        } else if (std::holds_alternative<double>(cell)) {
            str = std::to_string(std::get<double>(cell));
        }
        SQLSMALLINT wbytes = 0;
        SQLRETURN r = copy_string_to_wbuffer(str,
                          static_cast<SQLWCHAR*>(rgbValue),
                          static_cast<SQLINTEGER>(cbValueMax), &wbytes);
        if (pcbValue) *pcbValue = static_cast<SQLLEN>(wbytes);
        if (r == SQL_SUCCESS_WITH_INFO) {
            stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                "String data, right truncated");
            return SQL_SUCCESS_WITH_INFO;
        }
        return SQL_SUCCESS;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLNumResultCols(
    SQLHSTMT hstmt,
    SQLSMALLINT* pccol) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (pccol) {
        *pccol = stmt->num_result_cols_;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLDescribeCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLCHAR* szColName,
    SQLSMALLINT cbColNameMax,
    SQLSMALLINT* pcbColName,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbColDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (icol < 1 || icol > static_cast<SQLUSMALLINT>(stmt->column_names_.size())) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Invalid column number");
        return SQL_ERROR;
    }
    
    const std::string& name = stmt->column_names_[icol - 1];
    SQLSMALLINT type = stmt->column_types_[icol - 1];
    
    if (szColName) {
        // D24: the return code was discarded, so a column name that did not
        // fit came back cut short with no diagnostic at all.
        if (copy_string_to_buffer(name, szColName, cbColNameMax, pcbColName)
                == SQL_SUCCESS_WITH_INFO) {
            stmt->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                 "String data, right truncated");
        }
    } else if (pcbColName) {
        *pcbColName = static_cast<SQLSMALLINT>(name.length());
    }
    
    if (pfSqlType) *pfSqlType = type;
    
    // Default column size based on type
    if (pcbColDef) {
        switch (type) {
            case SQL_INTEGER: *pcbColDef = 10; break;
            case SQL_SMALLINT: *pcbColDef = 5; break;
            case SQL_BIGINT: *pcbColDef = 19; break;
            case SQL_VARCHAR:
            case SQL_WVARCHAR: *pcbColDef = 255; break;
            case SQL_CHAR:
            case SQL_WCHAR: *pcbColDef = 1; break;
            case SQL_DECIMAL: *pcbColDef = 18; break;
            case SQL_TYPE_DATE: *pcbColDef = 10; break;
            case SQL_TYPE_TIMESTAMP: *pcbColDef = 26; break;
            default: *pcbColDef = 255;
        }
    }
    
    if (pibScale) *pibScale = (type == SQL_DECIMAL) ? 2 : 0;
    if (pfNullable) *pfNullable = SQL_NULLABLE;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLBindCol(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLSMALLINT fCType,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (icol == 0) {
        // Unbind bookmark column - not supported
        return SQL_SUCCESS;
    }
    
    if (!rgbValue) {
        // Unbind column
        stmt->column_bindings_.erase(icol);
        return SQL_SUCCESS;
    }
    
    StatementHandle::ColumnBinding binding;
    binding.target_type = fCType;
    binding.target_value = rgbValue;
    binding.buffer_length = cbValueMax;
    binding.str_len_or_ind = pcbValue;
    
    stmt->column_bindings_[icol] = binding;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLBindParameter(
    SQLHSTMT hstmt,
    SQLUSMALLINT ipar,
    SQLSMALLINT fParamType,
    SQLSMALLINT fCType,
    SQLSMALLINT fSqlType,
    SQLULEN cbColDef,
    SQLSMALLINT ibScale,
    SQLPOINTER rgbValue,
    SQLLEN cbValueMax,
    SQLLEN* pcbValue) MOCK_ENTRY_TRY {
    
    (void)ibScale;

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (ipar == 0) {
        stmt->add_diagnostic(sqlstate::INVALID_PARAMETER_NUMBER, 0,
                            "Parameter number must be >= 1");
        return SQL_ERROR;
    }
    
    // Validate C type
    switch (fCType) {
        case SQL_C_CHAR:
        case SQL_C_WCHAR:
        case SQL_C_SSHORT:
        case SQL_C_USHORT:
        case SQL_C_SLONG:
        case SQL_C_ULONG:
        case SQL_C_FLOAT:
        case SQL_C_DOUBLE:
        case SQL_C_BIT:
        case SQL_C_STINYINT:
        case SQL_C_UTINYINT:
        case SQL_C_SBIGINT:
        case SQL_C_UBIGINT:
        case SQL_C_BINARY:
        case SQL_C_TYPE_DATE:
        case SQL_C_TYPE_TIME:
        case SQL_C_TYPE_TIMESTAMP:
        case SQL_C_NUMERIC:
        case SQL_C_DEFAULT:
            break; // Valid
        default:
            stmt->add_diagnostic(sqlstate::INVALID_APPLICATION_BUFFER_TYPE, 0,
                                "Invalid application buffer type");
            return SQL_ERROR;
    }
    
    if (!rgbValue) {
        // Unbind parameter
        stmt->parameter_bindings_.erase(ipar);
        return SQL_SUCCESS;
    }
    
    StatementHandle::ParameterBinding binding;
    binding.input_output_type = fParamType;
    binding.value_type = fCType;
    binding.param_type = fSqlType;
    binding.column_size = cbColDef;
    binding.decimal_digits = 0;
    binding.param_value = rgbValue;
    binding.buffer_length = cbValueMax;
    binding.str_len_or_ind = pcbValue;
    
    stmt->parameter_bindings_[ipar] = binding;
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLRowCount(
    SQLHSTMT hstmt,
    SQLLEN* pcrow) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (pcrow) {
        *pcrow = stmt->row_count_;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLCloseCursor(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (!stmt->cursor_open_) {
        stmt->add_diagnostic(sqlstate::INVALID_CURSOR_STATE, 0,
                            "Cursor not open");
        return SQL_ERROR;
    }
    
    stmt->cursor_open_ = false;
    stmt->current_row_ = -1;
    stmt->result_data_.clear();
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLMoreResults(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // Mock driver doesn't support multiple result sets
    return SQL_NO_DATA;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue) MOCK_ENTRY_TRY {
    
    (void)cbValueMax;
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) {
        return SQL_INVALID_HANDLE;
    }
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE: {
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->cursor_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
        }
            
        case SQL_ATTR_CONCURRENCY:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->concurrency_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_MAX_ROWS:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->max_rows_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_QUERY_TIMEOUT:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->query_timeout_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_ROW_ARRAY_SIZE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->row_array_size_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->paramset_size_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->async_enable_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;

        // Array parameter attributes
        case SQL_ATTR_PARAM_STATUS_PTR:
            if (rgbValue) *static_cast<SQLUSMALLINT**>(rgbValue) = stmt->param_status_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLUSMALLINT*);
            break;
            
        case SQL_ATTR_PARAMS_PROCESSED_PTR:
            if (rgbValue) *static_cast<SQLULEN**>(rgbValue) = stmt->params_processed_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;
            
        case SQL_ATTR_PARAM_BIND_TYPE:
            if (rgbValue) *static_cast<SQLULEN*>(rgbValue) = stmt->param_bind_type_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN);
            break;
            
        case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
            if (rgbValue) *static_cast<SQLULEN**>(rgbValue) = stmt->param_bind_offset_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLULEN*);
            break;
            
        case SQL_ATTR_PARAM_OPERATION_PTR:
            if (rgbValue) *static_cast<SQLUSMALLINT**>(rgbValue) = stmt->param_operation_ptr_;
            if (pcbValue) *pcbValue = sizeof(SQLUSMALLINT*);
            break;

        // Implicit descriptor handles — the DM queries these right after
        // SQLAllocHandle(SQL_HANDLE_STMT) to set up its internal dispatch.
        // Returning NULL causes a DM crash (ODBC32.dll access violation).
        case SQL_ATTR_APP_PARAM_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->app_param_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_IMP_PARAM_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->imp_param_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_APP_ROW_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->app_row_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
        case SQL_ATTR_IMP_ROW_DESC:
            if (rgbValue) *static_cast<SQLHANDLE*>(rgbValue) = static_cast<SQLHANDLE>(stmt->imp_row_desc_);
            if (pcbValue) *pcbValue = sizeof(SQLHANDLE);
            break;
            
        default:
            return SQL_SUCCESS;  // Ignore unknown attributes
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSetStmtAttr(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue) MOCK_ENTRY_TRY {
    
    (void)cbValue;

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    SQLULEN value = reinterpret_cast<SQLULEN>(rgbValue);
    
    switch (fAttribute) {
        case SQL_ATTR_CURSOR_TYPE:
            stmt->cursor_type_ = value;
            break;
            
        case SQL_ATTR_CONCURRENCY:
            stmt->concurrency_ = value;
            break;
            
        case SQL_ATTR_MAX_ROWS:
            stmt->max_rows_ = value;
            break;
            
        case SQL_ATTR_QUERY_TIMEOUT:
            stmt->query_timeout_ = value;
            break;
            
        case SQL_ATTR_ROW_ARRAY_SIZE:
            stmt->row_array_size_ = value;
            break;
            
        case SQL_ATTR_PARAMSET_SIZE:
            // PORT plan port 6 canary — SupportsArrayBind=false: report
            // option-not-supported when the application requests true
            // array-parameter execution (size > 1). Single-row binds
            // remain accepted so error-injected setup paths work.
            if (value > 1 &&
                !BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: array-parameter execution");
                return SQL_ERROR;
            }
            stmt->paramset_size_ = value;
            break;
            
        case SQL_ATTR_ASYNC_ENABLE:
            stmt->async_enable_ = value;
            break;
            
        // Array parameter attributes.
        //
        // SupportsArrayBind=false also declines these two, extended in Phase 2
        // for A8: a driver with no array-parameter execution has no use for a
        // per-row status array or a processed-row counter, and real ones do
        // reject them. Without this there was no configuration in which a
        // driver declines an optional Level 1 attribute, so A8's SKIP branch
        // — the one that stops a correct driver being FAILed at Core — could
        // not be exercised.
        case SQL_ATTR_PARAM_STATUS_PTR:
            if (!BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: SQL_ATTR_PARAM_STATUS_PTR");
                return SQL_ERROR;
            }
            stmt->param_status_ptr_ = static_cast<SQLUSMALLINT*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAMS_PROCESSED_PTR:
            if (!BehaviorController::instance().config().supports_array_bind) {
                stmt->add_diagnostic("HYC00", 0,
                    "Optional feature not implemented: SQL_ATTR_PARAMS_PROCESSED_PTR");
                return SQL_ERROR;
            }
            stmt->params_processed_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAM_BIND_TYPE:
            stmt->param_bind_type_ = value;
            break;
            
        case SQL_ATTR_PARAM_BIND_OFFSET_PTR:
            stmt->param_bind_offset_ptr_ = static_cast<SQLULEN*>(rgbValue);
            break;
            
        case SQL_ATTR_PARAM_OPERATION_PTR:
            stmt->param_operation_ptr_ = static_cast<SQLUSMALLINT*>(rgbValue);
            break;
            
        default:
            // Ignore unknown attributes
            break;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLFreeStmt(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;

    // D4: the `fOption != SQL_DROP` guard used to wrap only
    // clear_diagnostics(), so every option did its real work with no lock at
    // all. SQL_CLOSE clears result_data_, SQL_UNBIND clears
    // column_bindings_, SQL_RESET_PARAMS clears parameter_bindings_ - each of
    // which a concurrent SQLFetch iterates. That was the one place this
    // driver broke the invariant its own README states, and it is the shape
    // that corrupts rather than merely races.
    //
    // SQL_DROP is the exception, and has to stay outside the lock: it
    // deletes the object the mutex lives in, so unlocking afterwards would
    // touch freed memory.
    if (fOption == SQL_DROP) {
        delete stmt;
        return SQL_SUCCESS;
    }

    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    switch (fOption) {
        case SQL_CLOSE:
            stmt->cursor_open_ = false;
            stmt->current_row_ = -1;
            stmt->result_data_.clear();
            // D37: a closed cursor has no value to continue retrieving.
            stmt->getdata_col_ = 0;
            stmt->getdata_row_ = -1;
            stmt->getdata_offset_ = 0;
            break;

        case SQL_UNBIND:
            stmt->column_bindings_.clear();
            break;

        case SQL_RESET_PARAMS:
            stmt->parameter_bindings_.clear();
            break;

        default:
            break;
    }

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLCancel(SQLHSTMT hstmt) MOCK_ENTRY_TRY {
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // Mock: just reset state
    stmt->cursor_open_ = false;

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLNumParams(
    SQLHSTMT hstmt,
    SQLSMALLINT* pcpar) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    // Count ? placeholders in SQL
    int count = 0;
    for (char c : stmt->sql_) {
        if (c == '?') count++;
    }
    
    if (pcpar) *pcpar = static_cast<SQLSMALLINT>(count);
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLDescribeParam(
    SQLHSTMT hstmt,
    SQLUSMALLINT ipar,
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbParamDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable) MOCK_ENTRY_TRY {

    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    HandleLock lock(stmt);
    stmt->clear_diagnostics();

    if (!stmt->prepared_) {
        stmt->add_diagnostic(sqlstate::FUNCTION_SEQUENCE_ERROR, 0,
                            "Statement not prepared");
        return SQL_ERROR;
    }
    
    // Count parameter markers in the prepared SQL
    int num_params = 0;
    {
        bool in_sq = false, in_dq = false;
        for (size_t i = 0; i < stmt->sql_.length(); ++i) {
            char c = stmt->sql_[i];
            if (c == '\'' && !in_dq) {
                if (in_sq && i + 1 < stmt->sql_.length() && stmt->sql_[i + 1] == '\'') { ++i; continue; }
                in_sq = !in_sq;
            } else if (c == '"' && !in_sq) {
                in_dq = !in_dq;
            } else if (c == '?' && !in_sq && !in_dq) {
                ++num_params;
            }
        }
    }
    
    if (ipar < 1 || ipar > static_cast<SQLUSMALLINT>(num_params)) {
        stmt->add_diagnostic("HY000", 0,
                            "Invalid parameter number: " + std::to_string(ipar) +
                            " (statement has " + std::to_string(num_params) + " parameters)");
        return SQL_ERROR;
    }
    
    // D45: describe the parameter from the column it is bound to.
    //
    // This used to answer VARCHAR(255) for every parameter of every
    // statement, so the three SQLDescribeParam probes could not tell a
    // correct driver from one that has no idea what its own parameters are -
    // which is why they printed `expected` beside `actual` and never
    // compared them (B1). Describing an INTEGER parameter as VARCHAR(255) is
    // exactly the defect those probes exist to find.
    //
    // The parse is deliberately narrow: `INSERT INTO <t> (<cols>) VALUES
    // (?, ?, ...)`, which is the shape every probe in this suite uses. Any
    // statement it does not recognise keeps the old VARCHAR(255) answer,
    // which is a legal thing for a driver to say when it cannot infer more.
    SQLSMALLINT sql_type = SQL_VARCHAR;
    SQLULEN param_def = 255;
    SQLSMALLINT scale = 0;
    SQLSMALLINT nullable = SQL_NULLABLE;

    {
        ParsedQuery pq = parse_sql(stmt->sql_);
        if (pq.query_type == ParsedQuery::QueryType::Insert &&
            !pq.table_name.empty() &&
            ipar <= static_cast<SQLUSMALLINT>(pq.insert_columns.size())) {
            const MockTable* table =
                MockCatalog::instance().find_table(pq.table_name);
            if (table) {
                // `to_upper` is file-local to two other translation units,
                // so compare case-insensitively here rather than exporting it.
                auto same_name = [](const std::string& a, const std::string& b) {
                    if (a.size() != b.size()) return false;
                    for (size_t i = 0; i < a.size(); ++i) {
                        if (std::toupper(static_cast<unsigned char>(a[i])) !=
                            std::toupper(static_cast<unsigned char>(b[i]))) {
                            return false;
                        }
                    }
                    return true;
                };
                const std::string& want = pq.insert_columns[ipar - 1];
                for (const auto& col : table->columns) {
                    if (same_name(col.name, want)) {
                        sql_type  = col.data_type;
                        param_def = col.column_size;
                        scale     = static_cast<SQLSMALLINT>(col.decimal_digits);
                        nullable  = col.nullable;
                        break;
                    }
                }
            }
        }
    }

    if (pfSqlType) *pfSqlType = sql_type;
    if (pcbParamDef) *pcbParamDef = param_def;
    if (pibScale) *pibScale = scale;
    if (pfNullable) *pfNullable = nullable;

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hstmt)

// ODBC 2.x compatibility functions
SQLRETURN SQL_API SQLGetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLPOINTER pvParam) MOCK_ENTRY_TRY {
    // Map to ODBC 3.x function
    return SQLGetStmtAttr(hstmt, fOption, pvParam, SQL_MAX_OPTION_STRING_LENGTH, NULL);
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSetStmtOption(
    SQLHSTMT hstmt,
    SQLUSMALLINT fOption,
    SQLULEN vParam) MOCK_ENTRY_TRY {
    // Map to ODBC 3.x function
    return SQLSetStmtAttr(hstmt, fOption, reinterpret_cast<SQLPOINTER>(vParam), SQL_NTS);
}
MOCK_ENTRY_CATCH(hstmt)

} // extern "C"
