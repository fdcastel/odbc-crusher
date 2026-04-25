#include "test_base.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <utility>

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

// ── RoundTripTableGuard ─────────────────────────────────────────────────────

const std::vector<std::string>& RoundTripTableGuard::default_id_ddl_variants() {
    static const std::vector<std::string> kVariants = {"INTEGER", "INT"};
    return kVariants;
}

namespace {

// Save / set / restore SQL_ATTR_AUTOCOMMIT around DDL so a failed CREATE/DROP
// doesn't leave the connection in an inconsistent transaction state on
// drivers (Firebird) where DDL failure poisons the open txn.
class AutocommitForDdl {
public:
    explicit AutocommitForDdl(SQLHDBC hdbc) : hdbc_(hdbc) {
        SQLGetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT, &saved_, 0, nullptr);
        SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                          reinterpret_cast<SQLPOINTER>(SQL_AUTOCOMMIT_ON), 0);
    }
    ~AutocommitForDdl() {
        SQLSetConnectAttr(hdbc_, SQL_ATTR_AUTOCOMMIT,
                          reinterpret_cast<SQLPOINTER>(
                              static_cast<intptr_t>(saved_)), 0);
    }
    AutocommitForDdl(const AutocommitForDdl&) = delete;
    AutocommitForDdl& operator=(const AutocommitForDdl&) = delete;
private:
    SQLHDBC hdbc_;
    SQLUINTEGER saved_ = 0;
};

// Best-effort DROP: swallows errors, rolls back the connection-level txn on
// failure so the caller's next statement isn't blocked by a poisoned state.
void try_drop(core::OdbcConnection& conn, const std::string& table_name) {
    try {
        core::OdbcStatement s(conn);
        s.execute("DROP TABLE " + table_name);
    } catch (...) {
        SQLEndTran(SQL_HANDLE_DBC, conn.get_handle(), SQL_ROLLBACK);
    }
}

} // namespace

RoundTripTableGuard::RoundTripTableGuard(
    core::OdbcConnection& conn,
    std::string table_name,
    std::string val_ddl,
    const std::vector<std::string>& id_ddl_variants)
    : conn_(conn),
      table_name_(std::move(table_name)),
      val_ddl_(std::move(val_ddl))
{
    AutocommitForDdl ac(conn_.get_handle());

    auto try_create_all = [&]() -> bool {
        for (const auto& id_ddl : id_ddl_variants) {
            const std::string sql = "CREATE TABLE " + table_name_ +
                                    " (ID " + id_ddl + ", VAL " + val_ddl_ + ")";
            try {
                core::OdbcStatement s(conn_);
                s.execute(sql);
                return true;
            } catch (const core::OdbcError& e) {
                last_error_ = e.format_diagnostics();
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
            } catch (...) {
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
            }
        }
        return false;
    };

    if (try_create_all()) {
        ok_ = true;
        return;
    }

    // CREATE failed for every variant — most likely the table already exists
    // from a prior aborted run. Drop and retry once.
    try_drop(conn_, table_name_);
    if (try_create_all()) {
        ok_ = true;
    }
}

RoundTripTableGuard::~RoundTripTableGuard() {
    if (!ok_) return;
    AutocommitForDdl ac(conn_.get_handle());
    try_drop(conn_, table_name_);
}

} // namespace odbc_crusher::tests
