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

// A19 — read a character column in full, across as many SQLGetData calls as
// the driver needs.
bool TestBase::get_data_full(SQLHSTMT hstmt, SQLUSMALLINT col,
                             std::string& out, bool& is_null,
                             std::string& error) {
    out.clear();
    is_null = false;
    error.clear();

    // Deliberately small: a driver that gets the continuation protocol wrong
    // is far more likely to be caught with a buffer that forces several
    // round trips than with one that swallows every realistic value whole.
    char buf[256];

    // A19: a driver that returns 01004 but restarts the value from byte 0 on
    // every call would keep this loop running forever. That is not
    // hypothetical - the mock driver did exactly that until D37 - and this
    // tool's stated contract is that it never wedges, whatever the driver
    // does. Two independent guards: the driver's own "bytes still available"
    // must strictly decrease, and an absolute round cap catches a driver that
    // reports SQL_NO_TOTAL and so gives nothing to compare.
    SQLLEN prev_available = -1;
    size_t rounds = 0;
    const size_t kMaxRounds = 4096;   // ~1 MB at 255 bytes a round

    for (;;) {
        SQLLEN ind = 0;
        buf[0] = 0;
        SQLRETURN rc = SQLGetData(hstmt, col, SQL_C_CHAR, buf, sizeof(buf),
                                  &ind);
        if (rc == SQL_NO_DATA) {
            // No more data for this column — everything is already in `out`.
            return true;
        }
        if (!SQL_SUCCEEDED(rc)) {
            error = "SQLGetData rc=" + std::to_string(rc);
            return false;
        }
        if (ind == SQL_NULL_DATA) {
            is_null = true;
            out.clear();
            return true;
        }

        // `ind` is the bytes *still available*, not the bytes written, so it
        // overshoots on truncation and can be SQL_NO_TOTAL. bounded_string
        // already knows how to turn that pair into a safe length — A3.
        BoundedString piece = bounded_string(buf, sizeof(buf), ind);
        out += piece.value;

        // Continue only on the truncation shape: SQL_SUCCESS_WITH_INFO with
        // a reported length that overshoots the buffer, or SQL_NO_TOTAL.
        // Testing the length rather than reading the diagnostics means an
        // unrelated warning (a cursor message, say) cannot spin this loop.
        const bool more = rc == SQL_SUCCESS_WITH_INFO &&
                          (piece.truncated || piece.length_unknown);
        if (!more) return true;

        if (piece.value.empty()) {
            // A driver that keeps signalling truncation without producing
            // bytes would loop forever. Say so rather than hang the run —
            // "never crash" includes never wedging.
            error = "driver signalled truncation but returned no further data";
            return false;
        }
        if (ind != SQL_NO_TOTAL) {
            if (prev_available >= 0 && ind >= prev_available) {
                error = "driver did not advance between SQLGetData calls "
                        "(still reports " + std::to_string(ind) +
                        " bytes available after " + std::to_string(out.size()) +
                        " were read) — it is restarting the value rather than "
                        "continuing it";
                return false;
            }
            prev_available = ind;
        }
        if (++rounds > kMaxRounds) {
            error = "gave up after " + std::to_string(kMaxRounds) +
                    " SQLGetData continuations without reaching the end of "
                    "the value";
            return false;
        }
    }
}

// A19
bool TestBase::is_bare_identifier(const std::string& ident) {
    if (ident.empty()) return false;
    auto is_alpha = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    unsigned char first = static_cast<unsigned char>(ident[0]);
    if (!is_alpha(first) && first != '_') return false;
    for (unsigned char c : ident) {
        if (!is_alpha(c) && !(c >= '0' && c <= '9') && c != '_') return false;
    }
    return true;
}

// A19 — quote an identifier, but only one that needs it. See the header for
// why quoting every name is the wrong fix.
std::string TestBase::quote_identifier(const std::string& ident) {
    if (is_bare_identifier(ident)) return ident;

    char quote[8] = {0};
    SQLSMALLINT len = 0;
    SQLRETURN rc = SQLGetInfo(conn_.get_handle(), SQL_IDENTIFIER_QUOTE_CHAR,
                              quote, sizeof(quote), &len);
    if (!SQL_SUCCEEDED(rc)) return ident;

    BoundedString q = bounded_string(quote, sizeof(quote), len);
    // The spec spells "this driver does not support quoting" as a single
    // blank, so an empty or blank answer means: leave the name alone.
    if (q.value.empty() || q.value == " ") return ident;
    if (ident.find(q.value) != std::string::npos) return ident;
    return q.value + ident + q.value;
}

