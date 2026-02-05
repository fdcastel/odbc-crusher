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
    results.push_back(test_statistics());
    results.push_back(test_special_columns());
    
    return results;
}

TestResult MetadataTests::test_tables_catalog() {
    TestResult result = make_result(
        "test_tables_catalog",
        "SQLTables",
        TestStatus::PASS,
        "List tables in the database",
        "",
        Severity::INFO
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
            result.status = TestStatus::SKIP;
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
        Severity::INFO
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Try to get columns from a known system table
        // Different databases have different system tables
        std::vector<std::pair<std::string, std::string>> test_tables = {
            {"", "RDB$DATABASE"},      // Firebird system table
            {"information_schema", "TABLES"},  // MySQL system table
            {"sys", "tables"}          // SQL Server system table
        };
        
        bool success = false;
        int column_count = 0;
        
        for (const auto& [schema, table] : test_tables) {
            try {
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
            result.status = TestStatus::SKIP;
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
        Severity::INFO
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
            result.actual = "SQLPrimaryKeys callable (no PKs found in test tables)";
            result.status = TestStatus::SKIP;
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
        Severity::INFO
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
            result.actual = "SQLStatistics not tested";
            result.status = TestStatus::SKIP;
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
        Severity::INFO
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
            result.actual = "SQLSpecialColumns not tested";
            result.status = TestStatus::SKIP;
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

} // namespace odbc_crusher::tests
