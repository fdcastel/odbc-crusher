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

using namespace mock_odbc;

// Forward-declare ANSI entry points we delegate to
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
{
    std::string dsn  = sqlw_to_string(szDSN,     cbDSN);
    std::string uid  = sqlw_to_string(szUID,     cbUID);
    std::string auth = sqlw_to_string(szAuthStr, cbAuthStr);

    return SQLConnect(hdbc,
                      (SQLCHAR*)dsn.c_str(),  static_cast<SQLSMALLINT>(dsn.length()),
                      (SQLCHAR*)uid.c_str(),  static_cast<SQLSMALLINT>(uid.length()),
                      (SQLCHAR*)auth.c_str(), static_cast<SQLSMALLINT>(auth.length()));
}

SQLRETURN SQL_API SQLDriverConnectW(
    SQLHDBC hdbc,
    SQLHWND hwnd,
    SQLWCHAR* szConnStrIn,   SQLSMALLINT cbConnStrIn,
    SQLWCHAR* szConnStrOut,  SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut,
    SQLUSMALLINT fDriverCompletion)
{
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

SQLRETURN SQL_API SQLBrowseConnectW(
    SQLHDBC hdbc,
    SQLWCHAR* szConnStrIn,   SQLSMALLINT cbConnStrIn,
    SQLWCHAR* szConnStrOut,  SQLSMALLINT cbConnStrOutMax,
    SQLSMALLINT* pcbConnStrOut)
{
    return SQLDriverConnectW(hdbc, nullptr,
                             szConnStrIn, cbConnStrIn,
                             szConnStrOut, cbConnStrOutMax,
                             pcbConnStrOut, SQL_DRIVER_NOPROMPT);
}

// ================================================================
//  Connection Attributes — W variants
// ================================================================

SQLRETURN SQL_API SQLGetConnectAttrW(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue)
{
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

SQLRETURN SQL_API SQLSetConnectAttrW(
    SQLHDBC hdbc,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
{
    // All current mock connection attributes are numeric — pass through
    return SQLSetConnectAttr(hdbc, fAttribute, rgbValue, cbValue);
}

// ================================================================
//  Statement Execution — W variants
// ================================================================

SQLRETURN SQL_API SQLExecDirectW(
    SQLHSTMT hstmt,
    SQLWCHAR* szSqlStr,
    SQLINTEGER cbSqlStr)
{
    std::string sql = sqlw_to_string(szSqlStr, cbSqlStr);
    return SQLExecDirect(hstmt,
                         (SQLCHAR*)sql.c_str(),
                         static_cast<SQLINTEGER>(sql.length()));
}

SQLRETURN SQL_API SQLPrepareW(
    SQLHSTMT hstmt,
    SQLWCHAR* szSqlStr,
    SQLINTEGER cbSqlStr)
{
    std::string sql = sqlw_to_string(szSqlStr, cbSqlStr);
    return SQLPrepare(hstmt,
                      (SQLCHAR*)sql.c_str(),
                      static_cast<SQLINTEGER>(sql.length()));
}

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
{
    // Call ANSI version to get name into temp buffer
    SQLCHAR ansi_name[512] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLDescribeCol(hstmt, icol,
                                    ansi_name, sizeof(ansi_name), &ansi_len,
                                    pfSqlType, pcbColDef, pibScale, pfNullable);
    if (ret == SQL_ERROR) return ret;

    if (szColName && cbColNameMax > 0) {
        std::string name(reinterpret_cast<char*>(ansi_name), ansi_len);
        SQLSMALLINT wbytes = 0;
        SQLRETURN r2 = copy_string_to_wbuffer(name, szColName,
                            static_cast<SQLSMALLINT>(cbColNameMax * sizeof(SQLWCHAR)),
                            &wbytes);
        if (pcbColName)
            *pcbColName = static_cast<SQLSMALLINT>(wbytes / sizeof(SQLWCHAR));
        if (r2 == SQL_SUCCESS_WITH_INFO)
            return SQL_SUCCESS_WITH_INFO;
    } else if (pcbColName) {
        *pcbColName = ansi_len; // character count
    }

    return ret;
}

SQLRETURN SQL_API SQLColAttributeW(
    SQLHSTMT hstmt,
    SQLUSMALLINT iCol,
    SQLUSMALLINT iField,
    SQLPOINTER pCharAttr,      // SQLWCHAR* output for string fields
    SQLSMALLINT cbCharAttrMax,  // bytes
    SQLSMALLINT* pcbCharAttr,   // bytes
    SQLLEN* pNumAttr)
{
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

// ================================================================
//  Cursor Name — W variants
// ================================================================

SQLRETURN SQL_API SQLSetCursorNameW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCursor,
    SQLSMALLINT cbCursor)
{
    std::string name = sqlw_to_string(szCursor, cbCursor);
    return SQLSetCursorName(hstmt,
                            (SQLCHAR*)name.c_str(),
                            static_cast<SQLSMALLINT>(name.length()));
}

SQLRETURN SQL_API SQLGetCursorNameW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCursor,
    SQLSMALLINT cbCursorMax,    // characters
    SQLSMALLINT* pcbCursor)     // characters (excl NUL)
{
    SQLCHAR ansi[256] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLGetCursorName(hstmt, ansi, sizeof(ansi), &ansi_len);
    if (ret == SQL_ERROR) return ret;

    if (szCursor && cbCursorMax > 0) {
        std::string name(reinterpret_cast<char*>(ansi), ansi_len);
        SQLSMALLINT wbytes = 0;
        copy_string_to_wbuffer(name, szCursor,
                               static_cast<SQLSMALLINT>(cbCursorMax * sizeof(SQLWCHAR)),
                               &wbytes);
        if (pcbCursor) *pcbCursor = static_cast<SQLSMALLINT>(wbytes / sizeof(SQLWCHAR));
    } else if (pcbCursor) {
        *pcbCursor = ansi_len;
    }
    return ret;
}

// ================================================================
//  Driver Info — W variants
// ================================================================

SQLRETURN SQL_API SQLGetInfoW(
    SQLHDBC hdbc,
    SQLUSMALLINT fInfoType,
    SQLPOINTER rgbInfoValue,
    SQLSMALLINT cbInfoValueMax,   // bytes
    SQLSMALLINT* pcbInfoValue)    // bytes
{
    auto* conn = validate_dbc_handle(hdbc);
    if (!conn) return SQL_INVALID_HANDLE;

    // Determine if this info type returns a string value.
    // Use an explicit list — the heuristic based on length was fragile.
    bool is_string = false;
    switch (fInfoType) {
        case SQL_DRIVER_NAME:
        case SQL_DRIVER_VER:
        case SQL_DRIVER_ODBC_VER:
        case SQL_ODBC_VER:
        case SQL_DBMS_NAME:
        case SQL_DBMS_VER:
        case SQL_SERVER_NAME:
        case SQL_DATA_SOURCE_NAME:
        case SQL_DATA_SOURCE_READ_ONLY:
        case SQL_DATABASE_NAME:
        case SQL_USER_NAME:
        case SQL_IDENTIFIER_QUOTE_CHAR:
        case SQL_CATALOG_NAME:
        case SQL_CATALOG_NAME_SEPARATOR:
        case SQL_CATALOG_TERM:
        case SQL_SCHEMA_TERM:
        case SQL_TABLE_TERM:
        case SQL_PROCEDURE_TERM:
        case SQL_DESCRIBE_PARAMETER:
        case SQL_MULT_RESULT_SETS:
        case SQL_MULTIPLE_ACTIVE_TXN:
        case SQL_NEED_LONG_DATA_LEN:
        case SQL_OUTER_JOINS:
        case SQL_ORDER_BY_COLUMNS_IN_SELECT:
        case SQL_PROCEDURES:
        case SQL_ROW_UPDATES:
        case SQL_SEARCH_PATTERN_ESCAPE:
        case SQL_SPECIAL_CHARACTERS:
            is_string = true;
            break;
        default:
            is_string = false;
            break;
    }

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
        return copy_string_to_wbuffer(val,
                    static_cast<SQLWCHAR*>(rgbInfoValue),
                    static_cast<SQLINTEGER>(cbInfoValueMax),
                    pcbInfoValue);
    }

    // Numeric — copy raw bytes
    if (rgbInfoValue && ansi_len > 0) {
        std::memcpy(rgbInfoValue, ansi_buf, ansi_len);
    }
    if (pcbInfoValue) *pcbInfoValue = ansi_len;
    return ret;
}

SQLRETURN SQL_API SQLGetTypeInfoW(
    SQLHSTMT hstmt,
    SQLSMALLINT fSqlType)
{
    // No string parameters — just forward
    return SQLGetTypeInfo(hstmt, fSqlType);
}

// ================================================================
//  Statement Attributes — W variants
// ================================================================

SQLRETURN SQL_API SQLGetStmtAttrW(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValueMax,
    SQLINTEGER* pcbValue)
{
    // All mock stmt attributes are numeric — pass through
    return SQLGetStmtAttr(hstmt, fAttribute, rgbValue, cbValueMax, pcbValue);
}

SQLRETURN SQL_API SQLSetStmtAttrW(
    SQLHSTMT hstmt,
    SQLINTEGER fAttribute,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
{
    return SQLSetStmtAttr(hstmt, fAttribute, rgbValue, cbValue);
}

// ================================================================
//  Catalog Functions — W variants
// ================================================================

SQLRETURN SQL_API SQLTablesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szTableType,    SQLSMALLINT cbTableType)
{
    std::string cat  = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch  = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab  = sqlw_to_string(szTableName,   cbTableName);
    std::string typ  = sqlw_to_string(szTableType,   cbTableType);

    return SQLTables(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()),
        typ.empty() ? nullptr : (SQLCHAR*)typ.c_str(), static_cast<SQLSMALLINT>(typ.length()));
}

