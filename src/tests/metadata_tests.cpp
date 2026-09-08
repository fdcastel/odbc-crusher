#include "metadata_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <optional>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> MetadataTests::run() {
    return {
        test_tables_catalog(),
        test_columns_catalog(),
        test_primary_keys(),
        test_foreign_keys(),
        test_statistics(),
        test_special_columns(),
        test_table_privileges(),
        test_desc_unsigned_on_signed_integer(),
        test_count_star_result_metadata(),
        test_sqlprocedures_smoke(),
        test_sqlprocedurecolumns_smoke()
    };
}

TestResult MetadataTests::test_tables_catalog() {
    return run_test(
        "test_tables_catalog", "SQLTables",
        "List tables in the database",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLTables",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Call SQLTables to list all tables
            SQLRETURN ret = SQLTables(
                stmt.get_handle(),
                nullptr, 0,        // Catalog name (NULL = all)
                nullptr, 0,        // Schema name (NULL = all)
                nullptr, 0,        // Table name (NULL = all)
                (SQLCHAR*)"TABLE", SQL_NTS  // Table type
            );

            if (SQL_SUCCEEDED(ret)) {
                // Count how many tables we can find
                int table_count = 0;
                while (stmt.fetch() && table_count < 100) {  // Limit to avoid excessive output
                    table_count++;
                }

                std::ostringstream oss;
                oss << "Found " << table_count << " table(s)";
                r.actual = oss.str();
                r.status = TestStatus::PASS;
            } else {
                // B1: was SKIP_INCONCLUSIVE - "not supported or failed",
                // which conflates the two. SQLTables is Core; every driver
                // must implement it, and an empty catalog is reported by
                // returning no rows rather than by failing the call. B3 reads
                // the SQLSTATE so a driver that genuinely says HYC00 still
                // gets a skip, and anything else is the failure it is.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLTables");
            }
        });
}

