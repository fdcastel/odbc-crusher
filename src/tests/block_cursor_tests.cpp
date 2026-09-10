#include "block_cursor_tests.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <algorithm>
#include <array>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

namespace {

constexpr SQLULEN kRowset = 4;      // 6 rows => a full rowset then a partial one
constexpr SQLINTEGER kSlotSentinel = -111;

// Set a statement attribute, or explain why the probe cannot continue.
//
// A driver may decline a Level 2 attribute, and declining is not a failure —
// but it has to decline the way the specification says, so an application can
// tell "I will not" from "I broke". Same reasoning as B1/B3 elsewhere.
bool set_attr_or_skip(core::OdbcStatement& stmt, TestResult& r,
                      SQLINTEGER attr, SQLPOINTER value, const char* attr_name) {
    SQLRETURN rc = SQLSetStmtAttr(stmt.get_handle(), attr, value, 0);
    if (SQL_SUCCEEDED(rc)) return true;
    const std::string state =
        TestBase::first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "");
    // P18: HY024 joined this list after the fleet run. MariaDB refuses
    // SQL_ATTR_KEYSET_SIZE with "invalid attribute value", which is what a
    // driver that has no keyset-driven cursor says when asked to size one -
    // the value is meaningless without the cursor type to go with it. HYC00
    // would be the more precise refusal, but the distinction between "I do not
    // implement this" and "that value means nothing here" is not one this
    // probe is entitled to grade: either way the attribute is not set, and
    // there is nothing left to measure. Reporting a refusal as a defect is how
    // a conformance tool loses its reader.
    if (state == "HYC00" || state == "01S02" || state == "HY092" ||
        state == "HY024" || state == "IM001") {
        r.status = TestStatus::SKIP_UNSUPPORTED;
        r.actual = std::string("Driver declined SQLSetStmtAttr(") + attr_name +
                   ") with SQLSTATE=" + state;
        r.suggestion = "Block cursors are a Level 2 feature; declining one with "
                       "HYC00, 01S02 or HY024 is a refusal, not a defect.";
    } else {
        r.status = TestStatus::FAIL;
        r.severity = Severity::ERR;
        r.actual = std::string("SQLSetStmtAttr(") + attr_name + ") failed with SQLSTATE=" +
                   (state.empty() ? "(none)" : state);
        r.suggestion = "An optional statement attribute is declined with HYC00, "
                       "or substituted with 01S02. Any other state reports a "
                       "failure rather than a refusal.";
    }
    return false;
}

const char* row_status_name(SQLUSMALLINT s) {
    switch (s) {
        case SQL_ROW_SUCCESS:           return "SUCCESS";
        case SQL_ROW_SUCCESS_WITH_INFO: return "SUCCESS_WITH_INFO";
        case SQL_ROW_ERROR:             return "ERROR";
        case SQL_ROW_NOROW:             return "NOROW";
        default:                        return "?";
    }
}

std::string join_slots(const SQLINTEGER* values, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; ++i) {
        if (i) out += ", ";
        out += std::to_string(values[i]);
    }
    return out;
}

}  // namespace

// Six rows, 1..6, in a table this category owns.
bool BlockCursorTests::ensure_block_table(TestResult& r, std::string& out_query) {
    static const char* const kTable = "ODBC_CRUSHER_BLOCK";
    if (!table_) {
        table_.emplace(conn_, kTable, "INTEGER");
        if (!table_->ok()) {
            last_setup_error_ = table_->last_error();
            table_.reset();
        } else {
            // A15: the guard empties a reused table, so this always starts at
            // zero rows and the six inserts below are the whole content.
            try {
                core::OdbcStatement ins(conn_);
                for (int i = 1; i <= 6; ++i) {
                    ins.execute("INSERT INTO " + std::string(kTable) + " (ID, " +
                                table_->val_column() + ") VALUES (" +
                                std::to_string(i) + ", " + std::to_string(i) + ")");
                }
                commit_now();
            } catch (const core::OdbcError& e) {
                last_setup_error_ = e.what();
                table_.reset();
            }
        }
    }
    if (!table_) {
        r.status = TestStatus::SKIP_INCONCLUSIVE;
        r.actual = "Could not create the six-row block-cursor table";
        r.diagnostic = last_setup_error_;
        r.suggestion =
            "These probes need a six-row result set to fetch in rowsets. "
            "CREATE TABLE and INSERT privileges are required.";
        return false;
    }
    out_query = "SELECT " + table_->val_column() + " FROM " + table_->name() +
                " ORDER BY ID";
    return true;
}

