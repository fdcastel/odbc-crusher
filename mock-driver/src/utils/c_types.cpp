#include "c_types.hpp"

#include "buffer_copy.hpp"

#include <cmath>
#include <cstring>
#include <string>

namespace mock_odbc {

CTypeInfo c_type_info(SQLSMALLINT c_type) {
    switch (c_type) {
        case SQL_C_SLONG:
        case SQL_C_LONG:
            return {CTypeKind::Integer, sizeof(SQLINTEGER), true};
        case SQL_C_ULONG:
            return {CTypeKind::Integer, sizeof(SQLUINTEGER), false};
        case SQL_C_SBIGINT:
            return {CTypeKind::Integer, sizeof(SQLBIGINT), true};
        case SQL_C_UBIGINT:
            return {CTypeKind::Integer, sizeof(SQLUBIGINT), false};
        case SQL_C_SSHORT:
        case SQL_C_SHORT:
            return {CTypeKind::Integer, sizeof(SQLSMALLINT), true};
        case SQL_C_USHORT:
            return {CTypeKind::Integer, sizeof(SQLUSMALLINT), false};
        case SQL_C_STINYINT:
            return {CTypeKind::Integer, sizeof(SQLSCHAR), true};
        case SQL_C_UTINYINT:
        case SQL_C_BIT:
            return {CTypeKind::Integer, sizeof(SQLCHAR), false};
        case SQL_C_DOUBLE:
            return {CTypeKind::Float, sizeof(SQLDOUBLE), true};
        case SQL_C_FLOAT:
            return {CTypeKind::Float, sizeof(SQLREAL), true};
        case SQL_C_NUMERIC:
            return {CTypeKind::Numeric, sizeof(SQL_NUMERIC_STRUCT), true};
        case SQL_C_TYPE_DATE:
            return {CTypeKind::Date, sizeof(DATE_STRUCT), true};
        case SQL_C_TYPE_TIME:
            return {CTypeKind::Time, sizeof(TIME_STRUCT), true};
        case SQL_C_TYPE_TIMESTAMP:
            return {CTypeKind::Timestamp, sizeof(TIMESTAMP_STRUCT), true};
        case SQL_C_CHAR:
            return {CTypeKind::Char, 0, true};
        case SQL_C_WCHAR:
            return {CTypeKind::WChar, 0, true};
        case SQL_C_BINARY:
            return {CTypeKind::Binary, 0, true};
        default:
            return {CTypeKind::Unknown, 0, true};
    }
}

SQLLEN c_type_element_size(SQLSMALLINT c_type, SQLLEN buffer_length) {
    const CTypeInfo info = c_type_info(c_type);
    if (info.is_fixed_size()) return static_cast<SQLLEN>(info.size);
    // Char, WChar, Binary and Unknown all stride by the caller's buffer.
    return buffer_length > 0 ? buffer_length : 1;
}

namespace {

// Write an integral value into a fixed-width slot. Truncation of the *value*
// (42 into an SQLSCHAR) is the application's choice of binding, not a driver
// error, and the spec has no diagnostic for it here.
template <typename T>
void store(void* target, long long v) {
    T tmp = static_cast<T>(v);
    std::memcpy(target, &tmp, sizeof(T));
}

template <typename T>
void store_f(void* target, double v) {
    T tmp = static_cast<T>(v);
    std::memcpy(target, &tmp, sizeof(T));
}

// Render a numeric value the way SQL_C_CHAR expects: integers without a
// decimal point, floats through std::to_string, matching what the mock's
// hand-rolled branches produced so no existing expectation moves.
std::string render(long long ival, double dval, bool is_float) {
    return is_float ? std::to_string(dval) : std::to_string(ival);
}

} // namespace

SQLRETURN write_numeric_as(SQLSMALLINT c_type,
                           long long ival,
                           double dval,
                           bool is_float,
                           void* target,
                           SQLLEN buffer_bytes,
                           SQLLEN* indicator) {
    const CTypeInfo info = c_type_info(c_type);

    // A fixed-size target the caller sized too small is a real error: half of
    // an SQLINTEGER is a different number, not a smaller one. `buffer_bytes`
    // is routinely 0 for fixed-size bindings, which is the idiomatic ODBC way
    // to say "the type's own size", so only a *positive* length that is too
    // small counts.
    if (info.is_fixed_size() && buffer_bytes > 0 &&
        static_cast<size_t>(buffer_bytes) < info.size) {
        return SQL_ERROR;
    }

    switch (info.kind) {
        case CTypeKind::Integer: {
            if (target) {
                const long long v = is_float
                    ? static_cast<long long>(std::llround(dval)) : ival;
                switch (info.size) {
                    case 1: info.is_signed ? store<SQLSCHAR>(target, v)
                                           : store<SQLCHAR>(target, v); break;
                    case 2: info.is_signed ? store<SQLSMALLINT>(target, v)
                                           : store<SQLUSMALLINT>(target, v); break;
                    case 4: info.is_signed ? store<SQLINTEGER>(target, v)
                                           : store<SQLUINTEGER>(target, v); break;
                    default: info.is_signed ? store<SQLBIGINT>(target, v)
                                            : store<SQLUBIGINT>(target, v); break;
                }
            }
            if (indicator) *indicator = static_cast<SQLLEN>(info.size);
            return SQL_SUCCESS;
        }

        case CTypeKind::Float: {
            if (target) {
                const double v = is_float ? dval : static_cast<double>(ival);
                if (info.size == sizeof(SQLREAL)) store_f<SQLREAL>(target, v);
                else                              store_f<SQLDOUBLE>(target, v);
            }
            if (indicator) *indicator = static_cast<SQLLEN>(info.size);
            return SQL_SUCCESS;
        }

        case CTypeKind::Char: {
            const std::string s = render(ival, dval, is_float);
            const BufferCopyResult res = copy_chars(s, 0, target, buffer_bytes);
            if (indicator) *indicator = res.remaining;
            return res.rc;
        }

        case CTypeKind::WChar: {
            const std::string s = render(ival, dval, is_float);
            const BufferCopyResult res = copy_wchars(s, 0, target, buffer_bytes);
            if (indicator) *indicator = res.remaining;
            return res.rc;
        }

        default:
            // Numeric, the datetime structs, Binary and Unknown are not
            // numeric-delivery targets: converting an integer cell into a
            // DATE_STRUCT has no meaning the spec defines. The caller decides
            // what to do, which for the mock is 07006.
            return SQL_ERROR;
    }
}

} // namespace mock_odbc
