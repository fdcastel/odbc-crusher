// Unicode (W) entry-point wrappers for the Mock ODBC Driver
//
// Strategy (modelled on psqlodbc):
//   1. Convert SQLWCHAR* input parameters  →  std::string (UTF-8)
//   2. Call the existing ANSI implementation (which uses SQLCHAR*)
//   3. Convert SQLCHAR* output parameters   →  SQLWCHAR* (UTF-16)
//
// Functions that have no string parameters do NOT need a W wrapper —
// the .def file exports them under their ANSI name and the DM calls
// them directly.

#include "odbc/info_types.hpp"
#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "driver/config.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/mock_types.hpp"
#include "mock/mock_data.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"
#include <cstring>
#include <vector>
#include <string>
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

// Forward-declare ANSI entry points we delegate to
namespace {

// D31: six wrappers repeat the same four steps to hand a string back through
// a wide buffer - take the ANSI answer, multiply the caller's buffer size by
// sizeof(SQLWCHAR), copy, then divide the byte count back down to characters.
// The two conversions are the classic wrapper bug: one that forgets either
// reports a length in the wrong unit, and D49 is what that costs when nothing
// tests it. Written once here.
//
// `buffer_chars` and `*out_chars` are in characters, which is the convention
// every caller of this helper uses. SQLGetInfoW is deliberately *not* a
// caller: its lengths are in bytes, and folding the two conventions into one
// helper is how they get confused.
template <typename LenT>
SQLRETURN w_out(const std::string& value,
                SQLWCHAR* buffer,
                LenT buffer_chars,
                LenT* out_chars) {
    SQLSMALLINT written_bytes = 0;
    SQLRETURN ret = SQL_SUCCESS;
    if (buffer && buffer_chars > 0) {
        ret = copy_string_to_wbuffer(
            value, buffer,
            static_cast<SQLSMALLINT>(buffer_chars * sizeof(SQLWCHAR)),
            &written_bytes);
        if (out_chars) {
            *out_chars = static_cast<LenT>(written_bytes / sizeof(SQLWCHAR));
        }
    } else if (out_chars) {
        // No buffer: report what the caller would need.
        *out_chars = static_cast<LenT>(value.length());
    }
    return ret;
}

} // anonymous namespace