std::vector<TestResult> BlockCursorTests::run() {
    std::vector<TestResult> results = {
        test_rowset_fetch_fills_bound_arrays(),
        test_fetch_pointers_survive_prepare(),
        test_row_bind_offset_applies_column_wise(),
        test_keyset_size_is_not_the_rowset_size(),
        test_settable_pointer_attrs_round_trip(),
    };
    table_.reset();   // C4: the guard drops the table
    return results;
}

// ── P1 — the substrate ──────────────────────────────────────────────────
//
// Not a detector: it is the ground the next three stand on. If this fails, the
// driver has no working block cursor and the rest of the category is measuring
// nothing.
TestResult BlockCursorTests::test_rowset_fetch_fills_bound_arrays() {
    return run_test(
        "test_rowset_fetch_fills_bound_arrays",
        "SQLSetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE)/SQLBindCol/SQLFetch",
        "A rowset of 4 over 6 rows fills every bound slot: 1-4, then 5-6, then "
        "SQL_NO_DATA",
        Severity::ERR, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 Block Cursors — SQL_ATTR_ROW_ARRAY_SIZE",
        [&](TestResult& r) {
            std::string query;
            if (!ensure_block_table(r, query)) return;

            // The rowset size has to be set before the fetch, so the fixture
            // check above runs on its own handle and this one starts clean.
            core::OdbcStatement block(conn_);
            if (!set_attr_or_skip(block, r, SQL_ATTR_ROW_ARRAY_SIZE,
                                  reinterpret_cast<SQLPOINTER>(kRowset),
                                  "SQL_ATTR_ROW_ARRAY_SIZE")) return;

            SQLINTEGER values[kRowset];
            SQLLEN indicators[kRowset];
            std::fill(std::begin(values), std::end(values), kSlotSentinel);
            std::fill(std::begin(indicators), std::end(indicators), SQL_NULL_DATA);

            block.execute(query);
            SQLRETURN rc = SQLBindCol(block.get_handle(), 1, SQL_C_SLONG, values,
                                      sizeof(SQLINTEGER), indicators);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = "SQLBindCol over an array returned " + std::to_string(rc);
                return;
            }

            std::ostringstream oss;
            rc = SQLFetch(block.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "First rowset fetch returned " + std::to_string(rc) +
                           " (SQLSTATE=" + first_sqlstate(SQL_HANDLE_STMT,
                                                          block.get_handle(), "none") + ")";
                return;
            }
            oss << "rowset 1: [" << join_slots(values, kRowset) << "]";
            for (SQLULEN i = 0; i < kRowset; ++i) {
                if (values[i] != static_cast<SQLINTEGER>(i + 1)) {
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::CRITICAL;
                    r.actual = oss.str() + "; expected [1, 2, 3, 4]";
                    r.suggestion =
                        "Every row of a rowset must land in its own slot of the "
                        "bound array. Repeating one value, or leaving slots at "
                        "their pre-fetch contents, corrupts data without failing "
                        "a call.";
                    return;
                }
            }

            std::fill(std::begin(values), std::end(values), kSlotSentinel);
            rc = SQLFetch(block.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + "; second rowset fetch returned " +
                           std::to_string(rc);
                return;
            }
            oss << "; rowset 2: [" << join_slots(values, kRowset) << "]";
            if (values[0] != 5 || values[1] != 6) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = oss.str() + "; expected the partial rowset to start 5, 6";
                return;
            }

            rc = SQLFetch(block.get_handle());
            if (rc != SQL_NO_DATA) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + "; a third fetch returned " +
                           std::to_string(rc) + ", not SQL_NO_DATA";
                r.suggestion =
                    "Six rows at a rowset of four is two fetches and then "
                    "SQL_NO_DATA. Anything else means the cursor did not end "
                    "where the result set did.";
                return;
            }
            r.actual = oss.str() + "; then SQL_NO_DATA";
        });
}

