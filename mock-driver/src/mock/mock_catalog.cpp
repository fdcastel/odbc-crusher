#include "mock_catalog.hpp"
#include "behaviors.hpp"
#include <algorithm>
#include <cctype>

namespace mock_odbc {

namespace {

std::string to_upper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

} // anonymous namespace

MockCatalog& MockCatalog::instance() {
    static MockCatalog instance;
    return instance;
}

void MockCatalog::attach(const std::string& preset) {
    initialize(preset);
    live_connections_.fetch_add(1);
}

void MockCatalog::detach() {
    // Only the last connection out resets. fetch_sub returns the value
    // *before* the decrement, so > 1 means someone else is still open.
    const int before = live_connections_.fetch_sub(1);
    if (before <= 0) {
        live_connections_.store(0);   // unbalanced detach; do not go negative
        return;
    }
    if (before > 1) return;

    std::lock_guard<std::mutex> g(mu_);
    tables_.clear();
    indexes_.clear();
    inserted_data_.clear();
    procedures_.clear();
    initialized_ = false;
    loaded_preset_.clear();
}

void MockCatalog::initialize(const std::string& preset) {
    std::lock_guard<std::mutex> g(mu_);

    std::string lower_preset = preset;
    std::transform(lower_preset.begin(), lower_preset.end(), lower_preset.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // D6: this cleared everything on every call, and every SQLDriverConnect
    // calls it - so connection B's connect destroyed every table and row
    // connection A had created. The tool opens one connection per run today,
    // which is the only reason it has not bitten, but a probe that opens a
    // second connection to test isolation would have found its own schema
    // gone.
    //
    // Asking for the preset that is already loaded is now a no-op. That is
    // not the full fix - a genuinely per-connection catalog is - but it is
    // the difference between "a second connection is fatal" and "a second
    // connection asking for a *different* preset resets the shared state",
    // which is a documented consequence of the catalog being process-global
    // rather than a silent data loss.
    if (initialized_ && loaded_preset_ == lower_preset) {
        return;
    }

    tables_.clear();
    indexes_.clear();
    inserted_data_.clear();
    procedures_.clear();
    loaded_preset_ = lower_preset;
    initialized_ = true;

    if (lower_preset == "empty") {
        create_empty_catalog();
    } else if (lower_preset == "large") {
        create_large_catalog();
    } else {
        create_default_catalog();
    }

    // Register canonical procedures available in every preset. Tests rely on
    // INSERT_N_ROWS for the §1.8 SQLRowCount-after-EXECUTE-PROCEDURE probe.
    {
        MockProcedure insert_n;
        insert_n.name = "INSERT_N_ROWS";
        insert_n.input_param_count = 2;
        insert_n.remarks =
            "INSERT_N_ROWS(table_name VARCHAR, n INTEGER) — appends N rows "
            "with auto-incremented IDs into table_name. Used by the §1.8 "
            "SQLRowCount-after-EXECUTE-PROCEDURE probe.";
        insert_n.callback =
            [](MockCatalog& catalog,
               const std::vector<CellValue>& args) -> MockProcedureResult {
                MockProcedureResult res;
                if (args.size() < 2) {
                    res.success = false;
                    res.error_sqlstate = "42000";
                    res.error_message =
                        "INSERT_N_ROWS expects (table_name VARCHAR, n INTEGER)";
                    return res;
                }
                if (!std::holds_alternative<std::string>(args[0])) {
                    res.success = false;
                    res.error_sqlstate = "07006";
                    res.error_message = "INSERT_N_ROWS arg 0 must be VARCHAR";
                    return res;
                }
                if (!std::holds_alternative<long long>(args[1])) {
                    res.success = false;
                    res.error_sqlstate = "07006";
                    res.error_message = "INSERT_N_ROWS arg 1 must be INTEGER";
                    return res;
                }
                const std::string& table = std::get<std::string>(args[0]);
                long long n = std::get<long long>(args[1]);
                if (n < 0) n = 0;
                for (long long i = 1; i <= n; ++i) {
                    MockRow row;
                    row.push_back(static_cast<long long>(i));        // ID
                    row.push_back(std::string(std::to_string(i)));   // VAL
                    catalog.insert_row(table, std::move(row));
                }
                // Per ODBC spec, SQLRowCount after EXECUTE PROCEDURE is
                // commonly -1 ("driver doesn't know"). Use -1 here so
                // consumers that expect spec-baseline behaviour pass.
                res.affected_rows = -1;
                return res;
            };
        // Pre-existing lock is held; insert directly.
        procedures_.push_back(std::move(insert_n));
    }

    // PORT plan §4.3 / port 3 — IN/OUT/INOUT canonical procedure for the
    // {?=CALL …} escape probes. Three params: an IN integer that doubles
    // into the OUT integer, and an INOUT VARCHAR that gets uppercased.
    // The probes assert the OUT/INOUT slots are mutated post-execute.
    {
        MockProcedure inout;
        inout.name = "MOCK_INOUT";
        inout.params = {
            {"P_IN_INT",     SQL_PARAM_INPUT,         SQL_INTEGER, 10, 0},
            {"P_OUT_INT",    SQL_PARAM_OUTPUT,        SQL_INTEGER, 10, 0},
            {"P_INOUT_TEXT", SQL_PARAM_INPUT_OUTPUT,  SQL_VARCHAR, 64, 0},
        };
        inout.input_param_count = 3;  // total bound positions, IN+INOUT
        inout.remarks =
            "MOCK_INOUT(IN n INTEGER, OUT m INTEGER, INOUT s VARCHAR(64)) — "
            "sets m := n*2 and s := UPPER(s). Drives the PORT plan port 3 "
            "{?=CALL …} IN/OUT/INOUT escape probes.";
        inout.callback =
            [](MockCatalog&, const std::vector<CellValue>& args)
                -> MockProcedureResult {
                MockProcedureResult res;
                res.affected_rows = -1;

                // PORT plan port 3 canary — Procedures=BrokenInout: mimic
                // drivers that accept SQL_PARAM_OUTPUT/INPUT_OUTPUT bindings
                // syntactically but never write back. Empty output_values
                // means the SQLExecute writeback path becomes a no-op.
                if (BehaviorController::instance().config().procedures_broken_inout) {
                    return res;
                }

                res.output_values.resize(3);
                long long in_n = 0;
                if (args.size() > 0 && std::holds_alternative<long long>(args[0])) {
                    in_n = std::get<long long>(args[0]);
                }
                std::string in_s;
                if (args.size() > 2 && std::holds_alternative<std::string>(args[2])) {
                    in_s = std::get<std::string>(args[2]);
                }
                std::string upper = in_s;
                for (auto& c : upper) {
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                }
                // Slot 0 is IN — leave untouched (driver should not write back).
                res.output_values[1] = static_cast<long long>(in_n * 2);
                res.output_values[2] = upper;
                return res;
            };
        procedures_.push_back(std::move(inout));
    }

    // D13 — a callable *function*: `{?=CALL MOCK_FN(?, ?)}`.
    //
    // The mock had no function at all, only the MOCK_INOUT procedure, so the
    // shape the README advertises as the port-3 canary had nothing to run
    // against and A17/I7 stayed deferred. params[0] is the SQL_RETURN_VALUE
    // slot, which means the caller's first *argument* is parameter 2 - and a
    // driver that binds it as parameter 1 is the defect those probes look for.
    {
        MockProcedure fn;
        fn.name = "MOCK_FN";
        fn.params = {
            {"RETURN_VALUE", SQL_RETURN_VALUE, SQL_INTEGER, 10, 0},
            {"P_A",          SQL_PARAM_INPUT,  SQL_INTEGER, 10, 0},
            {"P_B",          SQL_PARAM_INPUT,  SQL_INTEGER, 10, 0},
        };
        // Bound positions the caller supplies as *arguments*; the return value
        // is bound too but is not an argument, hence 2 rather than 3.
        fn.input_param_count = 2;
        fn.remarks =
            "MOCK_FN(a INTEGER, b INTEGER) RETURNS INTEGER - returns a*10 + b. "
            "Drives the {?=CALL fn(...)} function-call escape: parameter 1 is "
            "the return value, so a is parameter 2 and b is parameter 3.";
        fn.callback =
            [](MockCatalog&, const std::vector<CellValue>& args)
                -> MockProcedureResult {
                MockProcedureResult res;
                res.affected_rows = -1;
                auto as_int = [](const CellValue& v) -> long long {
                    if (std::holds_alternative<long long>(v)) {
                        return std::get<long long>(v);
                    }
                    if (std::holds_alternative<double>(v)) {
                        return static_cast<long long>(std::get<double>(v));
                    }
                    return 0;
                };
                const long long a = args.size() > 0 ? as_int(args[0]) : 0;
                const long long b = args.size() > 1 ? as_int(args[1]) : 0;
                // a*10 + b, so an argument read into the wrong slot produces a
                // visibly wrong answer rather than a coincidentally right one.
                res.output_values.resize(3);
                res.output_values[0] = a * 10 + b;
                return res;
            };
        procedures_.push_back(std::move(fn));
    }
}

void MockCatalog::create_default_catalog() {
    // CUSTOMERS table (referenced by tests)
    MockTable customers;
    customers.catalog = "";
    customers.schema = "";
    customers.name = "CUSTOMERS";
    customers.type = "TABLE";
    customers.remarks = "Customer accounts";
    customers.columns = {
        {"CUSTOMER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"NAME", SQL_VARCHAR, 100, 0, SQL_NO_NULLS, false, false, "", "", ""},
        {"EMAIL", SQL_VARCHAR, 100, 0, SQL_NULLABLE, false, false, "", "", ""},
        {"CREATED_DATE", SQL_TYPE_DATE, 10, 0, SQL_NULLABLE, false, false, "", "", ""},
        {"IS_ACTIVE", SQL_BIT, 1, 0, SQL_NULLABLE, false, false, "1", "", ""},
        {"BALANCE", SQL_DECIMAL, 10, 2, SQL_NULLABLE, false, false, "0.00", "", ""}
    };
    tables_.push_back(customers);
    
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
    
    // ORDERS table (FK to CUSTOMERS)
    MockTable orders;
    orders.catalog = "";
    orders.schema = "";
    orders.name = "ORDERS";
    orders.type = "TABLE";
    orders.remarks = "Order records";
    orders.columns = {
        {"ORDER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, true, true, "", "", ""},
        {"CUSTOMER_ID", SQL_INTEGER, 10, 0, SQL_NO_NULLS, false, false, "", "CUSTOMERS", "CUSTOMER_ID"},
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
    MockIndex customers_pk;
    customers_pk.table_name = "CUSTOMERS";
    customers_pk.index_name = "PK_CUSTOMERS";
    customers_pk.non_unique = false;
    customers_pk.type = SQL_INDEX_CLUSTERED;
    customers_pk.columns = {"CUSTOMER_ID"};
    indexes_.push_back(customers_pk);
    
    MockIndex customers_email;
    customers_email.table_name = "CUSTOMERS";
    customers_email.index_name = "UQ_CUSTOMERS_EMAIL";
    customers_email.non_unique = false;
    customers_email.type = SQL_INDEX_OTHER;
    customers_email.columns = {"EMAIL"};
    indexes_.push_back(customers_email);
    
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

void MockCatalog::add_table(const MockTable& table) {
    std::lock_guard<std::mutex> g(mu_);
    tables_.push_back(table);
}

void MockCatalog::remove_table(const std::string& name) {
    std::lock_guard<std::mutex> g(mu_);
    std::string upper_name = to_upper(name);
    tables_.erase(
        std::remove_if(tables_.begin(), tables_.end(),
                       [&upper_name](const MockTable& t) { return to_upper(t.name) == upper_name; }),
        tables_.end());
    // Also remove inserted data and indexes for this table
    inserted_data_.erase(upper_name);
    indexes_.erase(
        std::remove_if(indexes_.begin(), indexes_.end(),
                       [&upper_name](const MockIndex& idx) { return to_upper(idx.table_name) == upper_name; }),
        indexes_.end());
}

void MockCatalog::insert_row(const std::string& table_name, MockRow row) {
    std::lock_guard<std::mutex> g(mu_);
    inserted_data_[to_upper(table_name)].push_back(std::move(row));
}

void MockCatalog::clear_inserted_data() {
    std::lock_guard<std::mutex> g(mu_);
    inserted_data_.clear();
}

void MockCatalog::clear_inserted_data(const std::string& table_name) {
    std::lock_guard<std::mutex> g(mu_);
    inserted_data_.erase(to_upper(table_name));
}

size_t MockCatalog::count_matching_rows(
    const std::string& table_name,
    const std::function<bool(const MockRow&)>& predicate) const
{
    std::lock_guard<std::mutex> g(mu_);
    auto it = inserted_data_.find(to_upper(table_name));
    if (it == inserted_data_.end()) return 0;
    size_t n = 0;
    for (const auto& row : it->second) {
        if (predicate(row)) ++n;
    }
    return n;
}

size_t MockCatalog::erase_matching_rows(
    const std::string& table_name,
    const std::function<bool(const MockRow&)>& predicate)
{
    std::lock_guard<std::mutex> g(mu_);
    auto it = inserted_data_.find(to_upper(table_name));
    if (it == inserted_data_.end()) return 0;
    auto& rows = it->second;
    auto first_keep = std::remove_if(rows.begin(), rows.end(),
        [&](const MockRow& r) { return predicate(r); });
    size_t erased = static_cast<size_t>(rows.end() - first_keep);
    rows.erase(first_keep, rows.end());
    return erased;
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

std::vector<MockTable> MockCatalog::snapshot_tables() const {
    std::lock_guard<std::mutex> g(mu_);
    return tables_;
}

std::vector<MockRow> MockCatalog::snapshot_inserted_rows(
    const std::string& table_name) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = inserted_data_.find(to_upper(table_name));
    if (it == inserted_data_.end()) return {};
    return it->second;
}

bool MockCatalog::has_row_store(const std::string& table_name) const {
    std::lock_guard<std::mutex> g(mu_);
    return inserted_data_.find(to_upper(table_name)) != inserted_data_.end();
}

bool MockCatalog::materialize_rows(const std::string& table_name,
                                   std::vector<MockRow> rows) {
    std::lock_guard<std::mutex> g(mu_);
    const std::string key = to_upper(table_name);
    if (inserted_data_.find(key) != inserted_data_.end()) return false;
    inserted_data_[key] = std::move(rows);
    return true;
}

void MockCatalog::register_procedure(MockProcedure procedure) {
    std::lock_guard<std::mutex> g(mu_);
    procedures_.push_back(std::move(procedure));
}

std::optional<MockProcedure> MockCatalog::find_procedure(
    const std::string& name) const {
    std::lock_guard<std::mutex> g(mu_);
    std::string upper_name = to_upper(name);
    for (const auto& p : procedures_) {
        if (to_upper(p.name) == upper_name) {
            return p;  // by-value copy; lock released on return
        }
    }
    return std::nullopt;
}

std::vector<MockProcedure> MockCatalog::snapshot_procedures() const {
    std::lock_guard<std::mutex> g(mu_);
    return procedures_;
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
        if (upper_pattern[p] == '\\' && p + 1 < plen) {
            // D25: `\\` is the escape character this driver advertises
            // through SQLGetInfo(SQL_SEARCH_PATTERN_ESCAPE), and the matcher
            // ignored it - so a caller asking for a name containing a literal
            // `%` or `_` was handed the wildcard meaning and got every name
            // back. An escaped character matches itself and nothing else.
            if (upper_value[v] != upper_pattern[p + 1]) return false;
            ++v;
            p += 2;
        } else if (upper_pattern[p] == '%') {
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
