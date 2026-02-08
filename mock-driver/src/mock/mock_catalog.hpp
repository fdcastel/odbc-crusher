#pragma once

#include "../driver/common.hpp"
#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

namespace mock_odbc {

// Forward declaration for CellValue
using CellValue = std::variant<std::monostate, long long, double, std::string>;
using MockRow = std::vector<CellValue>;

// Column definition for mock catalog
struct MockColumn {
    std::string name;
    SQLSMALLINT data_type;
    SQLULEN column_size;
    SQLSMALLINT decimal_digits;
    SQLSMALLINT nullable;
    bool is_primary_key;
    bool is_auto_increment;
    std::string default_value;
    std::string fk_table;       // Foreign key target table
    std::string fk_column;      // Foreign key target column
};

// Table definition
struct MockTable {
    std::string catalog;
    std::string schema;
    std::string name;
    std::string type;           // "TABLE", "VIEW", "SYSTEM TABLE"
    std::string remarks;
    std::vector<MockColumn> columns;
};

// Index definition
struct MockIndex {
    std::string table_name;
    std::string index_name;
    bool non_unique;
    std::string index_qualifier;
    SQLSMALLINT type;           // SQL_INDEX_CLUSTERED, etc.
    std::vector<std::string> columns;
};

// The mock catalog
class MockCatalog {
public:
    static MockCatalog& instance();
    
    // Initialize catalog based on preset
    void initialize(const std::string& preset);
    
    // Table operations
    const std::vector<MockTable>& tables() const { return tables_; }
    const MockTable* find_table(const std::string& name) const;
    
    // Mutable catalog operations (for CREATE TABLE / DROP TABLE)
    void add_table(const MockTable& table);
    void remove_table(const std::string& name);
    
    // Mutable data operations (for INSERT / transaction support)
    void insert_row(const std::string& table_name, MockRow row);
    void clear_inserted_data();
    void clear_inserted_data(const std::string& table_name);
    std::unordered_map<std::string, std::vector<MockRow>>& inserted_data() { return inserted_data_; }
    const std::unordered_map<std::string, std::vector<MockRow>>& inserted_data() const { return inserted_data_; }
    
    // Column operations
    std::vector<MockColumn> get_columns(const std::string& table_name,
                                         const std::string& column_pattern = "%") const;
    
    // Primary key operations
    std::vector<MockColumn> get_primary_keys(const std::string& table_name) const;
    
    // Foreign key operations
    std::vector<std::pair<MockColumn, MockColumn>> get_foreign_keys(
        const std::string& table_name) const;
    
    // Index operations
    std::vector<MockIndex> get_statistics(const std::string& table_name) const;
    
    // Pattern matching (SQL LIKE)
    static bool matches_pattern(const std::string& value, const std::string& pattern);
    
private:
    MockCatalog() = default;
    void create_default_catalog();
    void create_empty_catalog();
    void create_large_catalog();
    
    std::vector<MockTable> tables_;
    std::vector<MockIndex> indexes_;
    std::unordered_map<std::string, std::vector<MockRow>> inserted_data_;
};

} // namespace mock_odbc
