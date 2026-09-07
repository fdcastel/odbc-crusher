#include "test_base.hpp"
#include "core/odbc_error.hpp"
#include "core/odbc_statement.hpp"

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

#include <algorithm>
#include <cstring>
#include <utility>

namespace odbc_crusher::tests {

std::string DialectAttempt::format_failures() const {
    if (failures.empty()) return {};
    std::string out;
    for (const auto& f : failures) {
        if (!out.empty()) out += "\n";
        out += "  " + f.query + "  ->  ";
        out += f.sqlstate.empty() ? std::string("(no SQLSTATE)") : f.sqlstate;
        if (!f.message.empty()) out += ": " + f.message;
    }
    return out;
}

std::string TestBase::first_sqlstate(SQLSMALLINT handle_type, SQLHANDLE handle,
                                     const std::string& fallback) {
    try {
        auto err = core::OdbcError::from_handle(handle_type, handle, "");
        if (!err.diagnostics().empty()) return err.diagnostics()[0].sqlstate;
    } catch (...) {
        // Reading diagnostics must never be the thing that fails a probe.
    }
    return fallback;
}

namespace {

// Record why one dialect variant was rejected.
DialectFailure make_failure(const std::string& query, const core::OdbcError& e) {
    DialectFailure failure;
    failure.query = query;
    failure.message = e.what();
    if (!e.diagnostics().empty()) {
        failure.sqlstate = e.diagnostics()[0].sqlstate;
        failure.message = e.diagnostics()[0].message;
    }
    return failure;
}

}  // namespace

FailureClassification TestBase::classify_failure(SQLSMALLINT handle_type,
                                                 SQLHANDLE handle) {
    // The states that mean "not implemented", rather than "you asked for
    // something valid and I could not do it".
    //
    //   IM001  Driver does not support this function
    //   HYC00  Optional feature not implemented
    //   HY092  Invalid attribute/option identifier - what a driver returns for
    //          an attribute it does not know
    //   HY106  Fetch type out of range - likewise for a cursor mode it lacks
    static const char* const kUnsupported[] = {"IM001", "HYC00", "HY092", "HY106"};

    FailureClassification out;
    out.status = TestStatus::FAIL;
    try {
        auto err = core::OdbcError::from_handle(handle_type, handle, "");
        const auto& diags = err.diagnostics();
        for (const auto& d : diags) {
            for (const char* state : kUnsupported) {
                if (d.sqlstate == state) {
                    out.status = TestStatus::SKIP_UNSUPPORTED;
                    out.sqlstate = d.sqlstate;
                    out.message = d.message;
                    return out;
                }
            }
        }
        if (!diags.empty()) {
            out.sqlstate = diags[0].sqlstate;
            out.message = diags[0].message;
        }
    } catch (...) {
        // Reading diagnostics must never be the thing that fails a probe.
    }
    return out;
}

void TestBase::report_failure(TestResult& r, SQLSMALLINT handle_type,
                              SQLHANDLE handle, const std::string& what) {
    const auto c = classify_failure(handle_type, handle);
    r.status = c.status;

    const std::string state = c.sqlstate.empty() ? "no SQLSTATE" : c.sqlstate;
    if (c.status == TestStatus::SKIP_UNSUPPORTED) {
        r.actual = what + " reported " + state + " (optional feature not "
                   "implemented)";
    } else {
        r.actual = what + " failed with " + state;
        if (r.severity > Severity::ERR) r.severity = Severity::ERR;
    }
    // B3: the state always reaches the report, whichever way it was classified.
    r.diagnostic = state + (c.message.empty() ? "" : ": " + c.message);
}

std::vector<std::string> TestBase::literal_select_variants(const std::string& sql) {
    // Bare form first: it is what most engines want, and trying it first keeps
    // the common case a single round trip.
    return {sql,
            sql + " FROM RDB$DATABASE",   // Firebird
            sql + " FROM DUAL"};          // Oracle, and MySQL accepts it too
}

std::string TestBase::execute_literal_select(core::OdbcStatement& stmt,
                                             const std::string& sql) {
    auto attempt = execute_first_working(stmt, literal_select_variants(sql));
    if (!attempt) {
        std::string msg = "No dialect variant of `" + sql + "` executed:\n" +
                          attempt.format_failures();
        throw core::OdbcError(msg);
    }
    return attempt.query;
}

DialectAttempt TestBase::try_first_working(
    const std::vector<std::string>& queries,
    const std::function<void(const std::string&)>& body) {
    DialectAttempt attempt;
    for (const auto& query : queries) {
        try {
            body(query);
            attempt.executed = true;
            attempt.query = query;
            return attempt;
        } catch (const core::OdbcError& e) {
            attempt.failures.push_back(make_failure(query, e));
            // No recycle() here: OdbcStatement::execute() and ::prepare() both
            // recycle on entry, which is what makes trying the next variant
            // safe on drivers like Firebird that poison a handle after a
            // syntax error.
        }
    }
    return attempt;
}

DialectAttempt TestBase::prepare_first_working(
    core::OdbcStatement& stmt, const std::vector<std::string>& queries) {
    return try_first_working(queries,
                             [&](const std::string& q) { stmt.prepare(q); });
}

DialectAttempt TestBase::execute_first_working(
    core::OdbcStatement& stmt, const std::vector<std::string>& queries) {
    return try_first_working(queries,
                             [&](const std::string& q) { stmt.execute(q); });
}

BoundedString TestBase::bounded_string(const char* buf, size_t capacity,
                                       SQLLEN reported) {
    BoundedString out;
    if (!buf || capacity == 0) {
        out.length_unknown = true;
        return out;
    }

    // A driver may use every byte but the last; the last is the terminator.
    const size_t max_chars = capacity - 1;

    // How much was actually written, bounded by the buffer. memchr rather than
    // strlen so an unterminated buffer is not undefined behaviour.
    const void* nul = std::memchr(buf, '\0', max_chars);
    const size_t written =
        nul ? static_cast<size_t>(static_cast<const char*>(nul) - buf) : max_chars;

    if (reported == SQL_NO_TOTAL) {
        // The driver cannot say how much there is. Take what is in the buffer.
        out.length_unknown = true;
        out.truncated = true;
        out.value.assign(buf, written);
        return out;
    }

    if (reported < 0) {
        // SQL_NULL_DATA, or a driver returning nonsense. Either way there is
        // no length to trust; report nothing rather than invent a string.
        out.length_unknown = true;
        return out;
    }

    const size_t reported_len = static_cast<size_t>(reported);
    if (reported_len > max_chars) {
        // The classic case: `reported` is the total available, not the amount
        // written, and SQL_SUCCEEDED accepted the 01004 that came with it.
        out.truncated = true;
        out.value.assign(buf, written);
        return out;
    }

    // Trust the driver's length, but never past what it can have written.
    out.value.assign(buf, std::min(reported_len, written));
    return out;
}

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

RoundTripTableGuard::RoundTripTableGuard(RoundTripTableGuard&& other) noexcept
    : conn_(other.conn_),
      table_name_(std::move(other.table_name_)),
      val_ddl_(std::move(other.val_ddl_)),
      ok_(other.ok_),
      last_error_(std::move(other.last_error_)) {
    // The moved-from guard must not drop the table the new one now owns.
    other.ok_ = false;
}

RoundTripTableGuard RoundTripTableGuard::create_first_working(
    core::OdbcConnection& conn,
    const std::string& table_name,
    const std::vector<std::string>& val_ddl_variants,
    const std::vector<std::string>& id_ddl_variants) {
    if (val_ddl_variants.empty()) {
        return RoundTripTableGuard(conn, table_name, "VARCHAR(64)", id_ddl_variants);
    }
    // Try every variant but the last, returning as soon as one works.
    for (size_t i = 0; i + 1 < val_ddl_variants.size(); ++i) {
        RoundTripTableGuard guard(conn, table_name, val_ddl_variants[i],
                                  id_ddl_variants);
        if (guard.ok()) return guard;
    }
    // The last variant's guard is returned whether or not it worked, so a
    // caller that finds !ok() sees a real last_error() rather than one
    // synthesised from an extra attempt.
    return RoundTripTableGuard(conn, table_name, val_ddl_variants.back(),
                               id_ddl_variants);
}

RoundTripTableGuard::~RoundTripTableGuard() {
    if (!ok_) return;
    AutocommitForDdl ac(conn_.get_handle());
    try_drop(conn_, table_name_);
}

} // namespace odbc_crusher::tests
