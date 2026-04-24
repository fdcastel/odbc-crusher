#include "driver_info.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <iomanip>
#include <sqlext.h>

namespace odbc_crusher::discovery {

namespace {

struct EnumLabel {
    SQLUINTEGER value;
    const char* name;
};

template <size_t N>
std::string lookup_enum(SQLUINTEGER value, const EnumLabel (&table)[N],
                        const char* unknown_prefix) {
    for (const auto& entry : table) {
        if (entry.value == value) {
            return entry.name;
        }
    }
    return std::string(unknown_prefix) + " (" + std::to_string(value) + ")";
}

constexpr EnumLabel kSqlConformanceLabels[] = {
    {SQL_SC_SQL92_ENTRY,            "SQL-92 Entry"},
    {SQL_SC_FIPS127_2_TRANSITIONAL, "FIPS 127-2 Transitional"},
    {SQL_SC_SQL92_INTERMEDIATE,     "SQL-92 Intermediate"},
    {SQL_SC_SQL92_FULL,             "SQL-92 Full"},
};

constexpr EnumLabel kOdbcInterfaceConformanceLabels[] = {
    {SQL_OIC_CORE,   "Core"},
    {SQL_OIC_LEVEL1, "Level 1"},
    {SQL_OIC_LEVEL2, "Level 2"},
};

}  // namespace

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

    if (auto sql_conf = get_info_uint(SQL_SQL_CONFORMANCE)) {
        sql_conformance_ = lookup_enum(*sql_conf, kSqlConformanceLabels, "Custom");
    }

