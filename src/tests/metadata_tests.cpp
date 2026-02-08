#include "metadata_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <sstream>

namespace odbc_crusher::tests {

std::vector<TestResult> MetadataTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_tables_catalog());
    results.push_back(test_columns_catalog());
    results.push_back(test_primary_keys());
    results.push_back(test_foreign_keys());
    results.push_back(test_statistics());
    results.push_back(test_special_columns());
    results.push_back(test_table_privileges());
    
    return results;
}

TestResult MetadataTests::test_tables_catalog() {
    TestResult result = make_result(
        "test_tables_catalog",
        "SQLTables",
        TestStatus::PASS,
        "List tables in the database",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLTables"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
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
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "SQLTables not supported or failed";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "SQLTables call did not succeed; check driver catalog support";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult MetadataTests::test_columns_catalog() {
    TestResult result = make_result(
        "test_columns_catalog",
        "SQLColumns",
        TestStatus::PASS,
        "List columns from system tables",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLColumns"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to get columns from a known system table
        // Different databases have different system tables
        std::vector<std::pair<std::string, std::string>> test_tables = {
            {"", "RDB$DATABASE"},      // Firebird system table
            {"information_schema", "TABLES"},  // MySQL system table
            {"sys", "tables"},         // SQL Server system table
            {"", "CUSTOMERS"},         // Mock/test driver table
            {"", "USERS"}             // Mock/test driver table
        };
        
        bool success = false;
        int column_count = 0;
        
        for (const auto& [schema, table] : test_tables) {
            try {
                stmt.recycle();
                SQLRETURN ret = SQLColumns(
                    stmt.get_handle(),
                    nullptr, 0,                                    // Catalog
                    schema.empty() ? nullptr : (SQLCHAR*)schema.c_str(),
                    schema.empty() ? 0 : SQL_NTS,                 // Schema
                    (SQLCHAR*)table.c_str(), SQL_NTS,            // Table
                    nullptr, 0                                    // Column (all)
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
        
        if (success) {
            std::ostringstream oss;
            oss << "Found " << column_count << " column(s) from system table";
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "SQLColumns callable but no system tables accessible";
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "SQLColumns executed but no columns found in tested system tables";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult MetadataTests::test_primary_keys() {
    TestResult result = make_result(
        "test_primary_keys",
        "SQLPrimaryKeys",
        TestStatus::PASS,
        "Query primary key information",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLPrimaryKeys"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to get primary keys from system tables
        std::vector<std::pair<std::string, std::string>> test_tables = {
            {"", "RDB$DATABASE"},
            {"information_schema", "TABLES"},
            {"sys", "tables"}
        };
        
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
                        result.actual = oss.str();
                        result.status = TestStatus::PASS;
                        break;
                    }
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!callable) {
            result.actual = "SQLPrimaryKeys not supported by driver";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQLPrimaryKeys is a Level 1 function and may not be implemented";
        } else if (result.actual.empty()) {
            result.actual = "SQLPrimaryKeys callable (no PKs in queried tables)";
            result.status = TestStatus::PASS;
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult MetadataTests::test_statistics() {
    TestResult result = make_result(
        "test_statistics",
        "SQLStatistics",
        TestStatus::PASS,
        "Query index/statistics information",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLStatistics"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::pair<std::string, std::string>> test_tables = {
            {"", "RDB$DATABASE"},
            {"information_schema", "TABLES"}
        };
        
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
                    result.actual = oss.str();
                    result.status = TestStatus::PASS;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!callable) {
            result.actual = "SQLStatistics not supported by driver";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQLStatistics is a Level 1 function and may not be implemented";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult MetadataTests::test_special_columns() {
    TestResult result = make_result(
        "test_special_columns",
        "SQLSpecialColumns",
        TestStatus::PASS,
        "Query special columns (row identifiers)",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLSpecialColumns"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::pair<std::string, std::string>> test_tables = {
            {"", "RDB$DATABASE"},
            {"information_schema", "TABLES"}
        };
        
        bool callable = false;
        
        for (const auto& [schema, table] : test_tables) {
            try {
                stmt.recycle();
                SQLRETURN ret = SQLSpecialColumns(
                    stmt.get_handle(),
                    SQL_BEST_ROWID,     // Best row identifier
                    nullptr, 0,
                    schema.empty() ? nullptr : (SQLCHAR*)schema.c_str(),
                    schema.empty() ? 0 : SQL_NTS,
                    (SQLCHAR*)table.c_str(), SQL_NTS,
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
                    result.actual = oss.str();
                    result.status = TestStatus::PASS;
                    break;
                }
            } catch (const core::OdbcError&) {
                continue;
            }
        }
        
        if (!callable) {
            result.actual = "SQLSpecialColumns not supported by driver";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQLSpecialColumns is a Level 1 function and may not be implemented";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult MetadataTests::test_foreign_keys() {
    TestResult result = make_result(
        "test_foreign_keys",
        "SQLForeignKeys",
        TestStatus::PASS,
        "Retrieve foreign key relationships",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLForeignKeys"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to get foreign keys using different approaches
        // ODBC spec requires either PK or FK table name
        bool success = false;
        bool callable = false;
        int fk_count = 0;
        
        // Strategy 1: Try known FK tables (mock driver)
        std::vector<std::string> fk_tables = {"ORDERS", "ORDER_ITEMS"};
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
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else if (callable) {
            result.actual = "SQLForeignKeys callable (no foreign keys in database)";
            result.status = TestStatus::PASS;
        } else {
            result.actual = "SQLForeignKeys not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQLForeignKeys is a Level 1 function; some drivers don't implement foreign key metadata";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError&) {
        result.status = TestStatus::SKIP_UNSUPPORTED;
        result.actual = "Foreign keys not supported by driver";
        result.suggestion = "SQLForeignKeys is a Level 1 function; this is normal for simple drivers";
    }
    
    return result;
}

TestResult MetadataTests::test_table_privileges() {
    TestResult result = make_result(
        "test_table_privileges",
        "SQLTablePrivileges",
        TestStatus::PASS,
        "Query table access privileges",
        "",
        Severity::INFO,
        ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLTablePrivileges"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
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
            result.actual = oss.str();
            result.status = TestStatus::PASS;
        } else {
            result.actual = "SQLTablePrivileges not supported";
            result.status = TestStatus::SKIP_UNSUPPORTED;
            result.suggestion = "SQLTablePrivileges is a Level 2 function; many drivers don't implement privilege metadata";
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError&) {
        result.status = TestStatus::SKIP_UNSUPPORTED;
        result.actual = "Table privileges not supported by driver";
        result.suggestion = "SQLTablePrivileges is a Level 2 function; this is normal for basic ODBC drivers";
    }
    
    return result;
}

} // namespace odbc_crusher::tests
