#include "test_base.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

TestResult TestBase::make_result(
    const std::string& test_name,
    const std::string& function,
    TestStatus status,
    const std::string& expected,
    const std::string& actual,
    Severity severity,
    ConformanceLevel conformance,
    const std::string& spec_reference
) {
    TestResult result;
    result.test_name = test_name;
    result.function = function;
    result.status = status;
    result.severity = severity;
    result.conformance = conformance;
    result.spec_reference = spec_reference;
    result.expected = expected;
    result.actual = actual;
    result.duration = std::chrono::microseconds(0);
    return result;
}

RowVerification TestBase::verify_rows_persisted(
    const std::string& table,
    const std::string& pk_col,
    const std::string& value_col,
    long expected_count)
{
    RowVerification v;

    // Step 1: COUNT(*)
    try {
        core::OdbcStatement count_stmt(conn_);
        count_stmt.execute("SELECT COUNT(*) FROM " + table);
        SQLRETURN rc = SQLFetch(count_stmt.get_handle());
        if (!SQL_SUCCEEDED(rc)) {
            v.diagnostic = "SELECT COUNT(*) produced no row (fetch rc=" +
                           std::to_string(rc) + ")";
            return v;
        }
        SQLLEN ind = 0;
        SQLBIGINT count_bi = 0;
        rc = SQLGetData(count_stmt.get_handle(), 1, SQL_C_SBIGINT, &count_bi,
                        sizeof(count_bi), &ind);
        if (!SQL_SUCCEEDED(rc)) {
            v.diagnostic = "SELECT COUNT(*) SQLGetData rc=" + std::to_string(rc);
            return v;
        }
        v.actual_count = static_cast<long>(count_bi);
    } catch (const core::OdbcError& e) {
        v.diagnostic = std::string("SELECT COUNT(*) threw: ") + e.what();
        return v;
    }

    // Step 2: ORDER BY pk, collect value column
    try {
        core::OdbcStatement fetch_stmt(conn_);
        fetch_stmt.execute("SELECT " + value_col + " FROM " + table +
                           " ORDER BY " + pk_col);
        while (true) {
            SQLRETURN rc = SQLFetch(fetch_stmt.get_handle());
            if (rc == SQL_NO_DATA) break;
            if (!SQL_SUCCEEDED(rc)) {
                v.diagnostic = "SELECT value SQLFetch rc=" + std::to_string(rc);
                return v;
            }
            char buf[256] = {0};
            SQLLEN ind = 0;
            rc = SQLGetData(fetch_stmt.get_handle(), 1, SQL_C_CHAR, buf,
                            sizeof(buf), &ind);
            if (!SQL_SUCCEEDED(rc)) {
                v.diagnostic = "SELECT value SQLGetData rc=" + std::to_string(rc);
                return v;
            }
            if (ind == SQL_NULL_DATA) {
                v.actual_values.emplace_back();
            } else {
                v.actual_values.emplace_back(buf);
            }
        }
    } catch (const core::OdbcError& e) {
        v.diagnostic = std::string("SELECT value threw: ") + e.what();
        return v;
    }

    // Cross-check: returned row count matches expectations.
    if (v.actual_count != expected_count) {
        v.diagnostic = "COUNT(*) = " + std::to_string(v.actual_count) +
                       " but expected " + std::to_string(expected_count);
        return v;
    }
    if (static_cast<long>(v.actual_values.size()) != expected_count) {
        v.diagnostic = "ORDER BY fetch returned " +
                       std::to_string(v.actual_values.size()) +
                       " rows but COUNT(*) reported " +
                       std::to_string(v.actual_count);
        return v;
    }

    v.ok = true;
    return v;
}

} // namespace odbc_crusher::tests
