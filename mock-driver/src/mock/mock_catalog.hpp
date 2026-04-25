#pragma once

#include "../driver/common.hpp"
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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

// Stored-procedure registry entry. The body is a C++ callback rather than a
// PSM-like body language — keeps the mock minimal. Callbacks receive the
// parsed argument values (already substituted from any parameter markers)
// and return (success, affected_rows, error_message).
class MockCatalog;  // forward
struct MockProcedureResult {
    bool success = true;
    SQLLEN affected_rows = -1;  // ODBC spec default for EXECUTE PROCEDURE
    std::string error_message;
    std::string error_sqlstate;
};
using MockProcedureCallback = std::function<MockProcedureResult(
    MockCatalog& catalog, const std::vector<CellValue>& args)>;

struct MockProcedure {
    std::string name;
    SQLSMALLINT input_param_count = 0;
    MockProcedureCallback callback;
    std::string remarks;
};

// The mock catalog
//
// Thread-safety: all public methods serialize on `mu_`. The getters that
// still hand out references (`tables()`, `inserted_data()`) are kept for
// callers that already hold the driver's per-handle lock and run under the
// single-connection assumption. New call sites should prefer the copying
// `snapshot_*` accessors when crossing thread boundaries.
class MockCatalog {
public:
    static MockCatalog& instance();

    // Initialize catalog based on preset
    void initialize(const std::string& preset);

    // Table operations
    //
    // `tables()` and `inserted_data()` return references for backward-compat
    // and are SAFE only when the caller holds the driver's per-handle lock
    // and there is exactly one connection mutating the catalog. New code
    // should prefer `snapshot_tables()` / `snapshot_inserted_rows()` which
    // copy under the catalog mutex and are safe across concurrent
    // connections.
    const std::vector<MockTable>& tables() const { return tables_; }
    std::vector<MockTable> snapshot_tables() const;
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
    // Returns a copy of one table's rows under the catalog mutex. Empty
    // vector when the table has no inserted data.
    std::vector<MockRow> snapshot_inserted_rows(const std::string& table_name) const;

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

    // Stored procedure operations. `find_procedure` returns a *copy* so the
    // caller can invoke the callback without holding the catalog lock —
    // callbacks call back into MockCatalog (insert_row etc.) and would
    // deadlock if the caller still held mu_.
    void register_procedure(MockProcedure procedure);
    std::optional<MockProcedure> find_procedure(const std::string& name) const;
    std::vector<MockProcedure> snapshot_procedures() const;

    // Pattern matching (SQL LIKE)
    static bool matches_pattern(const std::string& value, const std::string& pattern);

private:
    MockCatalog() = default;
    void create_default_catalog();
    void create_empty_catalog();
    void create_large_catalog();

    // Guards every mutating path. See class comment.
    mutable std::mutex mu_;

    std::vector<MockTable> tables_;
    std::vector<MockIndex> indexes_;
    std::unordered_map<std::string, std::vector<MockRow>> inserted_data_;
    std::vector<MockProcedure> procedures_;
};

} // namespace mock_odbc