extern "C" {
SQLRETURN SQL_API SQLConnect(SQLHDBC, SQLCHAR*, SQLSMALLINT,
                             SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLDriverConnect(SQLHDBC, SQLHWND, SQLCHAR*, SQLSMALLINT,
                                    SQLCHAR*, SQLSMALLINT, SQLSMALLINT*, SQLUSMALLINT);
SQLRETURN SQL_API SQLBrowseConnect(SQLHDBC, SQLCHAR*, SQLSMALLINT,
                                    SQLCHAR*, SQLSMALLINT, SQLSMALLINT*);
SQLRETURN SQL_API SQLExecDirect(SQLHSTMT, SQLCHAR*, SQLINTEGER);
SQLRETURN SQL_API SQLPrepare(SQLHSTMT, SQLCHAR*, SQLINTEGER);
SQLRETURN SQL_API SQLGetInfo(SQLHDBC, SQLUSMALLINT, SQLPOINTER,
                              SQLSMALLINT, SQLSMALLINT*);
SQLRETURN SQL_API SQLGetTypeInfo(SQLHSTMT, SQLSMALLINT);
SQLRETURN SQL_API SQLGetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER,
                                     SQLINTEGER, SQLINTEGER*);
SQLRETURN SQL_API SQLSetConnectAttr(SQLHDBC, SQLINTEGER, SQLPOINTER, SQLINTEGER);
SQLRETURN SQL_API SQLGetStmtAttr(SQLHSTMT, SQLINTEGER, SQLPOINTER,
                                  SQLINTEGER, SQLINTEGER*);
SQLRETURN SQL_API SQLSetStmtAttr(SQLHSTMT, SQLINTEGER, SQLPOINTER, SQLINTEGER);
SQLRETURN SQL_API SQLDescribeCol(SQLHSTMT, SQLUSMALLINT, SQLCHAR*,
                                  SQLSMALLINT, SQLSMALLINT*, SQLSMALLINT*,
                                  SQLULEN*, SQLSMALLINT*, SQLSMALLINT*);
SQLRETURN SQL_API SQLColAttribute(SQLHSTMT, SQLUSMALLINT, SQLUSMALLINT,
                                   SQLPOINTER, SQLSMALLINT, SQLSMALLINT*, SQLLEN*);
SQLRETURN SQL_API SQLSetCursorName(SQLHSTMT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLGetCursorName(SQLHSTMT, SQLCHAR*, SQLSMALLINT, SQLSMALLINT*);
SQLRETURN SQL_API SQLTables(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                             SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                             SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLColumns(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                              SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                              SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLPrimaryKeys(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                  SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLForeignKeys(SQLHSTMT,
                                  SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                  SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                  SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLSpecialColumns(SQLHSTMT, SQLUSMALLINT,
                                     SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                     SQLCHAR*, SQLSMALLINT, SQLUSMALLINT, SQLUSMALLINT);
SQLRETURN SQL_API SQLStatistics(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                 SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                 SQLUSMALLINT, SQLUSMALLINT);
SQLRETURN SQL_API SQLProcedures(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                 SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLProcedureColumns(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                      SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                      SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLTablePrivileges(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                      SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLColumnPrivileges(SQLHSTMT, SQLCHAR*, SQLSMALLINT,
                                       SQLCHAR*, SQLSMALLINT, SQLCHAR*, SQLSMALLINT,
                                       SQLCHAR*, SQLSMALLINT);
SQLRETURN SQL_API SQLNativeSql(SQLHDBC, SQLCHAR*, SQLINTEGER,
                                SQLCHAR*, SQLINTEGER, SQLINTEGER*);
SQLRETURN SQL_API SQLGetDiagRec(SQLSMALLINT, SQLHANDLE, SQLSMALLINT,
                                 SQLCHAR*, SQLINTEGER*, SQLCHAR*,
                                 SQLSMALLINT, SQLSMALLINT*);
SQLRETURN SQL_API SQLGetDiagField(SQLSMALLINT, SQLHANDLE, SQLSMALLINT,
                                   SQLSMALLINT, SQLPOINTER, SQLSMALLINT,
                                   SQLSMALLINT*);
SQLRETURN SQL_API SQLGetDescField(SQLHDESC, SQLSMALLINT, SQLSMALLINT,
                                   SQLPOINTER, SQLINTEGER, SQLINTEGER*);
SQLRETURN SQL_API SQLGetDescRec(SQLHDESC, SQLSMALLINT, SQLCHAR*,
                                 SQLSMALLINT, SQLSMALLINT*, SQLSMALLINT*,
                                 SQLSMALLINT*, SQLLEN*, SQLSMALLINT*,
                                 SQLSMALLINT*, SQLSMALLINT*);
SQLRETURN SQL_API SQLSetDescField(SQLHDESC, SQLSMALLINT, SQLSMALLINT,
                                   SQLPOINTER, SQLINTEGER);
} // extern "C" forward declarations

extern "C" {

// ================================================================
//  Connection W variants
// ================================================================

SQLRETURN SQL_API SQLConnectW(
    SQLHDBC hdbc,
    SQLWCHAR* szDSN,     SQLSMALLINT cbDSN,
    SQLWCHAR* szUID,     SQLSMALLINT cbUID,
    SQLWCHAR* szAuthStr, SQLSMALLINT cbAuthStr)
MOCK_ENTRY_TRY {
    std::string dsn  = sqlw_to_string(szDSN,     cbDSN);
    std::string uid  = sqlw_to_string(szUID,     cbUID);
    std::string auth = sqlw_to_string(szAuthStr, cbAuthStr);

    return SQLConnect(hdbc,
                      (SQLCHAR*)dsn.c_str(),  static_cast<SQLSMALLINT>(dsn.length()),
                      (SQLCHAR*)uid.c_str(),  static_cast<SQLSMALLINT>(uid.length()),
                      (SQLCHAR*)auth.c_str(), static_cast<SQLSMALLINT>(auth.length()));
}
MOCK_ENTRY_CATCH(hdbc)

SQLRETURN SQL_API SQLDriverConnectW(
    SQLHDBC hdbc,
    SQLHWND hwnd,
    SQLWCHAR* szConnStrIn,   SQLSMALLINT cbConnStrIn,
    SQLWCHAR* szConnStrOut,  SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut,
    SQLUSMALLINT fDriverCompletion)
MOCK_ENTRY_TRY {
    std::string connIn = sqlw_to_string(szConnStrIn, cbConnStrIn);

    // Prepare ANSI output buffer
    SQLSMALLINT ansi_out_max = cbConnStrOutMax > 0 ? cbConnStrOutMax : 0;
    std::vector<SQLCHAR> ansi_out(ansi_out_max > 0 ? ansi_out_max : 1, 0);
    SQLSMALLINT ansi_out_len = 0;

    SQLRETURN ret = SQLDriverConnect(hdbc, hwnd,
                                     (SQLCHAR*)connIn.c_str(),
                                     static_cast<SQLSMALLINT>(connIn.length()),
                                     ansi_out.data(), ansi_out_max,
                                     &ansi_out_len, fDriverCompletion);

    if (ret != SQL_ERROR && szConnStrOut && cbConnStrOutMax > 0) {
        std::string out_str(reinterpret_cast<char*>(ansi_out.data()),
                            std::min(static_cast<int>(ansi_out_len),
                                     static_cast<int>(ansi_out_max - 1)));
        SQLSMALLINT wlen = 0;
        copy_string_to_wbuffer(out_str, szConnStrOut,
                               static_cast<SQLSMALLINT>(cbConnStrOutMax * sizeof(SQLWCHAR)),
                               &wlen);
        if (pcbConnStrOut)
            *pcbConnStrOut = static_cast<SQLSMALLINT>(wlen / sizeof(SQLWCHAR));
    } else if (pcbConnStrOut) {
        *pcbConnStrOut = ansi_out_len;
    }

    return ret;
}
MOCK_ENTRY_CATCH(hdbc)

SQLRETURN SQL_API SQLBrowseConnectW(
    SQLHDBC hdbc,
    SQLWCHAR* szConnStrIn,   SQLSMALLINT cbConnStrIn,
    SQLWCHAR* szConnStrOut,  SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut)
MOCK_ENTRY_TRY {
    return SQLDriverConnectW(hdbc, nullptr,
                             szConnStrIn, cbConnStrIn,
                             szConnStrOut, cbConnStrOutMax,
                             pcbConnStrOut, SQL_DRIVER_NOPROMPT);
}
MOCK_ENTRY_CATCH(hdbc)

// ================================================================
//  Connection Attributes — W variants
// ================================================================

SQLRETURN SQL_API SQLGetConnectAttrW(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue)
MOCK_ENTRY_TRY {
    // For string attributes we need to convert the output
    if (fAttribute == SQL_ATTR_CURRENT_CATALOG) {
        // Call ANSI version into temp buffer
        char buf[1024] = {0};
        SQLINTEGER len = 0;
        SQLRETURN ret = SQLGetConnectAttr(hdbc, fAttribute, buf,
                                          sizeof(buf), &len);
        if (ret == SQL_ERROR) return ret;
        std::string val(buf, std::min(static_cast<int>(len),
                                       static_cast<int>(sizeof(buf) - 1)));
        SQLSMALLINT wlen = 0;
        SQLRETURN r2 = copy_string_to_wbuffer(val,
                            static_cast<SQLWCHAR*>(rgbValue),
                            static_cast<SQLINTEGER>(cbValueMax), &wlen);
        if (pcbValue) *pcbValue = wlen;
        return (r2 == SQL_SUCCESS_WITH_INFO) ? SQL_SUCCESS_WITH_INFO : ret;
    }
    // Numeric attributes — pass through
    return SQLGetConnectAttr(hdbc, fAttribute, rgbValue, cbValueMax, pcbValue);
}
MOCK_ENTRY_CATCH(hdbc)

SQLRETURN SQL_API SQLSetConnectAttrW(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
MOCK_ENTRY_TRY {
    // All current mock connection attributes are numeric — pass through
    return SQLSetConnectAttr(hdbc, fAttribute, rgbValue, cbValue);
}
MOCK_ENTRY_CATCH(hdbc)

// ================================================================
//  Statement Execution — W variants
// ================================================================

SQLRETURN SQL_API SQLExecDirectW(
    SQLHSTMT hstmt,
    SQLWCHAR* szSqlStr,
    SQLINTEGER cbSqlStr)
MOCK_ENTRY_TRY {
    std::string sql = sqlw_to_string(szSqlStr, cbSqlStr);
    return SQLExecDirect(hstmt,
                         (SQLCHAR*)sql.c_str(),
                         static_cast<SQLINTEGER>(sql.length()));
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLPrepareW(
    SQLHSTMT hstmt,
    SQLWCHAR* szSqlStr,
    SQLINTEGER cbSqlStr)
MOCK_ENTRY_TRY {
    std::string sql = sqlw_to_string(szSqlStr, cbSqlStr);
    return SQLPrepare(hstmt,
                      (SQLCHAR*)sql.c_str(),
                      static_cast<SQLINTEGER>(sql.length()));
}
MOCK_ENTRY_CATCH(hstmt)

// ================================================================
//  Column Info — W variants
// ================================================================

SQLRETURN SQL_API SQLDescribeColW(
    SQLHSTMT hstmt,
    SQLUSMALLINT icol,
    SQLWCHAR* szColName,
    SQLSMALLINT cbColNameMax,   // in characters
    SQLSMALLINT* pcbColName,    // out: characters (excl NUL)
    SQLSMALLINT* pfSqlType,
    SQLULEN* pcbColDef,
    SQLSMALLINT* pibScale,
    SQLSMALLINT* pfNullable)
MOCK_ENTRY_TRY {
    // Call ANSI version to get name into temp buffer
    SQLCHAR ansi_name[512] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLDescribeCol(hstmt, icol,
                                    ansi_name, sizeof(ansi_name), &ansi_len,
                                    pfSqlType, pcbColDef, pibScale, pfNullable);
    if (ret == SQL_ERROR) return ret;

    const std::string name(reinterpret_cast<char*>(ansi_name), ansi_len);
    if (w_out(name, szColName, cbColNameMax, pcbColName) == SQL_SUCCESS_WITH_INFO) {
        return SQL_SUCCESS_WITH_INFO;
    }
    return ret;
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLColAttributeW(
    SQLHSTMT hstmt,
    SQLUSMALLINT iCol,
    SQLUSMALLINT iField,
    SQLPOINTER pCharAttr,      // SQLWCHAR* output for string fields
    SQLSMALLINT cbCharAttrMax,  // bytes
    SQLSMALLINT* pcbCharAttr,   // bytes
    SQLLEN* pNumAttr)
MOCK_ENTRY_TRY {
    // Determine if this field returns a string
    bool is_string_field = (iField == SQL_DESC_NAME ||
                            iField == SQL_COLUMN_NAME ||
                            iField == SQL_DESC_LABEL ||
                            iField == SQL_DESC_BASE_COLUMN_NAME ||
                            iField == SQL_DESC_BASE_TABLE_NAME ||
                            iField == SQL_DESC_CATALOG_NAME ||
                            iField == SQL_DESC_LITERAL_PREFIX ||
                            iField == SQL_DESC_LITERAL_SUFFIX ||
                            iField == SQL_DESC_LOCAL_TYPE_NAME ||
                            iField == SQL_DESC_SCHEMA_NAME ||
                            iField == SQL_DESC_TABLE_NAME ||
                            iField == SQL_DESC_TYPE_NAME);

    if (is_string_field) {
        SQLCHAR ansi_buf[512] = {0};
        SQLSMALLINT ansi_len = 0;
        SQLRETURN ret = SQLColAttribute(hstmt, iCol, iField,
                                         ansi_buf, sizeof(ansi_buf),
                                         &ansi_len, pNumAttr);
        if (ret == SQL_ERROR) return ret;

        std::string val(reinterpret_cast<char*>(ansi_buf), ansi_len);
        SQLSMALLINT wbytes = 0;
        SQLRETURN r2 = copy_string_to_wbuffer(val,
                            static_cast<SQLWCHAR*>(pCharAttr),
                            cbCharAttrMax, &wbytes);
        if (pcbCharAttr) *pcbCharAttr = wbytes;
        return (r2 == SQL_SUCCESS_WITH_INFO) ? SQL_SUCCESS_WITH_INFO : ret;
    }

    // Numeric field — pass through
    return SQLColAttribute(hstmt, iCol, iField, pCharAttr,
                           cbCharAttrMax, pcbCharAttr, pNumAttr);
}
MOCK_ENTRY_CATCH(hstmt)

// ================================================================
//  Cursor Name — W variants
// ================================================================

SQLRETURN SQL_API SQLSetCursorNameW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCursor,
    SQLSMALLINT cbCursor)
MOCK_ENTRY_TRY {
    std::string name = sqlw_to_string(szCursor, cbCursor);
    return SQLSetCursorName(hstmt,
                            (SQLCHAR*)name.c_str(),
                            static_cast<SQLSMALLINT>(name.length()));
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLGetCursorNameW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCursor,
    SQLSMALLINT cbCursorMax,    // characters
    SQLSMALLINT* pcbCursor)     // characters (excl NUL)
MOCK_ENTRY_TRY {
    SQLCHAR ansi[256] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLGetCursorName(hstmt, ansi, sizeof(ansi), &ansi_len);
    if (ret == SQL_ERROR) return ret;

    const std::string name(reinterpret_cast<char*>(ansi), ansi_len);
    w_out(name, szCursor, cbCursorMax, pcbCursor);
    return ret;
}
MOCK_ENTRY_CATCH(hstmt)

// ================================================================
//  Driver Info — W variants
// ================================================================

SQLRETURN SQL_API SQLGetInfoW(
    SQLHDBC hdbc,
    SQLUSMALLINT fInfoType,
    SQLPOINTER rgbInfoValue,
    SQLSMALLINT cbInfoValueMax,   // bytes
    SQLSMALLINT* pcbInfoValue)    // bytes
MOCK_ENTRY_TRY {
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;

    // D49: this kept its own list of the string info types and the two
    // drifted - nine were missing, so SQL_LIKE_ESCAPE_CLAUSE (and eight
    // others) came back empty through the W path. On Windows that is
    // every path: the driver manager converts an application's ANSI
    // call into a W call before it reaches the driver. One list now,
    // in the translation unit that owns the switch it describes.
    const bool is_string = info_type_is_string(fInfoType);

    // Call ANSI version into a temp buffer
    SQLCHAR ansi_buf[1024] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLGetInfo(hdbc, fInfoType,
                                ansi_buf, sizeof(ansi_buf), &ansi_len);
    if (ret == SQL_ERROR) return ret;

    if (is_string) {
        std::string val(reinterpret_cast<char*>(ansi_buf),
                        std::min(static_cast<int>(ansi_len),
                                 static_cast<int>(sizeof(ansi_buf) - 1)));
        const SQLRETURN str_ret = copy_string_to_wbuffer(val,
                    static_cast<SQLWCHAR*>(rgbInfoValue),
                    static_cast<SQLINTEGER>(cbInfoValueMax),
                    pcbInfoValue);

        // Found by A21 in Phase 2: the ANSI SQLGetInfo call above cleared the
        // connection's diagnostics on entry, and copy_string_to_wbuffer posts
        // nothing, so a truncating SQLGetInfoW returned SQL_SUCCESS_WITH_INFO
        // with an empty diagnostic stack — an application could not tell
        // truncation from any other warning. See D24 for the general case.
        if (str_ret == SQL_SUCCESS_WITH_INFO) {
            conn->add_diagnostic(sqlstate::STRING_TRUNCATED, 0,
                                 "String data, right truncated");
        }

        // D33: BufferValidation=Lenient drops the terminator here too. The
        // .def exports only SQLGetInfoW, so on Windows this is the path the
        // Driver Manager actually takes and the ANSI one is unreachable from
        // an application.
        if (BehaviorController::instance().config().buffer_validation ==
                DriverConfig::BufferValidationMode::Lenient &&
            rgbInfoValue && cbInfoValueMax > 0) {
            const size_t capacity_units =
                static_cast<size_t>(cbInfoValueMax) / sizeof(SQLWCHAR);
            if (capacity_units > 0) {
                const size_t written = std::min(val.size(), capacity_units - 1);
                static_cast<SQLWCHAR*>(rgbInfoValue)[written] =
                    static_cast<SQLWCHAR>('X');
            }
        }
        return str_ret;
    }

    // Numeric — copy raw bytes.
    //
    // D23: this used to be a bare memcpy of `ansi_len` bytes that never
    // consulted `cbInfoValueMax`. `SQLGetInfoW(dbc, SQL_GETDATA_EXTENSIONS,
    // &two_byte_var, 2, &len)` wrote four bytes into a two-byte variable,
    // and since the .def exports only SQLGetInfoW this is the path every
    // Unicode driver manager takes on Windows - so the overrun was on the
    // reachable path, not the dead one.
    //
    // A caller that supplies too small a buffer for a fixed-size numeric
    // attribute has made a mistake, and the spec's answer is HY090 rather
    // than a partial write: half of a SQLUINTEGER is not a smaller number,
    // it is a different one.
    if (rgbInfoValue && ansi_len > 0) {
        if (cbInfoValueMax > 0 &&
            static_cast<SQLINTEGER>(ansi_len) > cbInfoValueMax) {
            conn->add_diagnostic(sqlstate::INVALID_STRING_OR_BUFFER_LENGTH, 0,
                                 "Buffer length " +
                                 std::to_string(cbInfoValueMax) +
                                 " is too small for this information type, "
                                 "which needs " + std::to_string(ansi_len));
            return SQL_ERROR;
        }
        std::memcpy(rgbInfoValue, ansi_buf, ansi_len);
    }
    if (pcbInfoValue) *pcbInfoValue = ansi_len;
    return ret;
}
MOCK_ENTRY_CATCH(hdbc)

SQLRETURN SQL_API SQLGetTypeInfoW(
    SQLHSTMT hstmt,
    SQLSMALLINT fSqlType)
MOCK_ENTRY_TRY {
    // No string parameters — just forward
    return SQLGetTypeInfo(hstmt, fSqlType);
}
MOCK_ENTRY_CATCH(hstmt)

// ================================================================
//  Statement Attributes — W variants
// ================================================================

SQLRETURN SQL_API SQLGetStmtAttrW(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue)
MOCK_ENTRY_TRY {
    // All mock stmt attributes are numeric — pass through
    return SQLGetStmtAttr(hstmt, fAttribute, rgbValue, cbValueMax, pcbValue);
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSetStmtAttrW(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
MOCK_ENTRY_TRY {
    return SQLSetStmtAttr(hstmt, fAttribute, rgbValue, cbValue);
}
MOCK_ENTRY_CATCH(hstmt)

// D31: every catalog W wrapper converted its wide arguments and then repeated
// the same ternary once per argument - 37 copies across this file of
// `szX ? (SQLCHAR*)x.c_str() : nullptr, static_cast<SQLSMALLINT>(x.length())`.
// That repetition is exactly where D25's null-versus-empty distinction was
// destroyed, and fixing it meant editing all 37. The rule lives here now:
// a null pointer stays null, and an empty string stays an empty string.
namespace {

class WArg {
public:
    WArg(SQLWCHAR* wide, SQLSMALLINT length)
        : present_(wide != nullptr),
          text_(wide ? sqlw_to_string(wide, length) : std::string()) {}

    SQLCHAR* ptr() {
        return present_ ? reinterpret_cast<SQLCHAR*>(
                              const_cast<char*>(text_.c_str()))
                        : nullptr;
    }
    SQLSMALLINT len() const { return static_cast<SQLSMALLINT>(text_.length()); }

private:
    bool present_;
    std::string text_;
};

} // anonymous namespace

// ================================================================
//  Catalog Functions — W variants
// ================================================================

SQLRETURN SQL_API SQLTablesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szTableType,    SQLSMALLINT cbTableType)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);
    WArg typ(szTableType, cbTableType);

    return SQLTables(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len(),
        typ.ptr(), typ.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLColumnsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);
    WArg col(szColumnName, cbColumnName);

    return SQLColumns(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len(),
        col.ptr(), col.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLPrimaryKeysW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);

    return SQLPrimaryKeys(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLForeignKeysW(
    SQLHSTMT hstmt,
    SQLWCHAR* szPkCatalogName, SQLSMALLINT cbPkCatalogName,
    SQLWCHAR* szPkSchemaName,  SQLSMALLINT cbPkSchemaName,
    SQLWCHAR* szPkTableName,   SQLSMALLINT cbPkTableName,
    SQLWCHAR* szFkCatalogName, SQLSMALLINT cbFkCatalogName,
    SQLWCHAR* szFkSchemaName,  SQLSMALLINT cbFkSchemaName,
    SQLWCHAR* szFkTableName,   SQLSMALLINT cbFkTableName)
MOCK_ENTRY_TRY {
    WArg pkCat(szPkCatalogName, cbPkCatalogName);
    WArg pkSch(szPkSchemaName, cbPkSchemaName);
    WArg pkTab(szPkTableName, cbPkTableName);
    WArg fkCat(szFkCatalogName, cbFkCatalogName);
    WArg fkSch(szFkSchemaName, cbFkSchemaName);
    WArg fkTab(szFkTableName, cbFkTableName);

    return SQLForeignKeys(hstmt,
        pkCat.ptr(), pkCat.len(),
        pkSch.ptr(), pkSch.len(),
        pkTab.ptr(), pkTab.len(),
        fkCat.ptr(), fkCat.len(),
        fkSch.ptr(), fkSch.len(),
        fkTab.ptr(), fkTab.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLSpecialColumnsW(
    SQLHSTMT hstmt,
    SQLUSMALLINT fColType,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLUSMALLINT fScope,
    SQLUSMALLINT fNullable)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);

    return SQLSpecialColumns(hstmt, fColType,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len(),
        fScope, fNullable);
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLStatisticsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLUSMALLINT fUnique,
    SQLUSMALLINT fAccuracy)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);

    return SQLStatistics(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len(),
        fUnique, fAccuracy);
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLProceduresW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szProcName,     SQLSMALLINT cbProcName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg prc(szProcName, cbProcName);

    return SQLProcedures(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        prc.ptr(), prc.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLProcedureColumnsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szProcName,     SQLSMALLINT cbProcName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg prc(szProcName, cbProcName);
    WArg col(szColumnName, cbColumnName);

    return SQLProcedureColumns(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        prc.ptr(), prc.len(),
        col.ptr(), col.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLTablePrivilegesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);

    return SQLTablePrivileges(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len());
}
MOCK_ENTRY_CATCH(hstmt)

SQLRETURN SQL_API SQLColumnPrivilegesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
MOCK_ENTRY_TRY {
    WArg cat(szCatalogName, cbCatalogName);
    WArg sch(szSchemaName, cbSchemaName);
    WArg tab(szTableName, cbTableName);
    WArg col(szColumnName, cbColumnName);

    return SQLColumnPrivileges(hstmt,
        cat.ptr(), cat.len(),
        sch.ptr(), sch.len(),
        tab.ptr(), tab.len(),
        col.ptr(), col.len());
}
MOCK_ENTRY_CATCH(hstmt)

// ================================================================
//  NativeSql — W variant
// ================================================================

SQLRETURN SQL_API SQLNativeSqlW(
    SQLHDBC hdbc,
    SQLWCHAR* szSqlStrIn,    SQLINTEGER cbSqlStrIn,
    SQLWCHAR* szSqlStr,      SQLINTEGER cbSqlStrMax,
    SQLINTEGER* pcbSqlStr)
MOCK_ENTRY_TRY {
    std::string sqlIn = sqlw_to_string(szSqlStrIn, cbSqlStrIn);

    SQLCHAR ansi_out[4096] = {0};
    SQLINTEGER ansi_len = 0;
    SQLRETURN ret = SQLNativeSql(hdbc,
                                 (SQLCHAR*)sqlIn.c_str(),
                                 static_cast<SQLINTEGER>(sqlIn.length()),
                                 ansi_out,
                                 static_cast<SQLINTEGER>(sizeof(ansi_out)),
                                 &ansi_len);
    if (ret == SQL_ERROR) return ret;

    std::string out(reinterpret_cast<char*>(ansi_out),
                    std::min(static_cast<int>(ansi_len),
                             static_cast<int>(sizeof(ansi_out) - 1)));

    if (szSqlStr && cbSqlStrMax > 0) {
        SQLSMALLINT wbytes = 0;
        copy_string_to_wbuffer(out, szSqlStr,
                               static_cast<SQLINTEGER>(cbSqlStrMax * sizeof(SQLWCHAR)),
                               &wbytes);
        if (pcbSqlStr) *pcbSqlStr = wbytes / static_cast<SQLINTEGER>(sizeof(SQLWCHAR));
    } else if (pcbSqlStr) {
        *pcbSqlStr = static_cast<SQLINTEGER>(out.length());
    }

    return ret;
}
MOCK_ENTRY_CATCH(hdbc)

// ================================================================
//  Diagnostics — W variants
// ================================================================

SQLRETURN SQL_API SQLGetDiagRecW(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT iRecord,
    SQLWCHAR* szSqlState,
    SQLINTEGER* pfNativeError,
    SQLWCHAR* szErrorMsg,
    SQLSMALLINT cbErrorMsgMax,   // characters
    SQLSMALLINT* pcbErrorMsg)    // characters (excl NUL)
MOCK_ENTRY_TRY {
    SQLCHAR ansi_state[6] = {0};
    SQLCHAR ansi_msg[2048] = {0};
    SQLSMALLINT ansi_msg_len = 0;

    SQLRETURN ret = SQLGetDiagRec(fHandleType, hHandle, iRecord,
                                   ansi_state, pfNativeError,
                                   ansi_msg, sizeof(ansi_msg), &ansi_msg_len);
    if (ret == SQL_ERROR || ret == SQL_INVALID_HANDLE || ret == SQL_NO_DATA)
        return ret;

    // Convert SQLSTATE
    if (szSqlState) {
        for (int i = 0; i < 5; ++i)
            szSqlState[i] = static_cast<SQLWCHAR>(ansi_state[i]);
        szSqlState[5] = 0;
    }

    // Convert message
    {
        const std::string msg(reinterpret_cast<char*>(ansi_msg), ansi_msg_len);
        if (w_out(msg, szErrorMsg, cbErrorMsgMax, pcbErrorMsg)
                == SQL_SUCCESS_WITH_INFO) {
            return SQL_SUCCESS_WITH_INFO;
        }
    }

    return ret;
}
MOCK_ENTRY_CATCH(hHandle)

SQLRETURN SQL_API SQLGetDiagFieldW(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT iRecord,
    SQLSMALLINT fDiagField,
    SQLPOINTER rgbDiagInfo,
    SQLSMALLINT cbDiagInfoMax,   // bytes
    SQLSMALLINT* pcbDiagInfo)    // bytes
MOCK_ENTRY_TRY {
    // String diagnostic fields need conversion
    bool is_string_field = (fDiagField == SQL_DIAG_SQLSTATE ||
                            fDiagField == SQL_DIAG_MESSAGE_TEXT ||
                            fDiagField == SQL_DIAG_CLASS_ORIGIN ||
                            fDiagField == SQL_DIAG_SUBCLASS_ORIGIN ||
                            fDiagField == SQL_DIAG_CONNECTION_NAME ||
                            fDiagField == SQL_DIAG_SERVER_NAME ||
                            fDiagField == SQL_DIAG_DYNAMIC_FUNCTION);

    if (is_string_field) {
        SQLCHAR ansi_buf[2048] = {0};
        SQLSMALLINT ansi_len = 0;
        SQLRETURN ret = SQLGetDiagField(fHandleType, hHandle, iRecord,
                                         fDiagField, ansi_buf,
                                         sizeof(ansi_buf), &ansi_len);
        if (ret == SQL_ERROR || ret == SQL_INVALID_HANDLE || ret == SQL_NO_DATA)
            return ret;

        std::string val(reinterpret_cast<char*>(ansi_buf), ansi_len);
        SQLSMALLINT wbytes = 0;
        SQLRETURN r2 = copy_string_to_wbuffer(val,
                            static_cast<SQLWCHAR*>(rgbDiagInfo),
                            cbDiagInfoMax, &wbytes);
        if (pcbDiagInfo) *pcbDiagInfo = wbytes;
        return (r2 == SQL_SUCCESS_WITH_INFO) ? SQL_SUCCESS_WITH_INFO : ret;
    }

    // Numeric fields — pass through
    return SQLGetDiagField(fHandleType, hHandle, iRecord,
                           fDiagField, rgbDiagInfo,
                           cbDiagInfoMax, pcbDiagInfo);
}
MOCK_ENTRY_CATCH(hHandle)

// ================================================================
//  Descriptor — W variants
// ================================================================

SQLRETURN SQL_API SQLGetDescFieldW(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue)
MOCK_ENTRY_TRY {
    // The mock driver descriptor fields are all numeric — pass through
    return SQLGetDescField(hdesc, iRecord, iField, rgbValue,
                           cbValueMax, pcbValue);
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLGetDescRecW(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLWCHAR* szName,
    SQLSMALLINT cbNameMax,     // characters
    SQLSMALLINT* pcbName,      // characters
    SQLSMALLINT* pfType,
    SQLSMALLINT* pfSubType,
    SQLLEN* pLength,
    SQLSMALLINT* pPrecision,
    SQLSMALLINT* pScale,
    SQLSMALLINT* pNullable)
MOCK_ENTRY_TRY {
    SQLCHAR ansi_name[512] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLGetDescRec(hdesc, iRecord,
                                   ansi_name, sizeof(ansi_name), &ansi_len,
                                   pfType, pfSubType, pLength,
                                   pPrecision, pScale, pNullable);
    if (ret == SQL_ERROR || ret == SQL_NO_DATA) return ret;

    const std::string name(reinterpret_cast<char*>(ansi_name), ansi_len);
    w_out(name, szName, cbNameMax, pcbName);
    return ret;
}
MOCK_ENTRY_CATCH(hdesc)

SQLRETURN SQL_API SQLSetDescFieldW(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
MOCK_ENTRY_TRY {
    // Mock descriptor fields are all numeric — pass through
    return SQLSetDescField(hdesc, iRecord, iField, rgbValue, cbValue);
}
MOCK_ENTRY_CATCH(hdesc)

} // extern "C"
