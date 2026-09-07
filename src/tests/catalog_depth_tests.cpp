#include "catalog_depth_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> CatalogDepthTests::run() {
    return {
        test_tables_search_patterns(),
        test_columns_result_set_shape(),
        test_statistics_result(),
        test_procedures_result(),
        test_privileges_result(),
        test_catalog_null_parameters()
    };
}

TestResult CatalogDepthTests::test_tables_search_patterns() {
    return run_test(
        "test_tables_search_patterns", "SQLTables",
        "SQLTables with SQL_ALL_TABLE_TYPES returns valid result set",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLTables: Special search patterns for catalog enumeration",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Test SQL_ALL_TABLE_TYPES — returns list of table types.
            // Per ODBC spec, the special enumeration mode requires empty strings
            // (not nullptr) for catalog, schema, and table name with "%" as type.
            SqlWcharBuf empty_str("");
            SQLRETURN ret = SQLTablesW(stmt.get_handle(),
                empty_str.ptr(), 0,   // Catalog = empty string
                empty_str.ptr(), 0,   // Schema = empty string
                empty_str.ptr(), 0,   // Table name = empty string
                SqlWcharBuf("%").ptr(), SQL_NTS);  // All table types

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLTables with table type pattern did not succeed";
                return;
            }

            int count = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && count < 100) {
                count++;
            }

            std::ostringstream actual;
            actual << "SQLTables returned " << count << " row(s) with type pattern '%'";
            r.actual = actual.str();

            if (count == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLTables returned no rows for type pattern; catalog may be empty";
            }
        });
}

TestResult CatalogDepthTests::test_columns_result_set_shape() {
    return run_test(
        "test_columns_result_set_shape", "SQLColumns",
        "SQLColumns result set has all 18 ODBC-specified columns",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLColumns: Result set must have 18 columns",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            SQLRETURN ret = SQLColumnsW(stmt.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("%").ptr(), SQL_NTS,     // All tables
                SqlWcharBuf("%").ptr(), SQL_NTS);    // All columns

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLColumns call did not succeed";
                return;
            }

            SQLSMALLINT num_cols = 0;
            SQLNumResultCols(stmt.get_handle(), &num_cols);

            std::ostringstream actual;
            actual << "SQLColumns result set has " << num_cols << " columns (expected 18)";
            r.actual = actual.str();

            if (num_cols < 18) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "SQLColumns result set must have at least 18 columns per ODBC spec SQLColumns";
            }
        });
}

TestResult CatalogDepthTests::test_statistics_result() {
    return run_test(
        "test_statistics_result", "SQLStatistics",
        "SQLStatistics returns valid index information",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLStatistics: Returns index and table statistics",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            SQLRETURN ret = SQLStatisticsW(stmt.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("CUSTOMERS").ptr(), SQL_NTS,
                SQL_INDEX_ALL, SQL_QUICK);

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLStatistics call did not succeed";
                return;
            }

            // Check result set shape: should have 13 columns
            SQLSMALLINT num_cols = 0;
            SQLNumResultCols(stmt.get_handle(), &num_cols);

            int row_count = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && row_count < 50) {
                row_count++;
            }

            std::ostringstream actual;
            actual << "SQLStatistics returned " << row_count << " rows, "
                   << num_cols << " columns (expected 13)";
            r.actual = actual.str();

            if (num_cols < 13) {
                r.status = TestStatus::FAIL;
                r.suggestion = "SQLStatistics result set must have 13 columns per ODBC spec";
            }
        });
}

TestResult CatalogDepthTests::test_procedures_result() {
    return run_test(
        "test_procedures_result", "SQLProcedures",
        "SQLProcedures/SQLProcedureColumns return valid result sets",
        Severity::INFO, ConformanceLevel::LEVEL_1,
        "ODBC 3.8 SQLProcedures: Returns procedure catalog even if empty",
        [&](TestResult& r) {
            // Test SQLProcedures
            core::OdbcStatement stmt(conn_);

            SQLRETURN ret = SQLProceduresW(stmt.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("%").ptr(), SQL_NTS);

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.actual = "SQLProcedures not supported by driver";
                return;
            }

            SQLSMALLINT num_cols = 0;
            SQLNumResultCols(stmt.get_handle(), &num_cols);

            int row_count = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && row_count < 50) {
                row_count++;
            }

            // Also test SQLProcedureColumns
            core::OdbcStatement stmt2(conn_);
            SQLRETURN ret2 = SQLProcedureColumnsW(stmt2.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("%").ptr(), SQL_NTS,
                SqlWcharBuf("%").ptr(), SQL_NTS);

            bool proc_cols_ok = SQL_SUCCEEDED(ret2);

            std::ostringstream actual;
            actual << "SQLProcedures: " << row_count << " rows, " << num_cols
                   << " cols; SQLProcedureColumns: " << (proc_cols_ok ? "OK" : "not supported");
            r.actual = actual.str();
        });
}

TestResult CatalogDepthTests::test_privileges_result() {
    return run_test(
        "test_privileges_result", "SQLTablePrivileges",
        "SQLTablePrivileges/SQLColumnPrivileges return valid result sets",
        Severity::INFO, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLTablePrivileges: Returns privilege information",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            SQLRETURN ret = SQLTablePrivilegesW(stmt.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("CUSTOMERS").ptr(), SQL_NTS);

            bool tbl_priv_ok = SQL_SUCCEEDED(ret);
            int tbl_priv_rows = 0;
            if (tbl_priv_ok) {
                while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && tbl_priv_rows < 50) {
                    tbl_priv_rows++;
                }
            }

            // Also test SQLColumnPrivileges
            core::OdbcStatement stmt2(conn_);
            SQLRETURN ret2 = SQLColumnPrivilegesW(stmt2.get_handle(),
                nullptr, 0, nullptr, 0,
                SqlWcharBuf("CUSTOMERS").ptr(), SQL_NTS,
                SqlWcharBuf("%").ptr(), SQL_NTS);

            bool col_priv_ok = SQL_SUCCEEDED(ret2);

            std::ostringstream actual;
            actual << "TablePrivileges: " << (tbl_priv_ok ? "OK" : "failed")
                   << " (" << tbl_priv_rows << " rows)"
                   << "; ColumnPrivileges: " << (col_priv_ok ? "OK" : "not supported");
            r.actual = actual.str();

            if (!tbl_priv_ok && !col_priv_ok) {
                r.status = TestStatus::SKIP_UNSUPPORTED;
                r.suggestion = "Privilege catalog functions are Level 2 and may not be supported";
            }
        });
}

TestResult CatalogDepthTests::test_catalog_null_parameters() {
    return run_test(
        "test_catalog_null_parameters", "SQLTables",
        "Catalog functions with NULL catalog/schema use default behavior",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 Catalog Functions: NULL catalog/schema means 'current'",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Call SQLTables with all NULL catalog/schema/table/type
            // This should return all accessible tables
            SQLRETURN ret = SQLTablesW(stmt.get_handle(),
                nullptr, 0,    // NULL catalog
                nullptr, 0,    // NULL schema
                nullptr, 0,    // NULL table name
                nullptr, 0);   // NULL table type

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLTables with all NULL parameters did not succeed";
                return;
            }

            int count = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && count < 200) {
                count++;
            }

            std::ostringstream actual;
            actual << "SQLTables with all NULL params returned " << count << " tables";
            r.actual = actual.str();
        });
}

} // namespace odbc_crusher::tests
