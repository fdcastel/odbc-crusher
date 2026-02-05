#include "mock_catalog.hpp"
#include <algorithm>
#include <cctype>

namespace mock_odbc {

namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}

} // anonymous namespace

MockCatalog& MockCatalog::instance() {
    static MockCatalog instance;
    return instance;
}

void MockCatalog::initialize(const std::string& preset) {
    tables_.clear();
    indexes_.clear();
    
    std::string lower_preset = preset;
    std::transform(lower_preset.begin(), lower_preset.end(), lower_preset.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (lower_preset == "empty") {
        create_empty_catalog();
    } else if (lower_preset == "large") {
        create_large_catalog();
    } else {
        create_default_catalog();
    }
}

void MockCatalog::create_default_catalog() {
    // USERS table
    MockTable users;
    users.catalog = "";
    users.schema = "";
    users.name = "USERS";
    users.type = "TABLE";
    users.remarks = "User accounts";
    users.columns = {
        {"USER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"USERNAME", SQL_VARCHAR, 50, 0, SQL_NO_NULLS, false, false, "", "", ""},
        {"EMAIL", SQL_VARCHAR, 100, 0, SQL_NULLABLE, false, false, "", "", ""},
        {"CREATED_DATE", SQL_TYPE_DATE, 10, 0, SQL_NULLABLE, false, false, "", "", ""},
        {"IS_ACTIVE", SQL_BIT, 1, 0, SQL_NULLABLE, false, false, "1", "", ""},
        {"BALANCE", SQL_DECIMAL, 10, 2, SQL_NULLABLE, false, false, "0.00", "", ""}
    };
    tables_.push_back(users);
    
    // ORDERS table
    MockTable orders;
    orders.catalog = "";
    orders.schema = "";
    orders.name = "ORDERS";
    orders.type = "TABLE";
    orders.remarks = "Order records";
    orders.columns = {
        {"ORDER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"USER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, false, false, "", "USERS", "USER_ID"},
        {"ORDER_DATE", SQL_TYPE_TIMESTAMP, 26, 6, SQL_NULLABLE, false, false, "", "", ""},
        {"TOTAL_AMOUNT", SQL_DECIMAL, 10, 2, SQL_NULLABLE, false, false, "0.00", "", ""},
        {"STATUS", SQL_VARCHAR, 20, 0, SQL_NULLABLE, false, false, "PENDING", "", ""}
    };
    tables_.push_back(orders);
    
    // PRODUCTS table
    MockTable products;
    products.catalog = "";
    products.schema = "";
    products.name = "PRODUCTS";
    products.type = "TABLE";
    products.remarks = "Product catalog";
    products.columns = {
        {"PRODUCT_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"NAME", SQL_VARCHAR, 100, 0, SQL_NO_NULLS, false, false, "", "", ""},
        {"DESCRIPTION", SQL_LONGVARCHAR, 65535, 0, SQL_NULLABLE, false, false, "", "", ""},
        {"PRICE", SQL_DECIMAL, 10, 2, SQL_NULLABLE, false, false, "0.00", "", ""},
        {"STOCK_QUANTITY", SQL_INTEGER, 10, 0, SQL_NULLABLE, false, false, "0", "", ""},
        {"CATEGORY", SQL_VARCHAR, 50, 0, SQL_NULLABLE, false, false, "", "", ""}
    };
    tables_.push_back(products);
    
    // ORDER_ITEMS table
    MockTable order_items;
    order_items.catalog = "";
    order_items.schema = "";
    order_items.name = "ORDER_ITEMS";
    order_items.type = "TABLE";
    order_items.remarks = "Order line items";
    order_items.columns = {
        {"ORDER_ITEM_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"ORDER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, false, false, "", "ORDERS", "ORDER_ID"},
        {"PRODUCT_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, false, false, "", "PRODUCTS", "PRODUCT_ID"},
        {"QUANTITY", SQL_INTEGER, 10, 0, SQL_NULLABLE, false, false, "1", "", ""},
        {"UNIT_PRICE", SQL_DECIMAL, 10, 2, SQL_NULLABLE, false, false, "0.00", "", ""}
    };
    tables_.push_back(order_items);
    
    // Create indexes
    MockIndex users_pk;
    users_pk.table_name = "USERS";
    users_pk.index_name = "PK_USERS";
    users_pk.non_unique = false;
    users_pk.type = SQL_INDEX_CLUSTERED;
    users_pk.columns = {"USER_ID"};
    indexes_.push_back(users_pk);
    
    MockIndex users_username;
    users_username.table_name = "USERS";
    users_username.index_name = "UQ_USERS_USERNAME";
    users_username.non_unique = false;
    users_username.type = SQL_INDEX_OTHER;
    users_username.columns = {"USERNAME"};
    indexes_.push_back(users_username);
}

void MockCatalog::create_empty_catalog() {
    // No tables
}

void MockCatalog::create_large_catalog() {
    create_default_catalog();
    
    // Add more tables for performance testing
    for (int i = 1; i <= 100; ++i) {
        MockTable table;
        table.catalog = "";
        table.schema = "";
        table.name = "TABLE_" + std::to_string(i);
        table.type = "TABLE";
        table.remarks = "Generated table " + std::to_string(i);
        
        // Add columns
        for (int j = 1; j <= 20; ++j) {
            MockColumn col;
            col.name = "COLUMN_" + std::to_string(j);
            col.data_type = (j % 3 == 0) ? SQL_INTEGER : SQL_VARCHAR;
            col.column_size = (j % 3 == 0) ? 10 : 50;
            col.decimal_digits = 0;
            col.nullable = (j == 1) ? SQL_NO_NULLS : SQL_NULLABLE;
            col.is_primary_key = (j == 1);
            col.is_auto_increment = (j == 1);
            table.columns.push_back(col);
        }
        
        tables_.push_back(table);
    }
}

const MockTable* MockCatalog::find_table(const std::string& name) const {
    std::string upper_name = to_upper(name);
    for (const auto& table : tables_) {
        if (to_upper(table.name) == upper_name) {
            return &table;
        }
    }
    return nullptr;
}

std::vector<MockColumn> MockCatalog::get_columns(const std::string& table_name,
                                                   const std::string& column_pattern) const {
    std::vector<MockColumn> result;
    const MockTable* table = find_table(table_name);
    if (!table) return result;
    
    for (const auto& col : table->columns) {
        if (matches_pattern(col.name, column_pattern)) {
            result.push_back(col);
        }
    }
    return result;
}

std::vector<MockColumn> MockCatalog::get_primary_keys(const std::string& table_name) const {
    std::vector<MockColumn> result;
    const MockTable* table = find_table(table_name);
    if (!table) return result;
    
    for (const auto& col : table->columns) {
        if (col.is_primary_key) {
            result.push_back(col);
        }
    }
    return result;
}

std::vector<std::pair<MockColumn, MockColumn>> MockCatalog::get_foreign_keys(
    const std::string& table_name) const {
    std::vector<std::pair<MockColumn, MockColumn>> result;
    const MockTable* table = find_table(table_name);
    if (!table) return result;
    
    for (const auto& col : table->columns) {
        if (!col.fk_table.empty()) {
            const MockTable* fk_table = find_table(col.fk_table);
            if (fk_table) {
                for (const auto& fk_col : fk_table->columns) {
                    if (fk_col.name == col.fk_column) {
                        result.push_back({col, fk_col});
                        break;
                    }
                }
            }
        }
    }
    return result;
}

std::vector<MockIndex> MockCatalog::get_statistics(const std::string& table_name) const {
    std::vector<MockIndex> result;
    std::string upper_name = to_upper(table_name);
    
    for (const auto& index : indexes_) {
        if (to_upper(index.table_name) == upper_name) {
            result.push_back(index);
        }
    }
    return result;
}

bool MockCatalog::matches_pattern(const std::string& value, const std::string& pattern) {
    if (pattern.empty() || pattern == "%") return true;
    
    std::string upper_value = to_upper(value);
    std::string upper_pattern = to_upper(pattern);
    
    // Simple pattern matching with % and _
    size_t v = 0, p = 0;
    size_t vlen = upper_value.length();
    size_t plen = upper_pattern.length();
    
    while (v < vlen && p < plen) {
        if (upper_pattern[p] == '%') {
            // Skip consecutive %
            while (p < plen && upper_pattern[p] == '%') ++p;
            if (p >= plen) return true;  // Trailing %
            
            // Find next match
            while (v < vlen) {
                if (matches_pattern(upper_value.substr(v), upper_pattern.substr(p))) {
                    return true;
                }
                ++v;
            }
            return false;
        } else if (upper_pattern[p] == '_') {
            // Match single character
            ++v;
            ++p;
        } else if (upper_pattern[p] == upper_value[v]) {
            ++v;
            ++p;
        } else {
            return false;
        }
    }
    
    // Handle trailing %
    while (p < plen && upper_pattern[p] == '%') ++p;
    
    return v == vlen && p == plen;
}

} // namespace mock_odbc