// ── P2 — #301 ───────────────────────────────────────────────────────────
TestResult BlockCursorTests::test_fetch_pointers_survive_prepare() {
    return run_test(
        "test_fetch_pointers_survive_prepare",
        "SQLSetStmtAttr(SQL_ATTR_ROWS_FETCHED_PTR)/SQLPrepare/SQLFetch",
        "SQL_ATTR_ROWS_FETCHED_PTR and SQL_ATTR_ROW_STATUS_PTR set before "
        "SQLPrepare are still written by SQLFetch",
        Severity::ERR, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 Block Cursors — statement attributes persist until set again",
        [&](TestResult& r) {
            std::string query;
            if (!ensure_block_table(r, query)) return;

            core::OdbcStatement stmt(conn_);
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROW_ARRAY_SIZE,
                                  reinterpret_cast<SQLPOINTER>(kRowset),
                                  "SQL_ATTR_ROW_ARRAY_SIZE")) return;

            // The whole point: both pointers are set *before* the prepare, which
            // is the order an application naturally writes and the order that
            // was broken. `setDefaultImplDesc` rebuilt the implementation row
            // descriptor on every prepare and reset these two to NULL with it,
            // so SQLFetch wrote to neither — the counter kept whatever it held
            // and the status array stayed untouched, on every fetch.
            SQLULEN rows_fetched = 999;
            SQLUSMALLINT row_status[kRowset];
            std::fill(std::begin(row_status), std::end(row_status),
                      static_cast<SQLUSMALLINT>(0xFFFF));

            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROWS_FETCHED_PTR,
                                  &rows_fetched, "SQL_ATTR_ROWS_FETCHED_PTR")) return;
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROW_STATUS_PTR,
                                  row_status, "SQL_ATTR_ROW_STATUS_PTR")) return;

            SQLRETURN prep = SQLPrepare(
                stmt.get_handle(),
                reinterpret_cast<SQLCHAR*>(const_cast<char*>(query.c_str())),
                SQL_NTS);
            if (!SQL_SUCCEEDED(prep)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not prepare the six-row fixture query";
                return;
            }

            SQLINTEGER values[kRowset];
            SQLLEN indicators[kRowset];
            std::fill(std::begin(values), std::end(values), kSlotSentinel);
            SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG, values,
                       sizeof(SQLINTEGER), indicators);

            SQLRETURN rc = SQLExecute(stmt.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLExecute returned " + std::to_string(rc);
                return;
            }

            rc = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "First rowset fetch returned " + std::to_string(rc);
                return;
            }

            std::ostringstream oss;
            oss << "after rowset 1: rows_fetched=" << rows_fetched << ", status=["
                << row_status_name(row_status[0]) << ", "
                << row_status_name(row_status[1]) << ", "
                << row_status_name(row_status[2]) << ", "
                << row_status_name(row_status[3]) << "]";

            if (rows_fetched == 999) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = oss.str() + " — the counter was never written";
                r.suggestion =
                    "SQL_ATTR_ROWS_FETCHED_PTR set before SQLPrepare must still "
                    "be written by SQLFetch: a statement attribute persists "
                    "until the statement is freed or the attribute is set "
                    "again, and SQLPrepare does not clear it. An application "
                    "that sets it in the natural order gets no row count at "
                    "all, including for the last partial rowset.";
                return;
            }
            if (rows_fetched != kRowset) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + "; expected rows_fetched=4";
                return;
            }
            if (row_status[0] == 0xFFFF) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + " — the status array was never written";
                r.suggestion =
                    "SQL_ATTR_ROW_STATUS_PTR set before SQLPrepare must still be "
                    "filled by SQLFetch, one entry per row of the rowset.";
                return;
            }

            // The partial rowset is where a wrong counter does the most damage:
            // an application loops on it to know how many slots are valid.
            std::fill(std::begin(row_status), std::end(row_status),
                      static_cast<SQLUSMALLINT>(0xFFFF));
            rc = SQLFetch(stmt.get_handle());
            if (SQL_SUCCEEDED(rc)) {
                oss << "; after rowset 2: rows_fetched=" << rows_fetched
                    << ", status[2]=" << row_status_name(row_status[2]);
                if (rows_fetched != 2) {
                    r.status = TestStatus::FAIL;
                    r.actual = oss.str() + "; expected rows_fetched=2 on the "
                                           "partial rowset";
                    return;
                }
                if (row_status[2] != SQL_ROW_NOROW && row_status[2] != 0xFFFF) {
                    // Not graded beyond noting it: drivers differ on whether
                    // unused slots are marked NOROW or left alone, and the
                    // specification's own wording admits both readings.
                    oss << " (unused slots not marked NOROW)";
                }
            }
            r.actual = oss.str();
        });
}