SQLRETURN SQL_API SQLColumnsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);
    std::string col = sqlw_to_string(szColumnName,  cbColumnName);

    return SQLColumns(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()),
        col.empty() ? nullptr : (SQLCHAR*)col.c_str(), static_cast<SQLSMALLINT>(col.length()));
}

SQLRETURN SQL_API SQLPrimaryKeysW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);

    return SQLPrimaryKeys(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()));
}

SQLRETURN SQL_API SQLForeignKeysW(
    SQLHSTMT hstmt,
    SQLWCHAR* szPkCatalogName, SQLSMALLINT cbPkCatalogName,
    SQLWCHAR* szPkSchemaName,  SQLSMALLINT cbPkSchemaName,
    SQLWCHAR* szPkTableName,   SQLSMALLINT cbPkTableName,
    SQLWCHAR* szFkCatalogName, SQLSMALLINT cbFkCatalogName,
    SQLWCHAR* szFkSchemaName,  SQLSMALLINT cbFkSchemaName,
    SQLWCHAR* szFkTableName,   SQLSMALLINT cbFkTableName)
{
    std::string pkCat = sqlw_to_string(szPkCatalogName, cbPkCatalogName);
    std::string pkSch = sqlw_to_string(szPkSchemaName,  cbPkSchemaName);
    std::string pkTab = sqlw_to_string(szPkTableName,   cbPkTableName);
    std::string fkCat = sqlw_to_string(szFkCatalogName, cbFkCatalogName);
    std::string fkSch = sqlw_to_string(szFkSchemaName,  cbFkSchemaName);
    std::string fkTab = sqlw_to_string(szFkTableName,   cbFkTableName);

    return SQLForeignKeys(hstmt,
        pkCat.empty() ? nullptr : (SQLCHAR*)pkCat.c_str(), static_cast<SQLSMALLINT>(pkCat.length()),
        pkSch.empty() ? nullptr : (SQLCHAR*)pkSch.c_str(), static_cast<SQLSMALLINT>(pkSch.length()),
        pkTab.empty() ? nullptr : (SQLCHAR*)pkTab.c_str(), static_cast<SQLSMALLINT>(pkTab.length()),
        fkCat.empty() ? nullptr : (SQLCHAR*)fkCat.c_str(), static_cast<SQLSMALLINT>(fkCat.length()),
        fkSch.empty() ? nullptr : (SQLCHAR*)fkSch.c_str(), static_cast<SQLSMALLINT>(fkSch.length()),
        fkTab.empty() ? nullptr : (SQLCHAR*)fkTab.c_str(), static_cast<SQLSMALLINT>(fkTab.length()));
}

