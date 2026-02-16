#include "driver_info.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <iomanip>
#include <sqlext.h>

namespace odbc_crusher::discovery {

DriverInfo::DriverInfo(core::OdbcConnection& conn)
    : conn_(conn) {
}

void DriverInfo::collect() {
    // Driver information
    driver_name_ = get_info_string(SQL_DRIVER_NAME);
    driver_version_ = get_info_string(SQL_DRIVER_VER);
    driver_odbc_version_ = get_info_string(SQL_DRIVER_ODBC_VER);
    
    // DBMS information
    dbms_name_ = get_info_string(SQL_DBMS_NAME);
    dbms_version_ = get_info_string(SQL_DBMS_VER);
    
    // SQL Conformance
    auto sql_conf = get_info_uint(SQL_SQL_CONFORMANCE);
    if (sql_conf) {
        switch (*sql_conf) {
            case SQL_SC_SQL92_ENTRY:
                sql_conformance_ = "SQL-92 Entry";
                break;
            case SQL_SC_FIPS127_2_TRANSITIONAL:
                sql_conformance_ = "FIPS 127-2 Transitional";
                break;
            case SQL_SC_SQL92_FULL:
                sql_conformance_ = "SQL-92 Full";
                break;
            case SQL_SC_SQL92_INTERMEDIATE:
                sql_conformance_ = "SQL-92 Intermediate";
                break;
            default:
                sql_conformance_ = "Custom (" + std::to_string(*sql_conf) + ")";
                break;
        }
    }
    
    // ODBC Interface Conformance
    auto odbc_conf = get_info_uint(SQL_ODBC_INTERFACE_CONFORMANCE);
    if (odbc_conf) {
        switch (*odbc_conf) {
            case SQL_OIC_CORE:
                odbc_interface_conformance_ = "Core";
                break;
            case SQL_OIC_LEVEL1:
                odbc_interface_conformance_ = "Level 1";
                break;
            case SQL_OIC_LEVEL2:
                odbc_interface_conformance_ = "Level 2";
                break;
            default:
                odbc_interface_conformance_ = "Unknown (" + std::to_string(*odbc_conf) + ")";
                break;
        }
    }
    
    // Store in map for reporting
    if (driver_name_) info_map_["Driver Name"] = *driver_name_;
    if (driver_version_) info_map_["Driver Version"] = *driver_version_;
    if (driver_odbc_version_) info_map_["Driver ODBC Version"] = *driver_odbc_version_;
    if (dbms_name_) info_map_["DBMS Name"] = *dbms_name_;
    if (dbms_version_) info_map_["DBMS Version"] = *dbms_version_;
    if (sql_conformance_) info_map_["SQL Conformance"] = *sql_conformance_;
    if (odbc_interface_conformance_) info_map_["ODBC Interface Conformance"] = *odbc_interface_conformance_;
    
    // Collect additional useful info
    if (auto max_conn = get_info_uint(SQL_MAX_CONCURRENT_ACTIVITIES)) {
        info_map_["Max Concurrent Activities"] = std::to_string(*max_conn);
    }
    
    if (auto max_identifier = get_info_uint(SQL_MAX_IDENTIFIER_LEN)) {
        info_map_["Max Identifier Length"] = std::to_string(*max_identifier);
    }
    
    if (auto catalog_name = get_info_string(SQL_CATALOG_NAME)) {
        info_map_["Catalog Name Support"] = *catalog_name;
    }
    
    if (auto procedures = get_info_string(SQL_PROCEDURES)) {
        info_map_["Procedures Support"] = *procedures;
    }
    
    // Collect fields needed by get_properties()
    if (auto odbc_ver = get_info_string(SQL_ODBC_VER)) {
        info_map_["ODBC Version"] = *odbc_ver;
    }
    if (auto db_name = get_info_string(SQL_DATABASE_NAME)) {
        info_map_["Database Name"] = *db_name;
    }
    if (auto srv_name = get_info_string(SQL_SERVER_NAME)) {
        info_map_["Server Name"] = *srv_name;
    }
    if (auto usr_name = get_info_string(SQL_USER_NAME)) {
        info_map_["User Name"] = *usr_name;
    }
    if (auto cat_term = get_info_string(SQL_CATALOG_TERM)) {
        info_map_["Catalog Term"] = *cat_term;
    }
    if (auto sch_term = get_info_string(SQL_SCHEMA_TERM)) {
        info_map_["Schema Term"] = *sch_term;
    }
    if (auto tbl_term = get_info_string(SQL_TABLE_TERM)) {
        info_map_["Table Term"] = *tbl_term;
    }
    if (auto proc_term = get_info_string(SQL_PROCEDURE_TERM)) {
        info_map_["Procedure Term"] = *proc_term;
    }
    if (auto ident_quote = get_info_string(SQL_IDENTIFIER_QUOTE_CHAR)) {
        info_map_["Identifier Quote Char"] = *ident_quote;
    }

    // Collect scalar function capabilities (Phase 26)
    collect_scalar_functions();
}

std::optional<std::string> DriverInfo::get_info_string(SQLUSMALLINT info_type) {
    SQLCHAR buffer[1024] = {0};
    SQLSMALLINT buffer_length = 0;
    
    SQLRETURN ret = SQLGetInfo(conn_.get_handle(), info_type, buffer, sizeof(buffer), &buffer_length);
    
    if (SQL_SUCCEEDED(ret)) {
        return std::string(reinterpret_cast<char*>(buffer), buffer_length);
    }
    
    return std::nullopt;
}

std::optional<SQLUINTEGER> DriverInfo::get_info_uint(SQLUSMALLINT info_type) {
    SQLUINTEGER value = 0;
    
    SQLRETURN ret = SQLGetInfo(conn_.get_handle(), info_type, &value, sizeof(value), nullptr);
    
    if (SQL_SUCCEEDED(ret)) {
        return value;
    }
    
    return std::nullopt;
}

std::string DriverInfo::format_summary() const {
    std::ostringstream oss;
    
    oss << "Driver Information:\n";
    oss << "==================\n";
    
    for (const auto& [key, value] : info_map_) {
        oss << std::left << std::setw(30) << key << ": " << value << "\n";
    }
    
    return oss.str();
}
DriverInfo::Properties DriverInfo::get_properties() const {
    Properties props;
    props.driver_name = driver_name_.value_or("");
    props.driver_ver = driver_version_.value_or("");
    props.driver_odbc_ver = driver_odbc_version_.value_or("");
    props.dbms_name = dbms_name_.value_or("");
    props.dbms_ver = dbms_version_.value_or("");
    props.sql_conformance = sql_conformance_.value_or("");
    
    // Get ODBC version from driver manager
    auto it = info_map_.find("ODBC Version");
    props.odbc_ver = (it != info_map_.end()) ? it->second : "";
    
    // Get additional info from the map
    it = info_map_.find("Database Name");
    props.database_name = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Server Name");
    props.server_name = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("User Name");
    props.user_name = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Catalog Term");
    props.catalog_term = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Schema Term");
    props.schema_term = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Table Term");
    props.table_term = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Procedure Term");
    props.procedure_term = (it != info_map_.end()) ? it->second : "";
    
    it = info_map_.find("Identifier Quote Char");
    props.identifier_quote_char = (it != info_map_.end()) ? it->second : "";
    
    return props;
}

void DriverInfo::collect_scalar_functions() {
    // String functions
    if (auto v = get_info_uint(SQL_STRING_FUNCTIONS)) {
        scalar_functions_.string_bitmask = *v;
        if (*v & SQL_FN_STR_ASCII) scalar_functions_.string_functions.push_back("ASCII");
        if (*v & SQL_FN_STR_BIT_LENGTH) scalar_functions_.string_functions.push_back("BIT_LENGTH");
        if (*v & SQL_FN_STR_CHAR) scalar_functions_.string_functions.push_back("CHAR");
        if (*v & SQL_FN_STR_CHAR_LENGTH) scalar_functions_.string_functions.push_back("CHAR_LENGTH");
        if (*v & SQL_FN_STR_CHARACTER_LENGTH) scalar_functions_.string_functions.push_back("CHARACTER_LENGTH");
        if (*v & SQL_FN_STR_CONCAT) scalar_functions_.string_functions.push_back("CONCAT");
        if (*v & SQL_FN_STR_DIFFERENCE) scalar_functions_.string_functions.push_back("DIFFERENCE");
        if (*v & SQL_FN_STR_INSERT) scalar_functions_.string_functions.push_back("INSERT");
        if (*v & SQL_FN_STR_LCASE) scalar_functions_.string_functions.push_back("LCASE");
        if (*v & SQL_FN_STR_LEFT) scalar_functions_.string_functions.push_back("LEFT");
        if (*v & SQL_FN_STR_LENGTH) scalar_functions_.string_functions.push_back("LENGTH");
        if (*v & SQL_FN_STR_LOCATE) scalar_functions_.string_functions.push_back("LOCATE");
        if (*v & SQL_FN_STR_LOCATE_2) scalar_functions_.string_functions.push_back("LOCATE_2");
        if (*v & SQL_FN_STR_LTRIM) scalar_functions_.string_functions.push_back("LTRIM");
        if (*v & SQL_FN_STR_OCTET_LENGTH) scalar_functions_.string_functions.push_back("OCTET_LENGTH");
        if (*v & SQL_FN_STR_POSITION) scalar_functions_.string_functions.push_back("POSITION");
        if (*v & SQL_FN_STR_REPEAT) scalar_functions_.string_functions.push_back("REPEAT");
        if (*v & SQL_FN_STR_REPLACE) scalar_functions_.string_functions.push_back("REPLACE");
        if (*v & SQL_FN_STR_RIGHT) scalar_functions_.string_functions.push_back("RIGHT");
        if (*v & SQL_FN_STR_RTRIM) scalar_functions_.string_functions.push_back("RTRIM");
        if (*v & SQL_FN_STR_SOUNDEX) scalar_functions_.string_functions.push_back("SOUNDEX");
        if (*v & SQL_FN_STR_SPACE) scalar_functions_.string_functions.push_back("SPACE");
        if (*v & SQL_FN_STR_SUBSTRING) scalar_functions_.string_functions.push_back("SUBSTRING");
        if (*v & SQL_FN_STR_UCASE) scalar_functions_.string_functions.push_back("UCASE");
    }

    // Numeric functions
    if (auto v = get_info_uint(SQL_NUMERIC_FUNCTIONS)) {
        scalar_functions_.numeric_bitmask = *v;
        if (*v & SQL_FN_NUM_ABS) scalar_functions_.numeric_functions.push_back("ABS");
        if (*v & SQL_FN_NUM_ACOS) scalar_functions_.numeric_functions.push_back("ACOS");
        if (*v & SQL_FN_NUM_ASIN) scalar_functions_.numeric_functions.push_back("ASIN");
        if (*v & SQL_FN_NUM_ATAN) scalar_functions_.numeric_functions.push_back("ATAN");
        if (*v & SQL_FN_NUM_ATAN2) scalar_functions_.numeric_functions.push_back("ATAN2");
        if (*v & SQL_FN_NUM_CEILING) scalar_functions_.numeric_functions.push_back("CEILING");
        if (*v & SQL_FN_NUM_COS) scalar_functions_.numeric_functions.push_back("COS");
        if (*v & SQL_FN_NUM_COT) scalar_functions_.numeric_functions.push_back("COT");
        if (*v & SQL_FN_NUM_DEGREES) scalar_functions_.numeric_functions.push_back("DEGREES");
        if (*v & SQL_FN_NUM_EXP) scalar_functions_.numeric_functions.push_back("EXP");
        if (*v & SQL_FN_NUM_FLOOR) scalar_functions_.numeric_functions.push_back("FLOOR");
        if (*v & SQL_FN_NUM_LOG) scalar_functions_.numeric_functions.push_back("LOG");
        if (*v & SQL_FN_NUM_LOG10) scalar_functions_.numeric_functions.push_back("LOG10");
        if (*v & SQL_FN_NUM_MOD) scalar_functions_.numeric_functions.push_back("MOD");
        if (*v & SQL_FN_NUM_PI) scalar_functions_.numeric_functions.push_back("PI");
        if (*v & SQL_FN_NUM_POWER) scalar_functions_.numeric_functions.push_back("POWER");
        if (*v & SQL_FN_NUM_RADIANS) scalar_functions_.numeric_functions.push_back("RADIANS");
        if (*v & SQL_FN_NUM_RAND) scalar_functions_.numeric_functions.push_back("RAND");
        if (*v & SQL_FN_NUM_ROUND) scalar_functions_.numeric_functions.push_back("ROUND");
        if (*v & SQL_FN_NUM_SIGN) scalar_functions_.numeric_functions.push_back("SIGN");
        if (*v & SQL_FN_NUM_SIN) scalar_functions_.numeric_functions.push_back("SIN");
        if (*v & SQL_FN_NUM_SQRT) scalar_functions_.numeric_functions.push_back("SQRT");
        if (*v & SQL_FN_NUM_TAN) scalar_functions_.numeric_functions.push_back("TAN");
        if (*v & SQL_FN_NUM_TRUNCATE) scalar_functions_.numeric_functions.push_back("TRUNCATE");
    }

    // Timedate functions
    if (auto v = get_info_uint(SQL_TIMEDATE_FUNCTIONS)) {
        scalar_functions_.timedate_bitmask = *v;
        if (*v & SQL_FN_TD_CURDATE) scalar_functions_.timedate_functions.push_back("CURDATE");
        if (*v & SQL_FN_TD_CURTIME) scalar_functions_.timedate_functions.push_back("CURTIME");
        if (*v & SQL_FN_TD_DAYNAME) scalar_functions_.timedate_functions.push_back("DAYNAME");
        if (*v & SQL_FN_TD_DAYOFMONTH) scalar_functions_.timedate_functions.push_back("DAYOFMONTH");
        if (*v & SQL_FN_TD_DAYOFWEEK) scalar_functions_.timedate_functions.push_back("DAYOFWEEK");
        if (*v & SQL_FN_TD_DAYOFYEAR) scalar_functions_.timedate_functions.push_back("DAYOFYEAR");
        if (*v & SQL_FN_TD_EXTRACT) scalar_functions_.timedate_functions.push_back("EXTRACT");
        if (*v & SQL_FN_TD_HOUR) scalar_functions_.timedate_functions.push_back("HOUR");
        if (*v & SQL_FN_TD_MINUTE) scalar_functions_.timedate_functions.push_back("MINUTE");
        if (*v & SQL_FN_TD_MONTH) scalar_functions_.timedate_functions.push_back("MONTH");
        if (*v & SQL_FN_TD_MONTHNAME) scalar_functions_.timedate_functions.push_back("MONTHNAME");
        if (*v & SQL_FN_TD_NOW) scalar_functions_.timedate_functions.push_back("NOW");
        if (*v & SQL_FN_TD_QUARTER) scalar_functions_.timedate_functions.push_back("QUARTER");
        if (*v & SQL_FN_TD_SECOND) scalar_functions_.timedate_functions.push_back("SECOND");
        if (*v & SQL_FN_TD_TIMESTAMPADD) scalar_functions_.timedate_functions.push_back("TIMESTAMPADD");
        if (*v & SQL_FN_TD_TIMESTAMPDIFF) scalar_functions_.timedate_functions.push_back("TIMESTAMPDIFF");
        if (*v & SQL_FN_TD_WEEK) scalar_functions_.timedate_functions.push_back("WEEK");
        if (*v & SQL_FN_TD_YEAR) scalar_functions_.timedate_functions.push_back("YEAR");
    }

    // System functions
    if (auto v = get_info_uint(SQL_SYSTEM_FUNCTIONS)) {
        scalar_functions_.system_bitmask = *v;
        if (*v & SQL_FN_SYS_DBNAME) scalar_functions_.system_functions.push_back("DATABASE");
        if (*v & SQL_FN_SYS_IFNULL) scalar_functions_.system_functions.push_back("IFNULL");
        if (*v & SQL_FN_SYS_USERNAME) scalar_functions_.system_functions.push_back("USER");
    }

    // Convert functions
    if (auto v = get_info_uint(SQL_CONVERT_FUNCTIONS)) {
        scalar_functions_.convert_functions_bitmask = *v;
    }

    // OJ capabilities
    if (auto v = get_info_uint(SQL_OJ_CAPABILITIES)) {
        scalar_functions_.oj_capabilities = *v;
    }

    // Datetime literals
    if (auto v = get_info_uint(SQL_DATETIME_LITERALS)) {
        scalar_functions_.datetime_literals = *v;
    }

    // Timedate add/diff intervals
    if (auto v = get_info_uint(SQL_TIMEDATE_ADD_INTERVALS)) {
        scalar_functions_.timedate_add_intervals = *v;
    }
    if (auto v = get_info_uint(SQL_TIMEDATE_DIFF_INTERVALS)) {
        scalar_functions_.timedate_diff_intervals = *v;
    }

    // Conversion matrix
    struct ConvertEntry {
        SQLUSMALLINT info_type;
        const char* name;
    };
    static const ConvertEntry convert_entries[] = {
        {SQL_CONVERT_CHAR, "CHAR"},
        {SQL_CONVERT_VARCHAR, "VARCHAR"},
        {SQL_CONVERT_LONGVARCHAR, "LONGVARCHAR"},
        {SQL_CONVERT_WCHAR, "WCHAR"},
        {SQL_CONVERT_WVARCHAR, "WVARCHAR"},
        {SQL_CONVERT_WLONGVARCHAR, "WLONGVARCHAR"},
        {SQL_CONVERT_INTEGER, "INTEGER"},
        {SQL_CONVERT_SMALLINT, "SMALLINT"},
        {SQL_CONVERT_BIGINT, "BIGINT"},
        {SQL_CONVERT_TINYINT, "TINYINT"},
        {SQL_CONVERT_DECIMAL, "DECIMAL"},
        {SQL_CONVERT_NUMERIC, "NUMERIC"},
        {SQL_CONVERT_DOUBLE, "DOUBLE"},
        {SQL_CONVERT_FLOAT, "FLOAT"},
        {SQL_CONVERT_REAL, "REAL"},
        {SQL_CONVERT_DATE, "DATE"},
        {SQL_CONVERT_TIME, "TIME"},
        {SQL_CONVERT_TIMESTAMP, "TIMESTAMP"},
        {SQL_CONVERT_BIT, "BIT"},
        {SQL_CONVERT_BINARY, "BINARY"},
        {SQL_CONVERT_VARBINARY, "VARBINARY"},
        {SQL_CONVERT_LONGVARBINARY, "LONGVARBINARY"},
        {SQL_CONVERT_GUID, "GUID"},
    };

    for (const auto& entry : convert_entries) {
        if (auto v = get_info_uint(entry.info_type)) {
            scalar_functions_.convert_matrix[entry.name] = *v;
        }
    }

    // Add summary to info_map for console display
    info_map_["String Functions"] = std::to_string(scalar_functions_.string_functions.size()) + " supported";
    info_map_["Numeric Functions"] = std::to_string(scalar_functions_.numeric_functions.size()) + " supported";
    info_map_["Timedate Functions"] = std::to_string(scalar_functions_.timedate_functions.size()) + " supported";
    info_map_["System Functions"] = std::to_string(scalar_functions_.system_functions.size()) + " supported";
}
} // namespace odbc_crusher::discovery