// ── P3 — #306 ───────────────────────────────────────────────────────────
TestResult BlockCursorTests::test_row_bind_offset_applies_column_wise() {
    return run_test(
        "test_row_bind_offset_applies_column_wise",
        "SQLSetStmtAttr(SQL_ATTR_ROW_BIND_OFFSET_PTR)/SQLFetch",
        "A column-wise rowset writes each row at offset + elementSize * row, "
        "not all rows at the same address",
        Severity::ERR, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 Block Cursors — SQL_ATTR_ROW_BIND_OFFSET_PTR",
        [&](TestResult& r) {
            std::string query;
            if (!ensure_block_table(r, query)) return;

            core::OdbcStatement stmt(conn_);
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROW_ARRAY_SIZE,
                                  reinterpret_cast<SQLPOINTER>(kRowset),
                                  "SQL_ATTR_ROW_ARRAY_SIZE")) return;
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROW_BIND_TYPE,
                                  reinterpret_cast<SQLPOINTER>(SQL_BIND_BY_COLUMN),
                                  "SQL_ATTR_ROW_BIND_TYPE")) return;

            // Twice the rowset, so a shifted write has somewhere to land and
            // the unshifted half can be checked for not having been touched.
            constexpr size_t kSlots = kRowset * 2;
            SQLINTEGER values[kSlots];
            SQLLEN indicators[kSlots];
            std::fill(std::begin(values), std::end(values), kSlotSentinel);
            std::fill(std::begin(indicators), std::end(indicators), SQL_NULL_DATA);

            // The offset is in *bytes*, and it shifts both the data and the
            // indicator array by the same amount.
            SQLLEN offset = static_cast<SQLLEN>(kRowset * sizeof(SQLINTEGER));
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_ROW_BIND_OFFSET_PTR,
                                  &offset, "SQL_ATTR_ROW_BIND_OFFSET_PTR")) return;

            stmt.execute(query);
            SQLBindCol(stmt.get_handle(), 1, SQL_C_SLONG, values,
                       sizeof(SQLINTEGER), indicators);

            SQLRETURN rc = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "Fetch with a bind offset returned " + std::to_string(rc) +
                           " (SQLSTATE=" + first_sqlstate(SQL_HANDLE_STMT,
                                                          stmt.get_handle(), "none") + ")";
                return;
            }

            std::ostringstream oss;
            oss << "offset " << offset << " bytes; slots [" << join_slots(values, kSlots) << "]";

            // The shifted half must hold the rowset.
            bool shifted_ok = true;
            for (SQLULEN i = 0; i < kRowset; ++i) {
                if (values[kRowset + i] != static_cast<SQLINTEGER>(i + 1)) shifted_ok = false;
            }
            // The unshifted half must be untouched.
            bool unshifted_clean = true;
            for (SQLULEN i = 0; i < kRowset; ++i) {
                if (values[i] != kSlotSentinel) unshifted_clean = false;
            }

            if (!unshifted_clean) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = oss.str() + " — rows were written at the bound address, "
                                       "ignoring the offset";
                r.suggestion =
                    "SQL_ATTR_ROW_BIND_OFFSET_PTR is added to every bound address "
                    "before each row is written. Ignoring it overwrites whatever "
                    "the application put at the unshifted address.";
                return;
            }
            if (!shifted_ok) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = oss.str() + " — expected 1, 2, 3, 4 in the shifted half";
                r.suggestion =
                    "With column-wise binding the row number scales the element "
                    "size and the offset is added once: offset + elementSize * "
                    "row. Taking the row-wise path instead advances by "
                    "SQL_ATTR_ROW_BIND_TYPE, which is 0 here, so every row lands "
                    "on the same address while the counter still reports a full "
                    "rowset.";
                return;
            }
            r.actual = oss.str() + " — every row at offset + elementSize * row";
        });
}

