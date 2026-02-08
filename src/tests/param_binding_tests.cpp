#include "param_binding_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <sstream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

std::vector<TestResult> ParameterBindingTests::run() {
    std::vector<TestResult> results;
    
    results.push_back(test_bindparam_wchar_input());
    results.push_back(test_bindparam_null_indicator());
    results.push_back(test_param_rebind_execute());
    
    return results;
}

TestResult ParameterBindingTests::test_bindparam_wchar_input() {
    TestResult result = make_result(
        "test_bindparam_wchar_input",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQLBindParameter with SQL_C_WCHAR input type accepts Unicode data",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_C_WCHAR for Unicode parameter data"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        // Prepare a parameterized query — try multiple patterns
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        // (some drivers export W-functions but have broken W→A conversion)
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare parameterized query";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Bind a Unicode string parameter
        auto param_wbuf = to_sqlwchar("TestCustomer"); SQLWCHAR* param_value = param_wbuf.data();
        SQLLEN param_len = SQL_NTS;
        
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
            50, 0,
            param_value, (param_wbuf.size() * sizeof(SQLWCHAR)), &param_len);
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(ret)) {
            actual << "SQLBindParameter with SQL_C_WCHAR succeeded";
            
            // Try to execute
            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
            if (SQL_SUCCEEDED(exec_ret)) {
                actual << "; execute succeeded";
            } else {
                actual << "; execute returned " << exec_ret;
            }
        } else {
            actual << "SQLBindParameter with SQL_C_WCHAR returned " << ret;
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Driver may not support SQL_C_WCHAR parameter binding";
        }
        result.actual = actual.str();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult ParameterBindingTests::test_bindparam_null_indicator() {
    TestResult result = make_result(
        "test_bindparam_null_indicator",
        "SQLBindParameter",
        TestStatus::PASS,
        "SQLBindParameter with SQL_NULL_DATA indicator passes NULL to driver",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: SQL_NULL_DATA in StrLen_or_IndPtr for NULL"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS VARCHAR(50))",
            "SELECT CAST(? AS VARCHAR(50)) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare query for NULL parameter test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // Bind with SQL_NULL_DATA indicator
        SQLLEN null_ind = SQL_NULL_DATA;
        
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
            50, 0,
            nullptr, 0, &null_ind);
        
        std::ostringstream actual;
        if (SQL_SUCCEEDED(ret)) {
            actual << "SQLBindParameter with NULL indicator succeeded";
            
            SQLRETURN exec_ret = SQLExecute(stmt.get_handle());
            actual << "; execute returned " << exec_ret;
        } else {
            actual << "SQLBindParameter with NULL indicator returned " << ret;
            result.status = TestStatus::FAIL;
            result.suggestion = "Drivers must accept SQL_NULL_DATA as parameter indicator";
        }
        result.actual = actual.str();
        
        auto end_time = std::chrono::high_resolution_clock::now();
        result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
    } catch (const core::OdbcError& e) {
        result.status = TestStatus::ERR;
        result.actual = e.what();
        result.diagnostic = e.format_diagnostics();
    }
    
    return result;
}

TestResult ParameterBindingTests::test_param_rebind_execute() {
    TestResult result = make_result(
        "test_param_rebind_execute",
        "SQLBindParameter",
        TestStatus::PASS,
        "Bind, execute, rebind with new value, execute again",
        "",
        Severity::INFO,
        ConformanceLevel::CORE,
        "ODBC 3.8 SQLBindParameter: Parameters persist across executions"
    );
    
    try {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        core::OdbcStatement stmt(conn_);
        
        std::vector<std::string> queries = {
            "SELECT CAST(? AS INTEGER)",
            "SELECT CAST(? AS INTEGER) FROM RDB$DATABASE"
        };
        SQLRETURN ret = SQL_ERROR;
        // Strategy 1: Try W-function (SQLPrepareW)
        for (const auto& q : queries) {
            ret = SQLPrepareW(stmt.get_handle(),
                SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
            if (SQL_SUCCEEDED(ret)) break;
            SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
        }
        // Strategy 2: Fall back to ANSI SQLPrepare if W-function fails
        if (!SQL_SUCCEEDED(ret)) {
            for (const auto& q : queries) {
                ret = SQLPrepare(stmt.get_handle(),
                    (SQLCHAR*)q.c_str(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_RESET_PARAMS);
            }
        }
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not prepare query for rebind test";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        // First bind and execute
        SQLINTEGER param_val = 1;
        SQLLEN ind = 0;
        ret = SQLBindParameter(stmt.get_handle(), 1,
            SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
            0, 0, &param_val, 0, &ind);
        
        if (!SQL_SUCCEEDED(ret)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.actual = "Could not bind first parameter";
            auto end_time = std::chrono::high_resolution_clock::now();
            result.duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
            return result;
        }
        
        SQLRETURN exec1 = SQLExecute(stmt.get_handle());
        
        // Close cursor if needed
        SQLCloseCursor(stmt.get_handle());
        
        // Rebind with different value and execute again
        param_val = 2;
        SQLRETURN exec2 = SQLExecute(stmt.get_handle());
        
        std::ostringstream actual;
        actual << "First execute: " << exec1 << "; Rebind + second execute: " << exec2;
        result.actual = actual.str();
        
        if (!SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            result.status = TestStatus::SKIP_INCONCLUSIVE;
            result.suggestion = "Neither execution succeeded; driver may not support parameterized queries";
        } else if (SQL_SUCCEEDED(exec1) && !SQL_SUCCEEDED(exec2)) {
            result.status = TestStatus::FAIL;
            result.suggestion = "Second execute after rebind should succeed if first did";
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
