#include "driver_info.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <iomanip>

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
} // namespace odbc_crusher::discovery