SQLRETURN SQL_API SQLSpecialColumnsW(
    SQLHSTMT hstmt,
    SQLUSMALLINT fColType,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLUSMALLINT fScope,
    SQLUSMALLINT fNullable)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);

    return SQLSpecialColumns(hstmt, fColType,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()),
        fScope, fNullable);
}

SQLRETURN SQL_API SQLStatisticsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLUSMALLINT fUnique,
    SQLUSMALLINT fAccuracy)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);

    return SQLStatistics(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()),
        fUnique, fAccuracy);
}

SQLRETURN SQL_API SQLProceduresW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szProcName,     SQLSMALLINT cbProcName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string prc = sqlw_to_string(szProcName,    cbProcName);

    return SQLProcedures(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        prc.empty() ? nullptr : (SQLCHAR*)prc.c_str(), static_cast<SQLSMALLINT>(prc.length()));
}

SQLRETURN SQL_API SQLProcedureColumnsW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szProcName,     SQLSMALLINT cbProcName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string prc = sqlw_to_string(szProcName,    cbProcName);
    std::string col = sqlw_to_string(szColumnName,  cbColumnName);

    return SQLProcedureColumns(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        prc.empty() ? nullptr : (SQLCHAR*)prc.c_str(), static_cast<SQLSMALLINT>(prc.length()),
        col.empty() ? nullptr : (SQLCHAR*)col.c_str(), static_cast<SQLSMALLINT>(col.length()));
}

