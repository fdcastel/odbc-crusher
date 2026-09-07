#pragma once

// One description of the ODBC C types, and one place that writes a value
// into a caller's buffer according to it — D18.
//
// Four independent switches over `SQLSMALLINT c_type` existed:
// `c_type_element_size`, `read_param_value`, `write_output_to_binding`'s
// integer writer, and the fetch / SQLGetData delivery switches. They did not
// agree, and the disagreement is D11: the fetch delivery switch handled
// SQL_C_SLONG, SQL_C_SBIGINT and SQL_C_SSHORT and let everything else fall
// through to `default:`, where it wrote an **ANSI decimal string** into the
// caller's buffer. An application binding a column as SQL_C_ULONG got the
// characters "42" laid over its four-byte integer, with an indicator claiming
// a string length — success returned, garbage delivered.
//
// The types that fell through: SQL_C_ULONG, SQL_C_USHORT, SQL_C_UBIGINT,
// SQL_C_STINYINT, SQL_C_UTINYINT, SQL_C_BIT, SQL_C_DOUBLE, SQL_C_FLOAT,
// SQL_C_NUMERIC, SQL_C_WCHAR and the three datetime structs.

#include "../driver/common.hpp"

#include <cstddef>
#include <string>

namespace mock_odbc {

enum class CTypeKind {
    Integer,     // fixed-width integer, signed or not
    Float,       // SQLREAL / SQLDOUBLE
    Char,        // SQL_C_CHAR — variable length, needs the caller's buffer size
    WChar,       // SQL_C_WCHAR — as above, in UTF-16
    Binary,      // SQL_C_BINARY — variable length, no terminator
    Numeric,     // SQL_NUMERIC_STRUCT
    Date,
    Time,
    Timestamp,
    Unknown,
};

struct CTypeInfo {
    CTypeKind kind = CTypeKind::Unknown;
    size_t size = 0;        // fixed size in bytes; 0 for the variable kinds
    bool is_signed = true;  // meaningful for Integer

    bool is_fixed_size() const { return size != 0; }
};

// What a C type is. SQL_C_DEFAULT and SQL_ARD_TYPE are *not* resolved here —
// they depend on the cell being delivered, which is the caller's business.
CTypeInfo c_type_info(SQLSMALLINT c_type);

// Bytes one element of a bound array occupies. For the variable-length kinds
// this is the caller's own `buffer_length`, which is why it has to be passed
// in; returns 0 when the type is unknown and no sensible stride exists.
SQLLEN c_type_element_size(SQLSMALLINT c_type, SQLLEN buffer_length);

// Deliver a numeric value into `target` as `c_type` requires.
//
// `is_float` selects which of `ival` / `dval` carries the value; both are
// taken so a caller holding one variant does not have to convert twice and
// lose precision on the way.
//
// Sets `*indicator` to the bytes written for the fixed-size kinds, or to the
// bytes *available* for the character kinds (which is what an application
// needs to size a further call). Returns SQL_SUCCESS_WITH_INFO when a
// character conversion did not fit; the caller posts 01004, because only the
// caller knows which handle to post it on.
SQLRETURN write_numeric_as(SQLSMALLINT c_type,
                           long long ival,
                           double dval,
                           bool is_float,
                           void* target,
                           SQLLEN buffer_bytes,
                           SQLLEN* indicator);

} // namespace mock_odbc