RowVerification TestBase::verify_rows_persisted(
    const std::string& table,
    const std::string& pk_col,
    const std::string& value_col,
    long expected_count)
{
    RowVerification v;

    // A19: the three identifiers were interpolated bare, with nothing
    // checking they *could* be. They still are when they are bare-safe —
    // quoting a name whose CREATE TABLE was unquoted breaks PostgreSQL, which
    // folds unquoted names down and not up; see quote_identifier's header
    // comment. A name that is not bare-safe gets quoted, because such a name
    // can only exist if it was created quoted.
    const std::string q_table = quote_identifier(table);
    const std::string q_pk = quote_identifier(pk_col);
    const std::string q_value = quote_identifier(value_col);

    // Step 1: COUNT(*)
    try {
        core::OdbcStatement count_stmt(conn_);
        count_stmt.execute("SELECT COUNT(*) FROM " + q_table);
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
        fetch_stmt.execute("SELECT " + q_value + " FROM " + q_table +
                           " ORDER BY " + q_pk);
        while (true) {
            SQLRETURN rc = SQLFetch(fetch_stmt.get_handle());
            if (rc == SQL_NO_DATA) break;
            if (!SQL_SUCCEEDED(rc)) {
                v.diagnostic = "SELECT value SQLFetch rc=" + std::to_string(rc);
                return v;
            }
            // A19: this read into a bare `char buf[256]` and kept whatever
            // fitted. `SQL_SUCCEEDED` accepts the 01004 that comes with
            // truncation, so a value of 256 characters or more came back
            // short and was then compared against what the probe inserted —
            // reporting a correct driver as having corrupted the data, at
            // CRITICAL. get_data_full keeps calling until the value is whole.
            std::string value;
            bool is_null = false;
            std::string err;
            if (!get_data_full(fetch_stmt.get_handle(), 1, value, is_null,
                               err)) {
                v.diagnostic = "SELECT value: " + err;
                return v;
            }
            if (is_null) {
                // A19: NULL used to arrive here as an empty std::string,
                // indistinguishable from '' — in the one helper the header
                // advertises for NULL-versus-empty work.
                v.actual_values.emplace_back(std::nullopt);
            } else {
                v.actual_values.emplace_back(std::move(value));
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

// A22
CommitOutcome TestBase::commit_now() {
    CommitOutcome out;
    out.rc = SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_COMMIT);
    out.ok = SQL_SUCCEEDED(out.rc) != 0;

    std::string message;
    try {
        auto err = core::OdbcError::from_handle(SQL_HANDLE_DBC,
                                                conn_.get_handle(),
                                                "SQLEndTran(SQL_COMMIT)");
        if (!err.diagnostics().empty()) {
            out.sqlstate = err.diagnostics()[0].sqlstate;
            message = err.diagnostics()[0].message;
        }
    } catch (...) {
        // Reading diagnostics must never be the thing that fails a probe.
    }

    out.summary = "SQLEndTran(SQL_COMMIT) rc=" + std::to_string(out.rc);
    if (!out.sqlstate.empty()) out.summary += " [" + out.sqlstate + "]";
    if (!message.empty()) out.summary += " " + message;
    return out;
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
    const std::vector<std::string>& id_ddl_variants,
    std::string val_column)
    : conn_(conn),
      table_name_(std::move(table_name)),
      val_ddl_(std::move(val_ddl)),
      val_column_(std::move(val_column))
{
    ScopedAutocommitOn ac(conn_.get_handle());

    // C4: reuse an existing table rather than insisting on DDL privileges.
    //
    // All three helpers this guard replaces began this way, and for a good
    // reason: this tool is pointed at other people's databases, where the
    // connected user often cannot CREATE TABLE. If the table is already there
    // — left by an earlier run — use it.
    //
    // A15: empty it first. The crash guard in main.cpp keeps the process
    // alive past an abort, so the cleanup DROP is skipped and the rows
    // survive; probes that assert exact counts then fail a correct driver on
    // the strength of yesterday's data.
    {
        bool exists = false;
        try {
            core::OdbcStatement probe(conn_);
            probe.execute("SELECT 1 FROM " + table_name_ + " WHERE 1=0");
            exists = true;
        } catch (...) {
            SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
        }
        if (exists) {
            try {
                core::OdbcStatement clear(conn_);
                clear.execute("DELETE FROM " + table_name_);
            } catch (...) {
                // Not fatal. A probe that cannot empty the table will report
                // a count mismatch, which is the honest outcome.
                SQLEndTran(SQL_HANDLE_DBC, conn_.get_handle(), SQL_ROLLBACK);
            }
            ok_ = true;
            reused_ = true;
            // The requested DDL was not what created this table, and probes
            // print val_ddl() into their report ("... in a NVARCHAR(64)
            // column"). Say what is true instead of what was asked for.
            val_ddl_ = "pre-existing " + table_name_ + " column";
            return;
        }
    }

    auto try_create_all = [&]() -> bool {
        for (const auto& id_ddl : id_ddl_variants) {
            const std::string sql = "CREATE TABLE " + table_name_ +
                                    " (ID " + id_ddl + ", " + val_column_ +
                                    " " + val_ddl_ + ")";
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
      val_column_(std::move(other.val_column_)),
      ok_(other.ok_),
      reused_(other.reused_),
      last_error_(std::move(other.last_error_)) {
    // The moved-from guard must not drop the table the new one now owns.
    other.ok_ = false;
}

RoundTripTableGuard RoundTripTableGuard::create_first_working(
    core::OdbcConnection& conn,
    const std::string& table_name,
    const std::vector<std::string>& val_ddl_variants,
    const std::vector<std::string>& id_ddl_variants,
    std::string val_column) {
    if (val_ddl_variants.empty()) {
        return RoundTripTableGuard(conn, table_name, "VARCHAR(64)",
                                   id_ddl_variants, val_column);
    }
    // Try every variant but the last, returning as soon as one works.
    for (size_t i = 0; i + 1 < val_ddl_variants.size(); ++i) {
        RoundTripTableGuard guard(conn, table_name, val_ddl_variants[i],
                                  id_ddl_variants, val_column);
        if (guard.ok()) return guard;
    }
    // The last variant's guard is returned whether or not it worked, so a
    // caller that finds !ok() sees a real last_error() rather than one
    // synthesised from an extra attempt.
    return RoundTripTableGuard(conn, table_name, val_ddl_variants.back(),
                               id_ddl_variants, std::move(val_column));
}

RoundTripTableGuard::~RoundTripTableGuard() {
    if (!ok_) return;
    ScopedAutocommitOn ac(conn_.get_handle());
    try_drop(conn_, table_name_);
}

} // namespace odbc_crusher::tests