// ── P4 — #307 ───────────────────────────────────────────────────────────
TestResult BlockCursorTests::test_keyset_size_is_not_the_rowset_size() {
    return run_test(
        "test_keyset_size_is_not_the_rowset_size",
        "SQLSetStmtAttr(SQL_ATTR_KEYSET_SIZE)/SQLGetStmtAttr",
        "Setting SQL_ATTR_KEYSET_SIZE leaves SQL_ATTR_ROW_ARRAY_SIZE alone and "
        "reads back as itself",
        Severity::ERR, ConformanceLevel::LEVEL_2,
        "ODBC 3.8 SQLSetStmtAttr — SQL_ATTR_KEYSET_SIZE",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            SQLULEN rowset_before = 0;
            if (!SQL_SUCCEEDED(SQLGetStmtAttr(stmt.get_handle(),
                                              SQL_ATTR_ROW_ARRAY_SIZE,
                                              &rowset_before, 0, nullptr))) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLGetStmtAttr(SQL_ATTR_ROW_ARRAY_SIZE) failed, so "
                           "there is no before-value to compare against";
                return;
            }

            constexpr SQLULEN kKeyset = 7;
            if (!set_attr_or_skip(stmt, r, SQL_ATTR_KEYSET_SIZE,
                                  reinterpret_cast<SQLPOINTER>(kKeyset),
                                  "SQL_ATTR_KEYSET_SIZE")) return;

            SQLULEN rowset_after = 0;
            SQLGetStmtAttr(stmt.get_handle(), SQL_ATTR_ROW_ARRAY_SIZE,
                           &rowset_after, 0, nullptr);

            std::ostringstream oss;
            oss << "keyset size set to " << kKeyset << "; rowset was "
                << rowset_before << ", now " << rowset_after;

            if (rowset_after != rowset_before) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::CRITICAL;
                r.actual = oss.str();
                r.suggestion =
                    "SQL_ATTR_KEYSET_SIZE is the size of the keyset a "
                    "keyset-driven cursor keeps, not the number of rows a fetch "
                    "returns. Storing it in the rowset size means the next "
                    "SQLFetch writes that many rows into buffers the "
                    "application bound for one — a buffer overrun it never "
                    "asked for.";
                return;
            }

            SQLULEN keyset_back = 0;
            SQLRETURN rc = SQLGetStmtAttr(stmt.get_handle(), SQL_ATTR_KEYSET_SIZE,
                                          &keyset_back, 0, nullptr);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.actual = oss.str() + "; SQLGetStmtAttr(SQL_ATTR_KEYSET_SIZE) "
                           "returned SQLSTATE=" +
                           first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "none");
                r.suggestion =
                    "Every statement attribute SQLSetStmtAttr accepts must come "
                    "back from SQLGetStmtAttr.";
                return;
            }
            if (keyset_back != kKeyset) {
                r.status = TestStatus::FAIL;
                r.actual = oss.str() + "; read back as " + std::to_string(keyset_back);
                return;
            }
            r.actual = oss.str() + "; reads back as " + std::to_string(keyset_back);
        });
}