    if (auto odbc_conf = get_info_uint(SQL_ODBC_INTERFACE_CONFORMANCE)) {
        odbc_interface_conformance_ =
            lookup_enum(*odbc_conf, kOdbcInterfaceConformanceLabels, "Unknown");
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

namespace {

struct ScalarFn {
    SQLUINTEGER flag;
    const char* name;
};

// Appendix E of the ODBC Programmer's Reference lists these four bitmask
// groups. Keep the tables in spec order so a diff against Microsoft's docs
// is trivial.

constexpr ScalarFn kStringFunctions[] = {
    {SQL_FN_STR_ASCII,            "ASCII"},
    {SQL_FN_STR_BIT_LENGTH,       "BIT_LENGTH"},
    {SQL_FN_STR_CHAR,             "CHAR"},
    {SQL_FN_STR_CHAR_LENGTH,      "CHAR_LENGTH"},
    {SQL_FN_STR_CHARACTER_LENGTH, "CHARACTER_LENGTH"},
    {SQL_FN_STR_CONCAT,           "CONCAT"},
    {SQL_FN_STR_DIFFERENCE,       "DIFFERENCE"},
    {SQL_FN_STR_INSERT,           "INSERT"},
    {SQL_FN_STR_LCASE,            "LCASE"},
    {SQL_FN_STR_LEFT,             "LEFT"},
    {SQL_FN_STR_LENGTH,           "LENGTH"},
    {SQL_FN_STR_LOCATE,           "LOCATE"},
    {SQL_FN_STR_LOCATE_2,         "LOCATE_2"},
    {SQL_FN_STR_LTRIM,            "LTRIM"},
    {SQL_FN_STR_OCTET_LENGTH,     "OCTET_LENGTH"},
    {SQL_FN_STR_POSITION,         "POSITION"},
    {SQL_FN_STR_REPEAT,           "REPEAT"},
    {SQL_FN_STR_REPLACE,          "REPLACE"},
    {SQL_FN_STR_RIGHT,            "RIGHT"},
    {SQL_FN_STR_RTRIM,            "RTRIM"},
    {SQL_FN_STR_SOUNDEX,          "SOUNDEX"},
    {SQL_FN_STR_SPACE,            "SPACE"},
    {SQL_FN_STR_SUBSTRING,        "SUBSTRING"},
    {SQL_FN_STR_UCASE,            "UCASE"},
};

constexpr ScalarFn kNumericFunctions[] = {
    {SQL_FN_NUM_ABS,      "ABS"},
    {SQL_FN_NUM_ACOS,     "ACOS"},
    {SQL_FN_NUM_ASIN,     "ASIN"},
    {SQL_FN_NUM_ATAN,     "ATAN"},
    {SQL_FN_NUM_ATAN2,    "ATAN2"},
    {SQL_FN_NUM_CEILING,  "CEILING"},
    {SQL_FN_NUM_COS,      "COS"},
    {SQL_FN_NUM_COT,      "COT"},
    {SQL_FN_NUM_DEGREES,  "DEGREES"},
    {SQL_FN_NUM_EXP,      "EXP"},
    {SQL_FN_NUM_FLOOR,    "FLOOR"},
    {SQL_FN_NUM_LOG,      "LOG"},
    {SQL_FN_NUM_LOG10,    "LOG10"},
    {SQL_FN_NUM_MOD,      "MOD"},
    {SQL_FN_NUM_PI,       "PI"},
    {SQL_FN_NUM_POWER,    "POWER"},
    {SQL_FN_NUM_RADIANS,  "RADIANS"},
    {SQL_FN_NUM_RAND,     "RAND"},
    {SQL_FN_NUM_ROUND,    "ROUND"},
    {SQL_FN_NUM_SIGN,     "SIGN"},
    {SQL_FN_NUM_SIN,      "SIN"},
    {SQL_FN_NUM_SQRT,     "SQRT"},
    {SQL_FN_NUM_TAN,      "TAN"},
    {SQL_FN_NUM_TRUNCATE, "TRUNCATE"},
};

constexpr ScalarFn kTimedateFunctions[] = {
    {SQL_FN_TD_CURDATE,       "CURDATE"},
    {SQL_FN_TD_CURTIME,       "CURTIME"},
    {SQL_FN_TD_DAYNAME,       "DAYNAME"},
    {SQL_FN_TD_DAYOFMONTH,    "DAYOFMONTH"},
    {SQL_FN_TD_DAYOFWEEK,     "DAYOFWEEK"},
    {SQL_FN_TD_DAYOFYEAR,     "DAYOFYEAR"},
    {SQL_FN_TD_EXTRACT,       "EXTRACT"},
    {SQL_FN_TD_HOUR,          "HOUR"},
    {SQL_FN_TD_MINUTE,        "MINUTE"},
    {SQL_FN_TD_MONTH,         "MONTH"},
    {SQL_FN_TD_MONTHNAME,     "MONTHNAME"},
    {SQL_FN_TD_NOW,           "NOW"},
    {SQL_FN_TD_QUARTER,       "QUARTER"},
    {SQL_FN_TD_SECOND,        "SECOND"},
    {SQL_FN_TD_TIMESTAMPADD,  "TIMESTAMPADD"},
    {SQL_FN_TD_TIMESTAMPDIFF, "TIMESTAMPDIFF"},
    {SQL_FN_TD_WEEK,          "WEEK"},
    {SQL_FN_TD_YEAR,          "YEAR"},
};

// NOTE: SQL_FN_SYS_DBNAME/USERNAME map to the public-facing names
// DATABASE / USER — the driver-visible bit name and the scalar function
// name differ.
constexpr ScalarFn kSystemFunctions[] = {
    {SQL_FN_SYS_DBNAME,   "DATABASE"},
    {SQL_FN_SYS_IFNULL,   "IFNULL"},
    {SQL_FN_SYS_USERNAME, "USER"},
};

template <size_t N>
void unpack_bitmask(SQLUINTEGER value, const ScalarFn (&table)[N],
                    std::vector<std::string>& out) {
    for (const auto& entry : table) {
        if (value & entry.flag) {
            out.emplace_back(entry.name);
        }
    }
}

}  // namespace

void DriverInfo::collect_scalar_functions() {
    if (auto v = get_info_uint(SQL_STRING_FUNCTIONS)) {
        scalar_functions_.string_bitmask = *v;
        unpack_bitmask(*v, kStringFunctions, scalar_functions_.string_functions);
    }
    if (auto v = get_info_uint(SQL_NUMERIC_FUNCTIONS)) {
        scalar_functions_.numeric_bitmask = *v;
        unpack_bitmask(*v, kNumericFunctions, scalar_functions_.numeric_functions);
    }
    if (auto v = get_info_uint(SQL_TIMEDATE_FUNCTIONS)) {
        scalar_functions_.timedate_bitmask = *v;
        unpack_bitmask(*v, kTimedateFunctions, scalar_functions_.timedate_functions);
    }
    if (auto v = get_info_uint(SQL_SYSTEM_FUNCTIONS)) {
        scalar_functions_.system_bitmask = *v;
        unpack_bitmask(*v, kSystemFunctions, scalar_functions_.system_functions);
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