TestResult MetadataTests::test_columns_catalog() {
    return run_test(
        "test_columns_catalog", "SQLColumns",
        "List columns from system tables",
        Severity::INFO, ConformanceLevel::CORE, "ODBC 3.8 SQLColumns",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Strategy 1: Discover a real table via SQLTables, capturing both
            // catalog and table name, then query SQLColumns with both.
            // This avoids hard-coding schema/catalog assumptions that break on
            // drivers that use catalogs (MySQL/MariaDB) vs schemas (SQL Server).
            // C7: was a hand-rolled SQLTables walk with its own
            // DiscoveredTable struct, three fixed-size buffers and no 01004
            // loop - one of five such copies. The helper reads each column
            // with get_data_full, so a table name longer than 128 bytes is
            // no longer silently cut.
            std::vector<DiscoveredTable> discovered = discover_tables(5);

            // Strategy 2: Also try well-known system tables / mock tables with
            // different catalog/schema arrangements.
            // For MySQL/MariaDB: 'information_schema' is a catalog, not a schema.
            // For SQL Server: 'sys' is a schema.
            // For Firebird: no catalog/schema, just table name.
            struct StaticTable {
                std::string catalog;
                std::string schema;
                std::string name;
            };
            std::vector<StaticTable> static_tables = {
                {"information_schema", "", "TABLES"},       // MySQL/MariaDB (database=catalog)
                {"", "information_schema", "TABLES"},       // Fallback (schema-based)
                {"", "sys", "tables"},                      // SQL Server
                {"", "", "RDB$DATABASE"},                   // Firebird
                {"", "", "CUSTOMERS"},                      // Mock driver
                {"", "", "USERS"},                          // Mock driver
            };

            bool success = false;
            int column_count = 0;

            // Try discovered tables first (these have the correct catalog/schema)
            for (const auto& dt : discovered) {
                try {
                    stmt.recycle();
                    SQLRETURN ret = SQLColumns(
                        stmt.get_handle(),
                        dt.catalog.empty() ? nullptr : (SQLCHAR*)dt.catalog.c_str(),
                        dt.catalog.empty() ? 0 : SQL_NTS,
                        dt.schema.empty() ? nullptr : (SQLCHAR*)dt.schema.c_str(),
                        dt.schema.empty() ? 0 : SQL_NTS,
                        (SQLCHAR*)dt.name.c_str(), SQL_NTS,
                        nullptr, 0
                    );

                    if (SQL_SUCCEEDED(ret)) {
                        while (stmt.fetch() && column_count < 50) {
                            column_count++;
                        }
                        if (column_count > 0) {
                            success = true;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            // If discovered tables didn't work, try static table list
            if (!success) {
                for (const auto& st : static_tables) {
                    try {
                        stmt.recycle();
                        column_count = 0;
                        SQLRETURN ret = SQLColumns(
                            stmt.get_handle(),
                            st.catalog.empty() ? nullptr : (SQLCHAR*)st.catalog.c_str(),
                            st.catalog.empty() ? 0 : SQL_NTS,
                            st.schema.empty() ? nullptr : (SQLCHAR*)st.schema.c_str(),
                            st.schema.empty() ? 0 : SQL_NTS,
                            (SQLCHAR*)st.name.c_str(), SQL_NTS,
                            nullptr, 0
                        );

                        if (SQL_SUCCEEDED(ret)) {
                            while (stmt.fetch() && column_count < 50) {
                                column_count++;
                            }
                            if (column_count > 0) {
                                success = true;
                                break;
                            }
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }
            }

            if (success) {
                std::ostringstream oss;
                oss << "Found " << column_count << " column(s) from system table";
                r.actual = oss.str();
                r.status = TestStatus::PASS;
            } else {
                r.actual = "SQLColumns callable but no system tables accessible";
                // B1: kept as a skip, but for the right reason. SQLColumns
                // succeeded and returned nothing, which is a statement about
                // the *catalog* this account can see, not about the driver -
                // there is nothing to fail. The Core-ness of SQLColumns is
                // covered by the call itself succeeding above.
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLColumns executed but no columns found in tested system tables";
            }
        });
}

TestResult MetadataTests::test_primary_keys() {
    return run_test(
        "test_primary_keys", "SQLPrimaryKeys",
        "Query primary key information",
        Severity::INFO, ConformanceLevel::LEVEL_1, "ODBC 3.8 SQLPrimaryKeys",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // A24: this list is the whole problem. RDB$DATABASE is Firebird,
            // information_schema.TABLES is MySQL and SQL Server, sys.tables is
            // SQL Server - none of them exists on PostgreSQL, DuckDB or
            // ClickHouse, so on those engines every attempt returned nothing
            // and the probe passed as "callable (nothing found)" without ever
            // having seen a table. Ask the driver what it has first (C7), and
            // keep the well-known names as a fallback for a catalog that
            // reports none.
            std::vector<std::pair<std::string, std::string>> test_tables;
            for (const auto& t : discover_tables(3)) {
                test_tables.emplace_back(t.schema, t.name);
            }
            test_tables.emplace_back("", "RDB$DATABASE");
            test_tables.emplace_back("information_schema", "TABLES");
            test_tables.emplace_back("sys", "tables");

            bool callable = false;

            for (const auto& [schema, table] : test_tables) {
                try {
                    stmt.recycle();
                    SQLRETURN ret = SQLPrimaryKeys(
                        stmt.get_handle(),
                        nullptr, 0,
                        schema.empty() ? nullptr : (SQLCHAR*)schema.c_str(),
                        schema.empty() ? 0 : SQL_NTS,
                        (SQLCHAR*)table.c_str(), SQL_NTS
                    );

                    // Even if there are no primary keys, if the function succeeds, it's callable
                    if (SQL_SUCCEEDED(ret)) {
                        callable = true;

                        int pk_count = 0;
                        while (stmt.fetch() && pk_count < 10) {
                            pk_count++;
                        }

                        if (pk_count > 0) {
                            std::ostringstream oss;
                            oss << "Found " << pk_count << " primary key column(s)";
                            r.actual = oss.str();
                            r.status = TestStatus::PASS;
                            break;
                        }
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!callable) {
                // B1/B4: was an unconditional SKIP_UNSUPPORTED, which said
                // "not supported" for every possible failure - a permissions
                // error, a malformed catalog, a driver bug. `stmt` is still
                // alive here, so B3 can read what the last attempt actually
                // returned: HYC00 or IM001 still skip, anything else is the
                // failure it is.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLPrimaryKeys");
            } else if (r.actual.empty()) {
                r.actual = "SQLPrimaryKeys callable (no PKs in queried tables)";
                r.status = TestStatus::PASS;
            }
        });
}

TestResult MetadataTests::test_statistics() {
    return run_test(
        "test_statistics", "SQLStatistics",
        "Query index/statistics information",
        Severity::INFO, ConformanceLevel::LEVEL_1, "ODBC 3.8 SQLStatistics",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // A24: this list is the whole problem. RDB$DATABASE is Firebird,
            // information_schema.TABLES is MySQL and SQL Server, sys.tables is
            // SQL Server - none of them exists on PostgreSQL, DuckDB or
            // ClickHouse, so on those engines every attempt returned nothing
            // and the probe passed as "callable (nothing found)" without ever
            // having seen a table. Ask the driver what it has first (C7), and
            // keep the well-known names as a fallback for a catalog that
            // reports none.
            std::vector<std::pair<std::string, std::string>> test_tables;
            for (const auto& t : discover_tables(3)) {
                test_tables.emplace_back(t.schema, t.name);
            }
            test_tables.emplace_back("", "RDB$DATABASE");
            test_tables.emplace_back("information_schema", "TABLES");

            bool callable = false;

            for (const auto& [schema, table] : test_tables) {
                try {
                    stmt.recycle();
                    SQLRETURN ret = SQLStatistics(
                        stmt.get_handle(),
                        nullptr, 0,
                        schema.empty() ? nullptr : (SQLCHAR*)schema.c_str(),
                        schema.empty() ? 0 : SQL_NTS,
                        (SQLCHAR*)table.c_str(), SQL_NTS,
                        SQL_INDEX_ALL,      // All indexes
                        SQL_QUICK           // Don't guarantee accuracy
                    );

                    if (SQL_SUCCEEDED(ret)) {
                        callable = true;

                        int stat_count = 0;
                        while (stmt.fetch() && stat_count < 20) {
                            stat_count++;
                        }

                        std::ostringstream oss;
                        if (stat_count > 0) {
                            oss << "Found " << stat_count << " statistic(s)/index(es)";
                        } else {
                            oss << "SQLStatistics callable (no statistics in test table)";
                        }
                        r.actual = oss.str();
                        r.status = TestStatus::PASS;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!callable) {
                // B1/B4: was an unconditional SKIP_UNSUPPORTED, which said
                // "not supported" for every possible failure - a permissions
                // error, a malformed catalog, a driver bug. `stmt` is still
                // alive here, so B3 can read what the last attempt actually
                // returned: HYC00 or IM001 still skip, anything else is the
                // failure it is.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLStatistics");
            }
        });
}

TestResult MetadataTests::test_special_columns() {
    return run_test(
        "test_special_columns", "SQLSpecialColumns",
        "Query special columns (row identifiers)",
        Severity::INFO, ConformanceLevel::LEVEL_1, "ODBC 3.8 SQLSpecialColumns",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Strategy 1: Dynamically discover a base table via SQLTables.
            // We want a TABLE (not VIEW) because SQLSpecialColumns with
            // SQL_BEST_ROWID is meaningful on base tables with primary keys.
            // C7 - see test_tables_catalog. Second copy of the same walk.
            std::vector<DiscoveredTable> discovered = discover_tables(5);

                        std::vector<DiscoveredTable> test_tables;
            for (auto& d : discovered) test_tables.push_back(std::move(d));
            test_tables.push_back({"", "",  "RDB$DATABASE"});                // Firebird
            test_tables.push_back({"", "pg_catalog", "pg_class"});           // PostgreSQL
            test_tables.push_back({"", "pg_catalog", "pg_type"});            // PostgreSQL
            test_tables.push_back({"", "information_schema", "TABLES"});     // MySQL / SQL Server
            test_tables.push_back({"information_schema", "", "TABLES"});     // MySQL (catalog)
            test_tables.push_back({"", "dbo", "sysobjects"});               // SQL Server

            bool callable = false;

            for (const auto& tbl : test_tables) {
                try {
                    stmt.recycle();
                    SQLRETURN ret = SQLSpecialColumns(
                        stmt.get_handle(),
                        SQL_BEST_ROWID,     // Best row identifier
                        tbl.catalog.empty() ? nullptr : (SQLCHAR*)tbl.catalog.c_str(),
                        tbl.catalog.empty() ? 0 : SQL_NTS,
                        tbl.schema.empty() ? nullptr : (SQLCHAR*)tbl.schema.c_str(),
                        tbl.schema.empty() ? 0 : SQL_NTS,
                        (SQLCHAR*)tbl.name.c_str(), SQL_NTS,
                        SQL_SCOPE_SESSION,  // Valid for session
                        SQL_NULLABLE        // Include nullable columns
                    );

                    if (SQL_SUCCEEDED(ret)) {
                        callable = true;

                        int col_count = 0;
                        while (stmt.fetch() && col_count < 10) {
                            col_count++;
                        }

                        std::ostringstream oss;
                        if (col_count > 0) {
                            oss << "Found " << col_count << " special column(s)";
                        } else {
                            oss << "SQLSpecialColumns callable (no special columns)";
                        }
                        r.actual = oss.str();
                        r.status = TestStatus::PASS;
                        break;
                    }
                } catch (const core::OdbcError&) {
                    continue;
                }
            }

            if (!callable) {
                // B1/B4: was an unconditional SKIP_UNSUPPORTED, which said
                // "not supported" for every possible failure - a permissions
                // error, a malformed catalog, a driver bug. `stmt` is still
                // alive here, so B3 can read what the last attempt actually
                // returned: HYC00 or IM001 still skip, anything else is the
                // failure it is.
                report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                               "SQLSpecialColumns");
            }
        });
}

TestResult MetadataTests::test_foreign_keys() {
    // B4: the inner catch used to downgrade *any* OdbcError to
    // SKIP_UNSUPPORTED with a custom message - a failed SQLAllocHandle was
    // reported as "Foreign keys not supported by driver". It now reads the
    // exception's SQLSTATE and only HYC00 / IM001 earn the skip.
    return run_test(
        "test_foreign_keys", "SQLForeignKeys",
        "Retrieve foreign key relationships",
        Severity::INFO, ConformanceLevel::LEVEL_1, "ODBC 3.8 SQLForeignKeys",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                // Try to get foreign keys using different approaches
                // ODBC spec requires either PK or FK table name
                bool success = false;
                bool callable = false;
                int fk_count = 0;

                // Discover actual user tables from the database
                std::vector<std::string> user_tables;
                try {
                    core::OdbcStatement tbl_stmt(conn_);
                    SQLRETURN tbl_ret = SQLTables(tbl_stmt.get_handle(),
                        nullptr, 0, nullptr, 0, nullptr, 0,
                        (SQLCHAR*)"TABLE", SQL_NTS);
                    if (SQL_SUCCEEDED(tbl_ret)) {
                        core::GuardedBuffer<char> name_buf(128, 0);  // D62
                        SQLLEN ind = 0;
                        while (SQL_SUCCEEDED(SQLFetch(tbl_stmt.get_handle()))
                               && user_tables.size() < 20) {
                            if (SQL_SUCCEEDED(SQLGetData(tbl_stmt.get_handle(), 3,
                                    SQL_C_CHAR, name_buf.data(), name_buf.declared_bytes(), &ind))
                                && ind > 0) {
                                // D68: to `ind`. These names are handed
                                // straight to SQLColumns below.
                                user_tables.emplace_back(core::bounded_string(
                                    name_buf.data(), name_buf.declared_elements(),
                                    ind).value);
                            }
                        }
                    }
                } catch (...) {}

                // Build table list: discovered tables first, then well-known names
                std::vector<std::string> fk_tables;
                for (const auto& t : user_tables) fk_tables.push_back(t);
                fk_tables.push_back("ORDERS");
                fk_tables.push_back("ORDER_ITEMS");

                // Strategy 1: Try each table as FK table
                for (const auto& fk_tbl : fk_tables) {
                    try {
                        stmt.recycle();
                        SQLRETURN ret = SQLForeignKeys(
                            stmt.get_handle(),
                            nullptr, 0,     // PK Catalog
                            nullptr, 0,     // PK Schema
                            nullptr, 0,     // PK Table
                            nullptr, 0,     // FK Catalog
                            nullptr, 0,     // FK Schema
                            (SQLCHAR*)fk_tbl.c_str(), SQL_NTS  // FK Table
                        );

                        if (SQL_SUCCEEDED(ret)) {
                            callable = true;  // Function works even if 0 rows returned
                            while (stmt.fetch() && fk_count < 100) {
                                fk_count++;
                            }
                            if (fk_count > 0) {
                                success = true;
                                break;
                            }
                        }
                    } catch (const core::OdbcError&) {
                        continue;
                    }
                }

                // Strategy 2: Try with all NULLs (some drivers support this)
                if (!success && !callable) {
                    try {
                        stmt.recycle();
                        SQLRETURN ret = SQLForeignKeys(
                            stmt.get_handle(),
                            nullptr, 0,     // PK Catalog
                            nullptr, 0,     // PK Schema
                            nullptr, 0,     // PK Table
                            nullptr, 0,     // FK Catalog
                            nullptr, 0,     // FK Schema
                            nullptr, 0      // FK Table
                        );

                        if (SQL_SUCCEEDED(ret)) {
                            callable = true;
                            while (stmt.fetch() && fk_count < 100) {
                                fk_count++;
                            }
                            if (fk_count > 0) {
                                success = true;
                            }
                        }
                    } catch (const core::OdbcError&) {
                        // Ignore - some drivers don't support all-NULLs
                    }
                }

                if (success || fk_count > 0) {
                    std::ostringstream oss;
                    oss << "Found " << fk_count << " foreign key(s)";
                    r.actual = oss.str();
                    r.status = TestStatus::PASS;
                } else if (callable) {
                    r.actual = "SQLForeignKeys callable (no foreign keys in database)";
                    r.status = TestStatus::PASS;
                } else {
                // B1/B4: was an unconditional SKIP_UNSUPPORTED, which said
                // "not supported" for every possible failure - a permissions
                // error, a malformed catalog, a driver bug. `stmt` is still
                // alive here, so B3 can read what the last attempt actually
                // returned: HYC00 or IM001 still skip, anything else is the
                // failure it is.
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLForeignKeys");
                }
            } catch (const core::OdbcError& e) {
                // B1/B4: an exception here is not evidence of an unimplemented
                // function either. run_test's own OdbcError handler would set
                // ERR; this keeps the SQLSTATE-based classification instead,
                // which is the only thing that can tell "declined" from
                // "broken".
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = std::string("SQLForeignKeys threw: ") + e.what();
                r.diagnostic = e.format_diagnostics();
                // The statement is scoped inside the try, so the SQLSTATE has
                // to come from the exception rather than from a handle.
                const std::string state = e.diagnostics().empty()
                                        ? std::string()
                                        : e.diagnostics()[0].sqlstate;
                if (state == "HYC00" || state == "IM001") {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLForeignKeys is not implemented (" + state + ")";
                }
            }
        });
}

TestResult MetadataTests::test_table_privileges() {
    // B4 - see test_foreign_keys. Same downgrade, same fix.
    return run_test(
        "test_table_privileges", "SQLTablePrivileges",
        "Query table access privileges",
        Severity::INFO, ConformanceLevel::LEVEL_2, "ODBC 3.8 SQLTablePrivileges",
        [&](TestResult& r) {
            try {
                core::OdbcStatement stmt(conn_);

                // Try to get table privileges
                SQLRETURN ret = SQLTablePrivileges(
                    stmt.get_handle(),
                    nullptr, 0,     // Catalog
                    nullptr, 0,     // Schema
                    nullptr, 0      // Table
                );

                if (SQL_SUCCEEDED(ret)) {
                    int priv_count = 0;
                    while (stmt.fetch() && priv_count < 100) {
                        priv_count++;
                    }

                    std::ostringstream oss;
                    oss << "Found " << priv_count << " table privilege(s)";
                    r.actual = oss.str();
                    r.status = TestStatus::PASS;
                } else {
                // B1/B4: was an unconditional SKIP_UNSUPPORTED, which said
                // "not supported" for every possible failure - a permissions
                // error, a malformed catalog, a driver bug. `stmt` is still
                // alive here, so B3 can read what the last attempt actually
                // returned: HYC00 or IM001 still skip, anything else is the
                // failure it is.
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLTablePrivileges");
                }
            } catch (const core::OdbcError& e) {
                // B1/B4 - see test_foreign_keys.
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = std::string("SQLTablePrivileges threw: ") + e.what();
                r.diagnostic = e.format_diagnostics();
                // The statement is scoped inside the try, so the SQLSTATE has
                // to come from the exception rather than from a handle.
                const std::string state = e.diagnostics().empty()
                                        ? std::string()
                                        : e.diagnostics()[0].sqlstate;
                if (state == "HYC00" || state == "IM001") {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLTablePrivileges is not implemented (" +
                               state + ")";
                }
            }
        });
}

// ── §1.10: SQL_DESC_UNSIGNED sanity on signed numeric columns ──────────────
//
// IMPROVEMENT_PLAN.md §1.10. DuckDB ODBC reports `SQL_DESC_UNSIGNED = 1`
// for INT128 / HUGEINT columns while serving negative values, which fooled
// `odbc-scanner` tests that match on `(SQL_BIGINT, signed)`. This probe
// runs `SELECT CAST(1 AS INTEGER)` and asserts SQL_DESC_UNSIGNED is
// SQL_FALSE, with fallbacks for engines that can't cast literals.
TestResult MetadataTests::test_desc_unsigned_on_signed_integer() {
    return run_test(
        "test_desc_unsigned_on_signed_integer", "SQLColAttribute",
        "SQL_DESC_UNSIGNED == SQL_FALSE for a signed INTEGER column",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLColAttribute, Appendix D: SQL_DESC_UNSIGNED",
        [&](TestResult& r) {
            // Try a sequence of known-portable INTEGER queries. The first one that
            // executes is enough — we just need a cursor open on a signed integer
            // column so we can call SQLColAttribute on it.
            const std::vector<std::string> queries = {
                "SELECT CAST(1 AS INTEGER)",
                "SELECT CAST(1 AS INTEGER) FROM RDB$DATABASE",   // Firebird
                "SELECT CAST(1 AS INTEGER) FROM DUAL",           // Oracle
            };

            core::OdbcStatement stmt(conn_);

            SQLRETURN exec_rc = SQL_ERROR;
            std::string used_query;
            for (const auto& q : queries) {
                try {
                    stmt.execute(q);
                    exec_rc = SQL_SUCCESS;
                    used_query = q;
                    break;
                } catch (const core::OdbcError&) {
                    // try next
                }
            }
            if (!SQL_SUCCEEDED(exec_rc)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No portable `CAST(1 AS INTEGER)` query succeeded";
                r.suggestion = "Driver may not accept inline CAST literals; rerun "
                                    "against a connection that has a known signed "
                                    "INTEGER column.";
                return;
            }

            SQLLEN unsigned_attr = -1;
            SQLRETURN col_rc = SQLColAttribute(
                stmt.get_handle(), 1, SQL_DESC_UNSIGNED,
                nullptr, 0, nullptr, &unsigned_attr);

            if (!SQL_SUCCEEDED(col_rc)) {
                // B4: "does not implement" was asserted from the call having
                // failed. SQL_DESC_UNSIGNED is a descriptor field a driver may
                // decline, so a skip can be right - but only when the driver
                // says HYC00 or HY091, not for a broken statement handle.
                const std::string state = first_sqlstate(
                    SQL_HANDLE_STMT, stmt.get_handle(), "");
                if (state == "HYC00" || state == "HY091" || state == "IM001") {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "SQLColAttribute(SQL_DESC_UNSIGNED) declined "
                               "with " + state;
                    r.suggestion = "Callers cannot rely on SQL_DESC_UNSIGNED "
                                   "with this driver; treat numeric columns as "
                                   "signed unless it says otherwise.";
                } else {
                    report_failure(r, SQL_HANDLE_STMT, stmt.get_handle(),
                                   "SQLColAttribute(SQL_DESC_UNSIGNED)");
                }
                return;
            }

            if (unsigned_attr == SQL_FALSE) {
                std::ostringstream actual;
                actual << "Query `" << used_query
                       << "` returned SQL_DESC_UNSIGNED = SQL_FALSE";
                r.actual = actual.str();
            } else {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                std::ostringstream actual;
                actual << "Query `" << used_query
                       << "` returned SQL_DESC_UNSIGNED = " << unsigned_attr
                       << " (expected SQL_FALSE/0 for a signed INTEGER literal)";
                r.actual = actual.str();
                r.suggestion = "Driver reports a signed INTEGER as unsigned — "
                                    "this confuses scanner-style consumers that key "
                                    "on `(type, signed)`. See DuckDB ODBC HUGEINT bug.";
            }
        });
}

// ── §1.9: COUNT(*) result-metadata probe ──────────────────────────────────
//
// IMPROVEMENT_PLAN.md §1.9. `SELECT COUNT(*) FROM t` returns different
// types across engines:
//   - DuckDB ODBC: SQL_BIGINT
//   - Firebird:    SQL_NUMERIC, precision 18
//   - Oracle:      SQL_NUMERIC, precision 22+
//   - SQL Server:  SQL_INTEGER (or SQL_BIGINT for COUNT_BIG)
// A consumer that hard-codes one type breaks on every other driver. This
// probe is informational — it always PASSes and dumps the `(type,
// precision, scale, unsigned)` tuple into `actual` so a driver developer
// can read the report and update their consumer code.
TestResult MetadataTests::test_count_star_result_metadata() {
    return run_test(
        "test_count_star_result_metadata", "SQLDescribeCol/SQLColAttribute",
        "Record COUNT(*) result column's (type, precision, scale, unsigned) tuple",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeCol, SQLColAttribute, Appendix D",
        [&](TestResult& r) {
            // Find any table to count. SQLTables yields a portable list; avoids
            // hard-coding mock-driver names (CUSTOMERS) so the probe runs on real
            // drivers too.
            std::string target_table;
            try {
                core::OdbcStatement enum_stmt(conn_);
                SQLRETURN rc = SQLTables(enum_stmt.get_handle(),
                                         nullptr, 0,
                                         nullptr, 0,
                                         nullptr, 0,
                                         (SQLCHAR*)"TABLE", SQL_NTS);
                if (SQL_SUCCEEDED(rc) && enum_stmt.fetch()) {
                    core::GuardedBuffer<char> buf(256, 0);  // D62
                    SQLLEN ind = 0;
                    // Column 3 is TABLE_NAME per ODBC spec.
                    //
                    // D68: this used to take the name by walking to a
                    // terminator, and then builds `SELECT COUNT(*) FROM
                    // <name>` out of it - so a driver that omits the NUL did
                    // not make the probe report something odd, it made the
                    // probe execute different SQL. Under
                    // BufferValidation=Lenient the query became
                    // `SELECT COUNT(*) FROM CUSTOMERSX` and the probe skipped
                    // itself for a table that does not exist.
                    //
                    // D66's family too: the return code was discarded, so a
                    // refused SQLGetData left `buf` zeroed and the probe read
                    // its own initialisation.
                    const SQLRETURN got = SQLGetData(enum_stmt.get_handle(), 3,
                                                     SQL_C_CHAR, buf.data(),
                                                     buf.declared_bytes(), &ind);
                    if (SQL_SUCCEEDED(got) && ind != SQL_NULL_DATA) {
                        target_table = bounded_string(buf.data(), buf.declared_elements(),
                                                      ind).value;
                    }
                }
            } catch (const core::OdbcError&) {
                // Fall through — we'll handle empty target_table below.
            }

            if (target_table.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLTables returned no tables; cannot probe COUNT(*) metadata";
                return;
            }

            core::OdbcStatement stmt(conn_);
            std::string query = "SELECT COUNT(*) FROM " + target_table;
            try {
                stmt.execute(query);
            } catch (const core::OdbcError& e) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Failed to execute `" + query + "`: " + e.what();
                return;
            }

            SQLSMALLINT sql_type = 0;
            SQLULEN col_size = 0;
            SQLSMALLINT scale = 0;
            SQLSMALLINT nullable = 0;
            core::GuardedBuffer<SQLCHAR> col_name(256, 0);  // D62
            SQLSMALLINT col_name_len = 0;

            SQLRETURN rc = SQLDescribeCol(stmt.get_handle(), 1,
                                          col_name.data(), col_name.declared_bytes(), &col_name_len,
                                          &sql_type, &col_size, &scale, &nullable);

            SQLLEN unsigned_attr = -1;
            SQLColAttribute(stmt.get_handle(), 1, SQL_DESC_UNSIGNED,
                            nullptr, 0, nullptr, &unsigned_attr);

            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLDescribeCol returned " + std::to_string(rc);
                return;
            }

            // B1/B2: the row B2 names by name. What COUNT(*) is typed as is
            // left to the engine - SQL_INTEGER, SQL_BIGINT and SQL_NUMERIC
            // are all in use, with whatever precision goes with them - so
            // there is no right answer to grade, only an answer worth
            // recording so a consumer can size its buffer.
            r.status = TestStatus::INFORMATIONAL;
            std::ostringstream actual;
            actual << "Table=" << target_table
                   << " sql_type=" << sql_type
                   << " precision=" << col_size
                   << " scale=" << scale
                   << " unsigned=" << (unsigned_attr == SQL_TRUE ? "TRUE" :
                                       unsigned_attr == SQL_FALSE ? "FALSE" : "UNKNOWN");
            r.actual = actual.str();
        });
}

// ── PORT plan §4.8 — Procedure catalog discovery ──────────────────────────

namespace {

// Read column N as a string from the current row. Returns empty on NULL or
// fetch error. Used to dump the SQLProcedures / SQLProcedureColumns rows
// into the result's `actual` field for diagnostics.
std::string fetch_string_col(SQLHSTMT h, SQLUSMALLINT col) {
    core::GuardedBuffer<char> buf(256, 0);  // D62
    SQLLEN ind = 0;
    SQLRETURN rc = SQLGetData(h, col, SQL_C_CHAR, buf.data(), buf.declared_bytes(), &ind);
    if (!SQL_SUCCEEDED(rc) || ind == SQL_NULL_DATA) return {};
    // D68: to `ind`. These strings go into the report as the procedure and
    // column names the driver returned.
    return core::bounded_string(buf.data(), buf.declared_elements(), ind).value;
}

// A18: this returned 0 for a NULL value *and* for a failed SQLGetData, and
// 0 is also SQL_PARAM_TYPE_UNKNOWN — so an unreadable row was indistinguishable
// from a legitimate one and got counted as a bogus type code. Returns nullopt
// for "no value" now, leaving the caller to decide what that means.
std::optional<long long> fetch_int_col(SQLHSTMT h, SQLUSMALLINT col) {
    SQLBIGINT v = 0;
    SQLLEN ind = 0;
    SQLRETURN rc = SQLGetData(h, col, SQL_C_SBIGINT, &v, sizeof(v), &ind);
    if (!SQL_SUCCEEDED(rc) || ind == SQL_NULL_DATA) return std::nullopt;
    return static_cast<long long>(v);
}

} // namespace

TestResult MetadataTests::test_sqlprocedures_smoke() {
    return run_test(
        "test_sqlprocedures_smoke", "SQLProcedures",
        "SQLProcedures returns a result set with the documented column shape",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLProcedures — Core conformance, columns 1..8",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            SQLRETURN ret = SQLProcedures(
                stmt.get_handle(),
                nullptr, 0,   // CatalogName
                nullptr, 0,   // SchemaName
                nullptr, 0);  // ProcName
            if (ret == SQL_ERROR) {
                // SQLSTATE IM001 — Driver doesn't support SQLProcedures.
                core::GuardedBuffer<char> state(6, 0);  // D62
                SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(), 1,
                              reinterpret_cast<SQLCHAR*>(state.data()),
                              nullptr, nullptr, 0, nullptr);
                if (core::sqlstate_string(state.data()) == "IM001") {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "Driver returned IM001 — SQLProcedures not supported";
                    return;
                }
                r.status = TestStatus::FAIL;
                r.actual = "SQLProcedures returned SQL_ERROR (state="
                         + core::sqlstate_string(state.data()) + ")";
                return;
            }

            // Verify the result set has at least 8 columns. ODBC defines
            // exactly 8 for SQLProcedures (PROCEDURE_CAT, PROCEDURE_SCHEM,
            // PROCEDURE_NAME, NUM_INPUT_PARAMS, NUM_OUTPUT_PARAMS,
            // NUM_RESULT_SETS, REMARKS, PROCEDURE_TYPE) plus optional
            // driver-specific columns.
            SQLSMALLINT ncols = 0;
            SQLNumResultCols(stmt.get_handle(), &ncols);
            if (ncols < 8) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLProcedures result set has " + std::to_string(ncols)
                         + " columns; expected at least 8 per spec.";
                return;
            }

            // Walk the result, count rows, capture the first procedure name.
            //
            // A26: capped. SQLProcedures on PostgreSQL returns thousands of
            // pg_catalog rows, and this probe only needs to know that the call
            // works and returns a plausible shape.
            constexpr int kMaxRows = 500;
            int row_count = 0;
            bool hit_cap = false;
            std::string first_name;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
                if (row_count == 0) {
                    first_name = fetch_string_col(stmt.get_handle(), 3);
                }
                ++row_count;
                if (row_count >= kMaxRows) { hit_cap = true; break; }
            }
            std::ostringstream oss;
            oss << "ncols=" << ncols << " rows=" << row_count;
            if (hit_cap) oss << "+ (stopped at cap)";
            if (!first_name.empty()) oss << " first=" << first_name;
            r.actual = oss.str();
            // Empty procedure list is valid — many DBMSs ship without stored
            // procedures by default. Don't FAIL on row_count==0.
        });
}