// ── P5 — #304 ───────────────────────────────────────────────────────────
TestResult BlockCursorTests::test_settable_pointer_attrs_round_trip() {
    return run_test(
        "test_settable_pointer_attrs_round_trip",
        "SQLSetStmtAttr/SQLGetStmtAttr (pointer attributes)",
        "Every pointer statement attribute SQLSetStmtAttr accepts comes back "
        "from SQLGetStmtAttr",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetStmtAttr — settable attributes are gettable",
        [&](TestResult& r) {
            // The rule is the probe: an attribute the setter stored and the
            // driver is using, that the application cannot read back, leaves
            // generic ODBC consumers — anything that saves and restores
            // statement state — unable to do their job. `SQL_ATTR_ROW_STATUS_PTR`
            // is included as the control that already worked.
            struct Attr { SQLINTEGER id; const char* name; };
            static const Attr attrs[] = {
                {SQL_ATTR_ROW_STATUS_PTR,       "SQL_ATTR_ROW_STATUS_PTR"},
                {SQL_ATTR_ROWS_FETCHED_PTR,     "SQL_ATTR_ROWS_FETCHED_PTR"},
                {SQL_ATTR_ROW_BIND_OFFSET_PTR,  "SQL_ATTR_ROW_BIND_OFFSET_PTR"},
                {SQL_ATTR_ROW_OPERATION_PTR,    "SQL_ATTR_ROW_OPERATION_PTR"},
                {SQL_ATTR_PARAMS_PROCESSED_PTR, "SQL_ATTR_PARAMS_PROCESSED_PTR"},
                {SQL_ATTR_PARAM_BIND_OFFSET_PTR,"SQL_ATTR_PARAM_BIND_OFFSET_PTR"},
                {SQL_ATTR_PARAM_OPERATION_PTR,  "SQL_ATTR_PARAM_OPERATION_PTR"},
                {SQL_ATTR_PARAM_STATUS_PTR,     "SQL_ATTR_PARAM_STATUS_PTR"},
            };

            core::OdbcStatement stmt(conn_);
            // One buffer big enough to be a legal target for any of them; the
            // probe cares about the pointer's identity, not what it points to.
            std::array<SQLULEN, 8> scratch{};

            int settable = 0;
            std::string unreadable;
            std::string wrong;

            for (const auto& a : attrs) {
                SQLPOINTER wanted = scratch.data();
                if (!SQL_SUCCEEDED(SQLSetStmtAttr(stmt.get_handle(), a.id, wanted, 0))) {
                    // Not settable is not this probe's subject: it only claims
                    // that what the setter accepts, the getter returns.
                    continue;
                }
                ++settable;

                SQLPOINTER readback = nullptr;
                SQLRETURN rc = SQLGetStmtAttr(stmt.get_handle(), a.id,
                                              &readback, 0, nullptr);
                if (!SQL_SUCCEEDED(rc)) {
                    const std::string state =
                        first_sqlstate(SQL_HANDLE_STMT, stmt.get_handle(), "none");
                    if (!unreadable.empty()) unreadable += ", ";
                    unreadable += std::string(a.name) + " (" + state + ")";
                    continue;
                }
                if (readback != wanted) {
                    if (!wrong.empty()) wrong += ", ";
                    wrong += a.name;
                }
                // Put it back, so a later probe on a fresh statement is not
                // reading this one's scratch buffer.
                SQLSetStmtAttr(stmt.get_handle(), a.id, nullptr, 0);
            }

            std::ostringstream oss;
            oss << settable << " of " << (sizeof(attrs) / sizeof(attrs[0]))
                << " pointer attributes accepted by SQLSetStmtAttr";
            if (!unreadable.empty()) oss << "; unreadable: " << unreadable;
            if (!wrong.empty()) oss << "; read back as a different pointer: " << wrong;
            r.actual = oss.str();

            if (!unreadable.empty() || !wrong.empty()) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::ERR;
                r.suggestion =
                    "Every statement attribute SQLSetStmtAttr accepts must come "
                    "back from SQLGetStmtAttr. Answering HYC00 \"Optional "
                    "feature not implemented\" for a value the driver stored and "
                    "is using tells the application the feature is absent when "
                    "it is merely unreadable — and an application cannot even "
                    "check which rows-fetched pointer it set.";
            }
        });
}

} // namespace odbc_crusher::tests
