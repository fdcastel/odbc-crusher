#pragma once

#include "../driver/common.hpp"
#include <functional>
#include <atomic>
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
// and return (success, affected_rows, error_message, output_values).
class MockCatalog;  // forward

// Parameter metadata for stored procedures (PORT plan §4.3 / port 8).
// Drives both SQLProcedureColumns enumeration AND the output-value writeback
// at SQLExecute time when direction is SQL_PARAM_OUTPUT / SQL_PARAM_INPUT_OUTPUT.
struct MockProcParam {
    std::string name;
    SQLSMALLINT direction;     // SQL_PARAM_INPUT / SQL_PARAM_OUTPUT / SQL_PARAM_INPUT_OUTPUT
                               // (or SQL_RETURN_VALUE for a function's `?=`).
    SQLSMALLINT sql_type;      // SQL_INTEGER / SQL_VARCHAR / SQL_DECIMAL / ...
    SQLULEN     column_size = 0;
    SQLSMALLINT scale = 0;
};

struct MockProcedureResult {
    bool success = true;
    SQLLEN affected_rows = -1;  // ODBC spec default for EXECUTE PROCEDURE
    std::string error_message;
    std::string error_sqlstate;
    // Output values indexed by parameter position (0-based, matches MockProcedure::params).
    // Slots for SQL_PARAM_INPUT params are ignored. Empty when the procedure
    // has no OUT/INOUT/RETURN parameters.
    std::vector<CellValue> output_values;
};
using MockProcedureCallback = std::function<MockProcedureResult(
    MockCatalog& catalog, const std::vector<CellValue>& args)>;

struct MockProcedure {
    std::string name;
    // Either set `params` for full IN/OUT/INOUT/RETURN metadata (drives
    // SQLProcedureColumns + output writeback), or set `input_param_count`
    // alone for a legacy IN-only procedure with no enumerable columns.
    std::vector<MockProcParam> params;
    SQLSMALLINT input_param_count = 0;
    MockProcedureCallback callback;
    std::string remarks;

    // Helper — count direction-specific parameters from `params`.
    SQLSMALLINT count_params_with_direction(SQLSMALLINT direction) const {
        SQLSMALLINT n = 0;
        for (const auto& p : params) {
            if (p.direction == direction) ++n;
        }
        return n;
    }
};

// The mock catalog
//
// Thread-safety: all public methods serialize on `mu_`.
//
// D5: there are no reference-returning accessors left. `tables()` and
// `inserted_data()` used to hand out references into vectors another
// connection can reallocate, and `find_table()` a pointer into one; all
// three now copy under the mutex, so a caller owns what it reads and no
// pointer outlives the call.
class MockCatalog {
public:
    static MockCatalog& instance();

    // Initialize catalog based on preset
    void initialize(const std::string& preset);

    // Connection bookkeeping - D47.
    //
    // The catalog is a process-global singleton, so its lifetime has to
    // be tied to something. D6 tied it to the preset, which stopped a
    // concurrent connect from wiping a live connection's tables but also
    // meant nothing ever reset it: a second connection opened after the
    // first had closed inherited the first one's schema, and CREATE TABLE
    // failed with 'already exists'. attach()/detach() tie it to the set of
    // *open* connections instead - shared while any are open, reset when
    // the last one closes.
    void attach(const std::string& preset);
    void detach();
    int live_connections() const { return live_connections_.load(); }

    // Table operations
    // D5: `tables()` and `inserted_data()` used to hand out references into
    // vectors another connection can reallocate, and the comment above them
    // said so. They had no callers outside mock_catalog.cpp, so they are
    // gone; `snapshot_tables()` and `snapshot_inserted_rows()` copy under the
    // mutex and are what a caller should use.
    std::vector<MockTable> snapshot_tables() const;
    // D5: this returned `const MockTable*` into `tables_`, and
    // `execute_query` held it across the whole executor while another
    // connection's CREATE TABLE reallocated the vector underneath it - a
    // use-after-free, not merely a race. It copies under the mutex now, so
    // the caller owns what it reads.
    std::optional<MockTable> find_table(const std::string& name) const;

    // Mutable catalog operations (for CREATE TABLE / DROP TABLE)
    void add_table(const MockTable& table);
    void remove_table(const std::string& name);

    // Mutable data operations (for INSERT / transaction support)
    void insert_row(const std::string& table_name, MockRow row);
    void clear_inserted_data();
    void clear_inserted_data(const std::string& table_name);
    // Returns a copy of one table's rows under the catalog mutex. Empty
    // vector when the table has no inserted data.
    std::vector<MockRow> snapshot_inserted_rows(const std::string& table_name) const;

    // Counts rows in `table_name` for which `predicate(row)` returns true.
    // Used by UPDATE/DELETE executors to compute SQLRowCount honestly
    // (instead of the previous hard-coded `1`). Predicate runs under the
    // catalog mutex — keep it side-effect-free.
    size_t count_matching_rows(
        const std::string& table_name,
        const std::function<bool(const MockRow&)>& predicate) const;

    // Erases rows in `table_name` for which `predicate(row)` returns true,
    // returning the number erased. Used by the DELETE executor.
    size_t erase_matching_rows(
        const std::string& table_name,
        const std::function<bool(const MockRow&)>& predicate);

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
    // D13: has this table's row store been created yet? A table with an
    // empty store has been emptied; a table with no store at all has
    // never been materialised, and a stock table then still generates.
    bool has_row_store(const std::string& table_name) const;
    // Create the row store from `rows` unless it already exists.
    // Returns true when it did the work.
    bool materialize_rows(const std::string& table_name,
                          std::vector<MockRow> rows);

    void register_procedure(MockProcedure procedure);
    std::optional<MockProcedure> find_procedure(const std::string& name) const;
    std::vector<MockProcedure> snapshot_procedures() const;

    // Pattern matching (SQL LIKE)
    // The escape character defaults to `\\`, which is what the driver
    // advertises through SQLGetInfo(SQL_SEARCH_PATTERN_ESCAPE) for the
    // catalog functions. D42: a LIKE predicate passes '\0' unless the
    // statement supplied an ESCAPE clause, because standard SQL gives LIKE no
    // escape character unless one is named.
    static bool matches_pattern(const std::string& value,
                                const std::string& pattern,
                                char escape_char = '\\');

private:
    MockCatalog() = default;
    void create_default_catalog();
    void create_empty_catalog();
    void create_large_catalog();

    // Guards every mutating path. See class comment.
    mutable std::mutex mu_;

    // D6: which preset is loaded, so re-initialising with the same one is a
    // no-op rather than a wipe.
    // D5: the unlocked finder, for the readers in this file that already
    // hold `mu_`. Never hand the pointer outside the lock.
    const MockTable* find_table_locked(const std::string& name) const;

    bool initialized_ = false;
    std::string loaded_preset_;
    std::atomic<int> live_connections_{0};   // D47

    std::vector<MockTable> tables_;
    std::vector<MockIndex> indexes_;
    std::unordered_map<std::string, std::vector<MockRow>> inserted_data_;
    std::vector<MockProcedure> procedures_;
};

} // namespace mock_odbc