TestResult MetadataTests::test_sqlprocedurecolumns_smoke() {
    return run_test(
        "test_sqlprocedurecolumns_smoke", "SQLProcedureColumns",
        "SQLProcedureColumns enumerates parameters with COLUMN_TYPE codes",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLProcedureColumns — direction codes SQL_PARAM_INPUT, "
        "SQL_PARAM_OUTPUT, SQL_PARAM_INPUT_OUTPUT, SQL_RESULT_COL, SQL_RETURN_VALUE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);
            SQLRETURN ret = SQLProcedureColumns(
                stmt.get_handle(),
                nullptr, 0,   // CatalogName
                nullptr, 0,   // SchemaName
                nullptr, 0,   // ProcName  — match all
                nullptr, 0);  // ColumnName
            if (ret == SQL_ERROR) {
                core::GuardedBuffer<char> state(6, 0);  // D62
                SQLGetDiagRec(SQL_HANDLE_STMT, stmt.get_handle(), 1,
                              reinterpret_cast<SQLCHAR*>(state.data()),
                              nullptr, nullptr, 0, nullptr);
                if (core::sqlstate_string(state.data()) == "IM001") {
                    r.status = TestStatus::SKIP_UNSUPPORTED;
                    r.actual = "Driver returned IM001 — SQLProcedureColumns not supported";
                    return;
                }
                r.status = TestStatus::FAIL;
                r.actual = "SQLProcedureColumns returned SQL_ERROR (state="
                         + core::sqlstate_string(state.data()) + ")";
                return;
            }

            // Confirm shape — at least the documented 8 columns.
            SQLSMALLINT ncols = 0;
            SQLNumResultCols(stmt.get_handle(), &ncols);
            if (ncols < 8) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLProcedureColumns result has " + std::to_string(ncols)
                         + " columns; expected at least 8.";
                return;
            }

            // A26: capped, as above.
            constexpr int kMaxRows = 500;
            int row_count = 0;
            bool hit_cap = false;
            int input_count = 0, output_count = 0, inout_count = 0,
                result_count = 0, return_count = 0, other_count = 0,
                unknown_count = 0, unreadable_count = 0;
            std::ostringstream first_rows;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) {
                ++row_count;
                if (row_count >= kMaxRows) { hit_cap = true; break; }
                std::string proc = fetch_string_col(stmt.get_handle(), 3);
                std::string colname = fetch_string_col(stmt.get_handle(), 4);
                std::optional<long long> ctype = fetch_int_col(stmt.get_handle(), 5);
                if (!ctype) {
                    // A18: unreadable or NULL COLUMN_TYPE. Counted separately
                    // — it is a different defect from a code outside the set.
                    ++unreadable_count;
                } else {
                    switch (static_cast<SQLSMALLINT>(*ctype)) {
                        // A18: SQL_PARAM_TYPE_UNKNOWN (0) is in the spec's
                        // documented set and was missing, so every driver that
                        // reports it landed in other_count and FAILed.
                        case SQL_PARAM_TYPE_UNKNOWN: ++unknown_count; break;
                        case SQL_PARAM_INPUT:        ++input_count;  break;
                        case SQL_PARAM_OUTPUT:       ++output_count; break;
                        case SQL_PARAM_INPUT_OUTPUT: ++inout_count;  break;
                        case SQL_RESULT_COL:         ++result_count; break;
                        case SQL_RETURN_VALUE:       ++return_count; break;
                        default:                     ++other_count; break;
                    }
                }
                if (row_count <= 3) {
                    if (row_count > 1) first_rows << ", ";
                    first_rows << proc << "." << colname << "(type=";
                    if (ctype) first_rows << *ctype; else first_rows << "?";
                    first_rows << ")";
                }
            }
            std::ostringstream oss;
            oss << "rows=" << row_count
                << " IN=" << input_count
                << " OUT=" << output_count
                << " INOUT=" << inout_count
                << " RESULT=" << result_count
                << " RETURN=" << return_count;
            if (unknown_count) oss << " UNKNOWN=" << unknown_count;
            if (other_count) oss << " OTHER=" << other_count;
            if (unreadable_count) oss << " UNREADABLE=" << unreadable_count;
            if (hit_cap) oss << " (stopped at cap)";
            if (row_count > 0) oss << " sample=[" << first_rows.str() << "]";
            r.actual = oss.str();

            if (row_count == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "Driver returned zero parameter rows. If the "
                               "DBMS has registered procedures, this is a "
                               "conformance gap; if not, the test cannot run.";
                return;
            }
            if (other_count > 0) {
                r.status = TestStatus::FAIL;
                r.suggestion = "Some COLUMN_TYPE codes are outside the documented "
                               "set {SQL_PARAM_TYPE_UNKNOWN, SQL_PARAM_INPUT, "
                               "SQL_PARAM_OUTPUT, SQL_PARAM_INPUT_OUTPUT, "
                               "SQL_RESULT_COL, SQL_RETURN_VALUE}.";
            } else if (unreadable_count > 0) {
                // A18: a row whose COLUMN_TYPE cannot be read is its own
                // failure, not a bogus type code.
                r.status = TestStatus::FAIL;
                r.suggestion = "COLUMN_TYPE could not be read for some rows. "
                               "It is a NOT NULL SMALLINT in the documented "
                               "result set, so every row must carry one.";
            }
        });
}

} // namespace odbc_crusher::tests