SQLRETURN SQL_API SQLTablePrivilegesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);

    return SQLTablePrivileges(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()));
}

SQLRETURN SQL_API SQLColumnPrivilegesW(
    SQLHSTMT hstmt,
    SQLWCHAR* szCatalogName,  SQLSMALLINT cbCatalogName,
    SQLWCHAR* szSchemaName,   SQLSMALLINT cbSchemaName,
    SQLWCHAR* szTableName,    SQLSMALLINT cbTableName,
    SQLWCHAR* szColumnName,   SQLSMALLINT cbColumnName)
{
    std::string cat = sqlw_to_string(szCatalogName, cbCatalogName);
    std::string sch = sqlw_to_string(szSchemaName,  cbSchemaName);
    std::string tab = sqlw_to_string(szTableName,   cbTableName);
    std::string col = sqlw_to_string(szColumnName,  cbColumnName);

    return SQLColumnPrivileges(hstmt,
        cat.empty() ? nullptr : (SQLCHAR*)cat.c_str(), static_cast<SQLSMALLINT>(cat.length()),
        sch.empty() ? nullptr : (SQLCHAR*)sch.c_str(), static_cast<SQLSMALLINT>(sch.length()),
        tab.empty() ? nullptr : (SQLCHAR*)tab.c_str(), static_cast<SQLSMALLINT>(tab.length()),
        col.empty() ? nullptr : (SQLCHAR*)col.c_str(), static_cast<SQLSMALLINT>(col.length()));
}

// ================================================================
//  NativeSql — W variant
// ================================================================

SQLRETURN SQL_API SQLNativeSqlW(
    SQLHDBC hdbc,
    SQLWCHAR* szSqlStrIn,    SQLINTEGER cbSqlStrIn,
    SQLWCHAR* szSqlStr,      SQLINTEGER cbSqlStrMax,
    SQLINTEGER* pcbSqlStr)
{
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
{
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
    if (szErrorMsg && cbErrorMsgMax > 0) {
        std::string msg(reinterpret_cast<char*>(ansi_msg), ansi_msg_len);
        SQLSMALLINT wbytes = 0;
        SQLRETURN r2 = copy_string_to_wbuffer(msg, szErrorMsg,
                            static_cast<SQLSMALLINT>(cbErrorMsgMax * sizeof(SQLWCHAR)),
                            &wbytes);
        if (pcbErrorMsg)
            *pcbErrorMsg = static_cast<SQLSMALLINT>(wbytes / sizeof(SQLWCHAR));
        if (r2 == SQL_SUCCESS_WITH_INFO)
            return SQL_SUCCESS_WITH_INFO;
    } else if (pcbErrorMsg) {
        *pcbErrorMsg = ansi_msg_len;
    }

    return ret;
}

SQLRETURN SQL_API SQLGetDiagFieldW(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT iRecord,
    SQLSMALLINT fDiagField,
    SQLPOINTER rgbDiagInfo,
    SQLSMALLINT cbDiagInfoMax,   // bytes
    SQLSMALLINT* pcbDiagInfo)    // bytes
{
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
{
    // The mock driver descriptor fields are all numeric — pass through
    return SQLGetDescField(hdesc, iRecord, iField, rgbValue,
                           cbValueMax, pcbValue);
}

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
{
    SQLCHAR ansi_name[512] = {0};
    SQLSMALLINT ansi_len = 0;
    SQLRETURN ret = SQLGetDescRec(hdesc, iRecord,
                                   ansi_name, sizeof(ansi_name), &ansi_len,
                                   pfType, pfSubType, pLength,
                                   pPrecision, pScale, pNullable);
    if (ret == SQL_ERROR || ret == SQL_NO_DATA) return ret;

    if (szName && cbNameMax > 0) {
        std::string name(reinterpret_cast<char*>(ansi_name), ansi_len);
        SQLSMALLINT wbytes = 0;
        copy_string_to_wbuffer(name, szName,
                               static_cast<SQLSMALLINT>(cbNameMax * sizeof(SQLWCHAR)),
                               &wbytes);
        if (pcbName) *pcbName = static_cast<SQLSMALLINT>(wbytes / sizeof(SQLWCHAR));
    } else if (pcbName) {
        *pcbName = ansi_len;
    }
    return ret;
}

SQLRETURN SQL_API SQLSetDescFieldW(
    SQLHDESC hdesc,
    SQLSMALLINT iRecord,
    SQLSMALLINT iField,
    SQLPOINTER rgbValue,
    SQLINTEGER cbValue)
{
    // Mock descriptor fields are all numeric — pass through
    return SQLSetDescField(hdesc, iRecord, iField, rgbValue, cbValue);
}

} // extern "C"
