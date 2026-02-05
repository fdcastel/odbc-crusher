// Catalog API - SQLTables, SQLColumns, SQLPrimaryKeys, etc.

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/behaviors.hpp"
#include "utils/string_utils.hpp"

using namespace mock_odbc;

namespace {

// Helper to create a catalog result set
void setup_catalog_result(StatementHandle* stmt,
                          const std::vector<std::string>& col_names,
                          const std::vector<SQLSMALLINT>& col_types) {
    stmt->executed_ = true;
    stmt->cursor_open_ = true;
    stmt->current_row_ = -1;
    stmt->num_result_cols_ = static_cast<SQLSMALLINT>(col_names.size());
    stmt->column_names_ = col_names;
    stmt->column_types_ = col_types;
    stmt->result_data_.clear();
}

} // anonymous namespace

extern "C" {

SQLRETURN SQL_API SQLTables(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName,
    SQLCHAR* szTableType,
    SQLSMALLINT cbTableType) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLTables")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated SQLTables failure");
        return SQL_ERROR;
    }
    
    std::string table_pattern = sql_to_string(szTableName, cbTableName);
    std::string type_pattern = sql_to_string(szTableType, cbTableType);
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    
    // Set up result columns as per ODBC spec
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "TABLE_TYPE", "REMARKS"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR});
    
    // Get matching tables
    MockCatalog& catalog = MockCatalog::instance();
    
    for (const auto& table : catalog.tables()) {
        // Filter by pattern
        if (!table_pattern.empty() && table_pattern != "%" && 
            !MockCatalog::matches_pattern(table.name, table_pattern)) {
            continue;
        }
        
        // Filter by type
        if (!type_pattern.empty() && type_pattern != "%" &&
            type_pattern.find(table.type) == std::string::npos) {
            continue;
        }
        
        std::vector<std::variant<std::monostate, long long, double, std::string>> row;
        row.push_back(table.catalog.empty() ? std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : table.catalog);
        row.push_back(table.schema.empty() ? std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : table.schema);
        row.push_back(table.name);
        row.push_back(table.type);
        row.push_back(table.remarks);
        
        stmt->result_data_.push_back(std::move(row));
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColumns(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName,
    SQLCHAR* szColumnName,
    SQLSMALLINT cbColumnName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLColumns")) {
        stmt->add_diagnostic(config.error_code, 0, "Simulated SQLColumns failure");
        return SQL_ERROR;
    }
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    
    std::string table_pattern = sql_to_string(szTableName, cbTableName);
    std::string column_pattern = sql_to_string(szColumnName, cbColumnName);
    
    if (column_pattern.empty()) column_pattern = "%";
    
    // Set up result columns as per ODBC spec (18 columns)
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "DATA_TYPE",
         "TYPE_NAME", "COLUMN_SIZE", "BUFFER_LENGTH", "DECIMAL_DIGITS", "NUM_PREC_RADIX",
         "NULLABLE", "REMARKS", "COLUMN_DEF", "SQL_DATA_TYPE", "SQL_DATETIME_SUB",
         "CHAR_OCTET_LENGTH", "ORDINAL_POSITION", "IS_NULLABLE"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_SMALLINT,
         SQL_VARCHAR, SQL_INTEGER, SQL_INTEGER, SQL_SMALLINT, SQL_SMALLINT,
         SQL_SMALLINT, SQL_VARCHAR, SQL_VARCHAR, SQL_SMALLINT, SQL_SMALLINT,
         SQL_INTEGER, SQL_INTEGER, SQL_VARCHAR});
    
    MockCatalog& catalog = MockCatalog::instance();
    
    for (const auto& table : catalog.tables()) {
        if (!table_pattern.empty() && table_pattern != "%" &&
            !MockCatalog::matches_pattern(table.name, table_pattern)) {
            continue;
        }
        
        int ordinal = 1;
        for (const auto& col : table.columns) {
            if (!MockCatalog::matches_pattern(col.name, column_pattern)) {
                continue;
            }
            
            std::vector<std::variant<std::monostate, long long, double, std::string>> row;
            row.push_back(std::monostate{});  // TABLE_CAT
            row.push_back(std::monostate{});  // TABLE_SCHEM
            row.push_back(table.name);        // TABLE_NAME
            row.push_back(col.name);          // COLUMN_NAME
            row.push_back(static_cast<long long>(col.data_type));  // DATA_TYPE
            
            // TYPE_NAME
            std::string type_name;
            switch (col.data_type) {
                case SQL_INTEGER: type_name = "INTEGER"; break;
                case SQL_VARCHAR: type_name = "VARCHAR"; break;
                case SQL_DECIMAL: type_name = "DECIMAL"; break;
                case SQL_TYPE_DATE: type_name = "DATE"; break;
                case SQL_TYPE_TIMESTAMP: type_name = "TIMESTAMP"; break;
                case SQL_BIT: type_name = "BIT"; break;
                case SQL_LONGVARCHAR: type_name = "TEXT"; break;
                default: type_name = "UNKNOWN";
            }
            row.push_back(type_name);
            
            row.push_back(static_cast<long long>(col.column_size));  // COLUMN_SIZE
            row.push_back(static_cast<long long>(col.column_size));  // BUFFER_LENGTH
            row.push_back(static_cast<long long>(col.decimal_digits));  // DECIMAL_DIGITS
            row.push_back(static_cast<long long>(10));  // NUM_PREC_RADIX
            row.push_back(static_cast<long long>(col.nullable));  // NULLABLE
            row.push_back(std::string(""));  // REMARKS
            row.push_back(col.default_value.empty() ? 
                         std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : 
                         col.default_value);  // COLUMN_DEF
            row.push_back(static_cast<long long>(col.data_type));  // SQL_DATA_TYPE
            row.push_back(std::monostate{});  // SQL_DATETIME_SUB
            row.push_back(static_cast<long long>(col.column_size));  // CHAR_OCTET_LENGTH
            row.push_back(static_cast<long long>(ordinal));  // ORDINAL_POSITION
            row.push_back(col.nullable == SQL_NULLABLE ? std::string("YES") : std::string("NO"));  // IS_NULLABLE
            
            stmt->result_data_.push_back(std::move(row));
            ordinal++;
        }
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLPrimaryKeys(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    
    std::string table_name = sql_to_string(szTableName, cbTableName);
    
    // Set up result columns
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "KEY_SEQ", "PK_NAME"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_SMALLINT, SQL_VARCHAR});
    
    MockCatalog& catalog = MockCatalog::instance();
    auto pk_cols = catalog.get_primary_keys(table_name);
    
    int seq = 1;
    for (const auto& col : pk_cols) {
        std::vector<std::variant<std::monostate, long long, double, std::string>> row;
        row.push_back(std::monostate{});  // TABLE_CAT
        row.push_back(std::monostate{});  // TABLE_SCHEM
        row.push_back(table_name);        // TABLE_NAME
        row.push_back(col.name);          // COLUMN_NAME
        row.push_back(static_cast<long long>(seq++));  // KEY_SEQ
        row.push_back(std::string("PK_") + table_name);  // PK_NAME
        
        stmt->result_data_.push_back(std::move(row));
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLForeignKeys(
    SQLHSTMT hstmt,
    SQLCHAR* szPkCatalogName,
    SQLSMALLINT cbPkCatalogName,
    SQLCHAR* szPkSchemaName,
    SQLSMALLINT cbPkSchemaName,
    SQLCHAR* szPkTableName,
    SQLSMALLINT cbPkTableName,
    SQLCHAR* szFkCatalogName,
    SQLSMALLINT cbFkCatalogName,
    SQLCHAR* szFkSchemaName,
    SQLSMALLINT cbFkSchemaName,
    SQLCHAR* szFkTableName,
    SQLSMALLINT cbFkTableName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szPkCatalogName;
    (void)cbPkCatalogName;
    (void)szPkSchemaName;
    (void)cbPkSchemaName;
    (void)szFkCatalogName;
    (void)cbFkCatalogName;
    (void)szFkSchemaName;
    (void)cbFkSchemaName;
    
    std::string pk_table = sql_to_string(szPkTableName, cbPkTableName);
    std::string fk_table = sql_to_string(szFkTableName, cbFkTableName);
    
    // Set up result columns (14 columns as per ODBC spec)
    setup_catalog_result(stmt,
        {"PKTABLE_CAT", "PKTABLE_SCHEM", "PKTABLE_NAME", "PKCOLUMN_NAME",
         "FKTABLE_CAT", "FKTABLE_SCHEM", "FKTABLE_NAME", "FKCOLUMN_NAME",
         "KEY_SEQ", "UPDATE_RULE", "DELETE_RULE", "FK_NAME", "PK_NAME", "DEFERRABILITY"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR,
         SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR,
         SQL_SMALLINT, SQL_SMALLINT, SQL_SMALLINT, SQL_VARCHAR, SQL_VARCHAR, SQL_SMALLINT});
    
    MockCatalog& catalog = MockCatalog::instance();
    
    // If FK table specified, get its foreign keys
    if (!fk_table.empty()) {
        auto fks = catalog.get_foreign_keys(fk_table);
        int seq = 1;
        
        for (const auto& [fk_col, pk_col] : fks) {
            std::vector<std::variant<std::monostate, long long, double, std::string>> row;
            row.push_back(std::monostate{});  // PKTABLE_CAT
            row.push_back(std::monostate{});  // PKTABLE_SCHEM
            row.push_back(fk_col.fk_table);   // PKTABLE_NAME
            row.push_back(fk_col.fk_column);  // PKCOLUMN_NAME
            row.push_back(std::monostate{});  // FKTABLE_CAT
            row.push_back(std::monostate{});  // FKTABLE_SCHEM
            row.push_back(fk_table);          // FKTABLE_NAME
            row.push_back(fk_col.name);       // FKCOLUMN_NAME
            row.push_back(static_cast<long long>(seq++));  // KEY_SEQ
            row.push_back(static_cast<long long>(SQL_CASCADE));  // UPDATE_RULE
            row.push_back(static_cast<long long>(SQL_CASCADE));  // DELETE_RULE
            row.push_back(std::string("FK_") + fk_table + "_" + fk_col.name);  // FK_NAME
            row.push_back(std::string("PK_") + fk_col.fk_table);  // PK_NAME
            row.push_back(static_cast<long long>(SQL_NOT_DEFERRABLE));  // DEFERRABILITY
            
            stmt->result_data_.push_back(std::move(row));
        }
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLStatistics(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName,
    SQLUSMALLINT fUnique,
    SQLUSMALLINT fAccuracy) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)fUnique;
    (void)fAccuracy;
    
    std::string table_name = sql_to_string(szTableName, cbTableName);
    
    // Set up result columns (13 columns as per ODBC spec)
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "NON_UNIQUE", "INDEX_QUALIFIER",
         "INDEX_NAME", "TYPE", "ORDINAL_POSITION", "COLUMN_NAME", "ASC_OR_DESC",
         "CARDINALITY", "PAGES", "FILTER_CONDITION"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_SMALLINT, SQL_VARCHAR,
         SQL_VARCHAR, SQL_SMALLINT, SQL_SMALLINT, SQL_VARCHAR, SQL_CHAR,
         SQL_INTEGER, SQL_INTEGER, SQL_VARCHAR});
    
    MockCatalog& catalog = MockCatalog::instance();
    auto indexes = catalog.get_statistics(table_name);
    
    for (const auto& idx : indexes) {
        int ordinal = 1;
        for (const auto& col : idx.columns) {
            std::vector<std::variant<std::monostate, long long, double, std::string>> row;
            row.push_back(std::monostate{});  // TABLE_CAT
            row.push_back(std::monostate{});  // TABLE_SCHEM
            row.push_back(idx.table_name);    // TABLE_NAME
            row.push_back(static_cast<long long>(idx.non_unique ? 1 : 0));  // NON_UNIQUE
            row.push_back(idx.index_qualifier.empty() ? 
                         std::variant<std::monostate, long long, double, std::string>(std::monostate{}) : 
                         idx.index_qualifier);  // INDEX_QUALIFIER
            row.push_back(idx.index_name);    // INDEX_NAME
            row.push_back(static_cast<long long>(idx.type));  // TYPE
            row.push_back(static_cast<long long>(ordinal++));  // ORDINAL_POSITION
            row.push_back(col);               // COLUMN_NAME
            row.push_back(std::string("A"));  // ASC_OR_DESC
            row.push_back(static_cast<long long>(100));  // CARDINALITY (mock)
            row.push_back(static_cast<long long>(10));   // PAGES (mock)
            row.push_back(std::monostate{});  // FILTER_CONDITION
            
            stmt->result_data_.push_back(std::move(row));
        }
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLSpecialColumns(
    SQLHSTMT hstmt,
    SQLUSMALLINT fColType,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName,
    SQLUSMALLINT fScope,
    SQLUSMALLINT fNullable) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)fScope;
    (void)fNullable;
    
    std::string table_name = sql_to_string(szTableName, cbTableName);
    
    // Set up result columns (8 columns as per ODBC spec)
    setup_catalog_result(stmt,
        {"SCOPE", "COLUMN_NAME", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE",
         "BUFFER_LENGTH", "DECIMAL_DIGITS", "PSEUDO_COLUMN"},
        {SQL_SMALLINT, SQL_VARCHAR, SQL_SMALLINT, SQL_VARCHAR, SQL_INTEGER,
         SQL_INTEGER, SQL_SMALLINT, SQL_SMALLINT});
    
    MockCatalog& catalog = MockCatalog::instance();
    
    if (fColType == SQL_BEST_ROWID) {
        // Return primary key columns as row identifier
        auto pk_cols = catalog.get_primary_keys(table_name);
        
        for (const auto& col : pk_cols) {
            std::vector<std::variant<std::monostate, long long, double, std::string>> row;
            row.push_back(static_cast<long long>(SQL_SCOPE_SESSION));  // SCOPE
            row.push_back(col.name);  // COLUMN_NAME
            row.push_back(static_cast<long long>(col.data_type));  // DATA_TYPE
            row.push_back(std::string("INTEGER"));  // TYPE_NAME
            row.push_back(static_cast<long long>(col.column_size));  // COLUMN_SIZE
            row.push_back(static_cast<long long>(col.column_size));  // BUFFER_LENGTH
            row.push_back(static_cast<long long>(col.decimal_digits));  // DECIMAL_DIGITS
            row.push_back(static_cast<long long>(SQL_PC_NOT_PSEUDO));  // PSEUDO_COLUMN
            
            stmt->result_data_.push_back(std::move(row));
        }
    }
    
    stmt->row_count_ = static_cast<SQLLEN>(stmt->result_data_.size());
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLProcedures(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szProcName,
    SQLSMALLINT cbProcName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)szProcName;
    (void)cbProcName;
    
    // Mock: no procedures
    setup_catalog_result(stmt,
        {"PROCEDURE_CAT", "PROCEDURE_SCHEM", "PROCEDURE_NAME", "NUM_INPUT_PARAMS",
         "NUM_OUTPUT_PARAMS", "NUM_RESULT_SETS", "REMARKS", "PROCEDURE_TYPE"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_INTEGER,
         SQL_INTEGER, SQL_INTEGER, SQL_VARCHAR, SQL_SMALLINT});
    
    stmt->row_count_ = 0;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLProcedureColumns(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szProcName,
    SQLSMALLINT cbProcName,
    SQLCHAR* szColumnName,
    SQLSMALLINT cbColumnName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    stmt->clear_diagnostics();
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)szProcName;
    (void)cbProcName;
    (void)szColumnName;
    (void)cbColumnName;
    
    // Mock: no procedure columns
    setup_catalog_result(stmt,
        {"PROCEDURE_CAT", "PROCEDURE_SCHEM", "PROCEDURE_NAME", "COLUMN_NAME",
         "COLUMN_TYPE", "DATA_TYPE", "TYPE_NAME", "COLUMN_SIZE"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR,
         SQL_SMALLINT, SQL_SMALLINT, SQL_VARCHAR, SQL_INTEGER});
    
    stmt->row_count_ = 0;
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLTablePrivileges(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)szTableName;
    (void)cbTableName;
    
    // Mock: empty privileges
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "GRANTOR", "GRANTEE",
         "PRIVILEGE", "IS_GRANTABLE"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR,
         SQL_VARCHAR, SQL_VARCHAR});
    
    return SQL_SUCCESS;
}

SQLRETURN SQL_API SQLColumnPrivileges(
    SQLHSTMT hstmt,
    SQLCHAR* szCatalogName,
    SQLSMALLINT cbCatalogName,
    SQLCHAR* szSchemaName,
    SQLSMALLINT cbSchemaName,
    SQLCHAR* szTableName,
    SQLSMALLINT cbTableName,
    SQLCHAR* szColumnName,
    SQLSMALLINT cbColumnName) {
    
    auto* stmt = validate_stmt_handle(hstmt);
    if (!stmt) return SQL_INVALID_HANDLE;
    
    (void)szCatalogName;
    (void)cbCatalogName;
    (void)szSchemaName;
    (void)cbSchemaName;
    (void)szTableName;
    (void)cbTableName;
    (void)szColumnName;
    (void)cbColumnName;
    
    // Mock: empty privileges
    setup_catalog_result(stmt,
        {"TABLE_CAT", "TABLE_SCHEM", "TABLE_NAME", "COLUMN_NAME", "GRANTOR",
         "GRANTEE", "PRIVILEGE", "IS_GRANTABLE"},
        {SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR,
         SQL_VARCHAR, SQL_VARCHAR, SQL_VARCHAR});
    
    return SQL_SUCCESS;
}

} // extern "C"
