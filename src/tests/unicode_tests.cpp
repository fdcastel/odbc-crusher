#include "unicode_tests.hpp"
#include "core/odbc_statement.hpp"
#include "sqlwchar_utils.hpp"
#include "core/odbc_error.hpp"
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#include <sql.h>
#include <sqlext.h>

namespace odbc_crusher::tests {

namespace {

// First SQLSTATE on a handle, or "" when the driver posted nothing.
//
// Deliberately a local copy: **C5** (Phase 3) extracts the six-plus variants of
// this shape across the tree into one helper and **B3** builds
// classify_failure() on top of it. Phase 2 is localized probe fixes with no
// refactor, so this is a stand-in that C5 deletes.
std::string first_sqlstate(SQLSMALLINT handle_type, SQLHANDLE handle) {
    try {
        auto err = core::OdbcError::from_handle(handle_type, handle, "");
        if (!err.diagnostics().empty()) return err.diagnostics()[0].sqlstate;
    } catch (...) {
    }
    return "";
}

}  // namespace

std::vector<TestResult> UnicodeTests::run() {
    return {
        test_getinfo_wchar_strings(),
        test_describecol_wchar_names(),
        test_getdata_sql_c_wchar(),
        test_columns_unicode_patterns(),
        test_string_truncation_wchar(),
        test_wchar_roundtrip_non_ascii(),
        test_wchar_surrogate_pair_preserved()
    };
}

TestResult UnicodeTests::test_getinfo_wchar_strings() {
    return run_test(
        "test_getinfo_wchar_strings", "SQLGetInfo",
        "SQLGetInfo returns valid SQLWCHAR* for string info types",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetInfo: String info types return character data",
        [&](TestResult& r) {
            // Test multiple string info types
            struct InfoTest {
                SQLUSMALLINT info_type;
                const char* name;
            };

            InfoTest tests[] = {
                { SQL_DBMS_NAME, "SQL_DBMS_NAME" },
                { SQL_DBMS_VER, "SQL_DBMS_VER" },
                { SQL_DRIVER_NAME, "SQL_DRIVER_NAME" },
                { SQL_DRIVER_VER, "SQL_DRIVER_VER" },
            };

            int success_count = 0;
            std::ostringstream details;

            for (const auto& t : tests) {
                SQLWCHAR wbuf[256] = {0};
                SQLSMALLINT len = 0;
                SQLRETURN ret = SQLGetInfoW(conn_.get_handle(), t.info_type,
                                            wbuf, sizeof(wbuf), &len);
                if (SQL_SUCCEEDED(ret) && len > 0) {
                    success_count++;
                    // Verify the length is in bytes and is a multiple of sizeof(SQLWCHAR)
                    if (len % sizeof(SQLWCHAR) != 0) {
                        details << t.name << ": length " << len
                                << " not a multiple of sizeof(SQLWCHAR); ";
                    }
                } else {
                    details << t.name << ": failed (ret=" << ret << "); ";
                }
            }

            std::ostringstream actual;
            actual << success_count << "/4 string info types returned valid SQLWCHAR*";
            if (details.str().length() > 0) {
                actual << " [" << details.str() << "]";
            }
            r.actual = actual.str();

            if (success_count == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "Driver may not support Unicode info retrieval via SQLGetInfoW";
            } else if (success_count < 4) {
                r.status = TestStatus::FAIL;
                r.severity = Severity::WARNING;
                r.suggestion = "Some string info types did not return valid SQLWCHAR* data";
            }
        });
}

TestResult UnicodeTests::test_describecol_wchar_names() {
    return run_test(
        "test_describecol_wchar_names", "SQLDescribeCol",
        "SQLDescribeColW returns column names as SQLWCHAR*",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLDescribeCol: Column names returned in driver charset",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Execute a query to get a result set with known column names
            // Try multiple queries for cross-database compatibility
            std::vector<std::string> queries = {
                "SELECT 1 AS COL1, 'hello' AS COL2",
                "SELECT * FROM RDB$DATABASE",
                "SELECT * FROM CUSTOMERS"
            };
            SQLRETURN ret = SQL_ERROR;
            // Strategy 1: Try W-function (SQLExecDirectW)
            for (const auto& q : queries) {
                ret = SQLExecDirectW(stmt.get_handle(),
                    SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
            // Strategy 2: Fall back to ANSI SQLExecDirect if W-function fails
            // (some drivers export W-functions but have broken W→A conversion)
            if (!SQL_SUCCEEDED(ret)) {
                for (const auto& q : queries) {
                    ret = SQLExecDirect(stmt.get_handle(),
                        (SQLCHAR*)q.c_str(), SQL_NTS);
                    if (SQL_SUCCEEDED(ret)) break;
                    SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
                }
            }

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query to test column names";
                r.suggestion = "Ensure driver supports basic SELECT queries";
                return;
            }

            // Get column count
            SQLSMALLINT num_cols = 0;
            SQLNumResultCols(stmt.get_handle(), &num_cols);

            int success_count = 0;
            std::ostringstream details;

            for (SQLUSMALLINT i = 1; i <= static_cast<SQLUSMALLINT>(num_cols) && i <= 5; i++) {
                SQLWCHAR col_name[128] = {0};
                SQLSMALLINT name_len = 0;
                SQLSMALLINT data_type = 0;
                SQLULEN col_size = 0;
                SQLSMALLINT decimal_digits = 0;
                SQLSMALLINT nullable = 0;

                ret = SQLDescribeColW(stmt.get_handle(), i,
                    col_name, sizeof(col_name)/sizeof(SQLWCHAR), &name_len,
                    &data_type, &col_size, &decimal_digits, &nullable);

                if (SQL_SUCCEEDED(ret) && name_len > 0) {
                    success_count++;
                }
            }

            std::ostringstream actual;
            actual << success_count << " of " << std::min((int)num_cols, 5)
                   << " columns returned valid SQLWCHAR* names";
            r.actual = actual.str();

            if (success_count == 0) {
                r.status = TestStatus::FAIL;
                r.suggestion = "SQLDescribeColW did not return any column names";
            }
        });
}

TestResult UnicodeTests::test_getdata_sql_c_wchar() {
    return run_test(
        "test_getdata_sql_c_wchar", "SQLGetData",
        "SQLGetData with SQL_C_WCHAR retrieves Unicode string data",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQLGetData: SQL_C_WCHAR returns UTF-16 data with byte-length",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Execute a query that returns a string column
            std::vector<std::string> queries = {
                "SELECT CAST('Hello' AS VARCHAR(50))",
                "SELECT CAST('Hello' AS VARCHAR(50)) FROM RDB$DATABASE",
                "SELECT 'Hello'"
            };
            SQLRETURN ret = SQL_ERROR;
            // Strategy 1: Try W-function (SQLExecDirectW)
            for (const auto& q : queries) {
                ret = SQLExecDirectW(stmt.get_handle(),
                    SqlWcharBuf(q.c_str()).ptr(), SQL_NTS);
                if (SQL_SUCCEEDED(ret)) break;
                SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
            }
            // Strategy 2: Fall back to ANSI SQLExecDirect if W-function fails
            if (!SQL_SUCCEEDED(ret)) {
                for (const auto& q : queries) {
                    ret = SQLExecDirect(stmt.get_handle(),
                        (SQLCHAR*)q.c_str(), SQL_NTS);
                    if (SQL_SUCCEEDED(ret)) break;
                    SQLFreeStmt(stmt.get_handle(), SQL_CLOSE);
                }
            }

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "Could not execute query for SQL_C_WCHAR test";
                return;
            }

            // Fetch first row
            ret = SQLFetch(stmt.get_handle());
            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No rows to fetch for SQL_C_WCHAR test";
                return;
            }

            // Try to get a string column as SQL_C_WCHAR
            // Column 1 — first column of the result set
            SQLWCHAR wbuf[256] = {0};
            SQLLEN cb_value = 0;
            ret = SQLGetData(stmt.get_handle(), 1, SQL_C_WCHAR,
                             wbuf, sizeof(wbuf), &cb_value);

            if (SQL_SUCCEEDED(ret)) {
                // cb_value should be in bytes for SQL_C_WCHAR
                std::ostringstream actual;
                actual << "SQL_C_WCHAR data retrieved, byte length=" << cb_value;

                // Verify byte length is even (multiple of sizeof(SQLWCHAR))
                if (cb_value > 0 && cb_value % sizeof(SQLWCHAR) != 0) {
                    actual << " (WARNING: not a multiple of sizeof(SQLWCHAR))";
                    r.status = TestStatus::FAIL;
                    r.severity = Severity::WARNING;
                    r.suggestion = "pcbValue for SQL_C_WCHAR must be in bytes, not characters";
                }
                r.actual = actual.str();
            } else {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData with SQL_C_WCHAR failed";
                r.suggestion = "Driver should support SQL_C_WCHAR target type for string data";
            }
        });
}

TestResult UnicodeTests::test_columns_unicode_patterns() {
    return run_test(
        "test_columns_unicode_patterns", "SQLColumns",
        "SQLColumnsW accepts Unicode table/column name patterns",
        Severity::INFO, ConformanceLevel::CORE,
        "ODBC 3.8 SQLColumns: Accepts search patterns for catalog metadata",
        [&](TestResult& r) {
            core::OdbcStatement stmt(conn_);

            // Discover a table from the database via SQLTables.
            // IMPORTANT: Also capture the catalog (column 1) and schema (column 2)
            // from the SQLTables result, so we can pass them to SQLColumnsW.
            // On MySQL/MariaDB, the database is the catalog and schema is empty.
            // Without propagating the catalog, SQLColumns defaults to DATABASE()
            // which may be a different database than where the table was discovered.
            std::string table_catalog;
            std::string table_schema;
            std::string table_name;

            auto discover_table = [&](const SQLWCHAR* type_filter, SQLSMALLINT type_len) -> bool {
                core::OdbcStatement tbl_stmt(conn_);
                SQLRETURN tbl_ret = SQLTablesW(tbl_stmt.get_handle(),
                    nullptr, 0, nullptr, 0, nullptr, 0,
                    const_cast<SQLWCHAR*>(type_filter), type_len);
                if (!SQL_SUCCEEDED(tbl_ret)) return false;

                // Iterate through results to find a table NOT in information_schema.
                // information_schema views are dynamically-defined in many databases
                // (e.g. PostgreSQL) and SQLColumns cannot enumerate their columns.
                while (SQL_SUCCEEDED(SQLFetch(tbl_stmt.get_handle()))) {
                    char cat_buf[128] = {0};
                    char sch_buf[128] = {0};
                    char name_buf[128] = {0};
                    SQLLEN cat_ind = 0, sch_ind = 0, name_ind = 0;

                    SQLGetData(tbl_stmt.get_handle(), 1, SQL_C_CHAR, cat_buf, sizeof(cat_buf), &cat_ind);
                    SQLGetData(tbl_stmt.get_handle(), 2, SQL_C_CHAR, sch_buf, sizeof(sch_buf), &sch_ind);
                    SQLGetData(tbl_stmt.get_handle(), 3, SQL_C_CHAR, name_buf, sizeof(name_buf), &name_ind);

                    if (name_ind <= 0) continue;

                    // Skip information_schema tables/views — many drivers cannot
                    // expose their columns via SQLColumns (they are synthetic views).
                    std::string candidate_schema = (sch_ind > 0) ? std::string(sch_buf) : "";
                    std::string candidate_catalog = (cat_ind > 0) ? std::string(cat_buf) : "";
                    if (candidate_schema == "information_schema" || candidate_catalog == "information_schema") {
                        continue;
                    }

                    table_catalog = candidate_catalog;
                    table_schema = candidate_schema;
                    table_name = std::string(name_buf);
                    return true;
                }
                return false;
            };

            // Strategy 1: Look for user tables (type = 'TABLE')
            if (!discover_table(SqlWcharBuf("TABLE").ptr(), SQL_NTS)) {
                // Strategy 2: Look for system tables
                if (!discover_table(SqlWcharBuf("SYSTEM TABLE").ptr(), SQL_NTS)) {
                    // Strategy 3: Look for any table type
                    discover_table(nullptr, 0);
                }
            }

            if (table_name.empty()) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "No tables found in catalog for SQLColumnsW test";
                return;
            }

            // Call SQLColumnsW with the discovered table name AND its catalog/schema
            SQLRETURN ret = SQLColumnsW(stmt.get_handle(),
                table_catalog.empty() ? nullptr : SqlWcharBuf(table_catalog.c_str()).ptr(),
                table_catalog.empty() ? 0 : SQL_NTS,
                table_schema.empty() ? nullptr : SqlWcharBuf(table_schema.c_str()).ptr(),
                table_schema.empty() ? 0 : SQL_NTS,
                SqlWcharBuf(table_name.c_str()).ptr(), SQL_NTS,
                SqlWcharBuf("%").ptr(), SQL_NTS);

            if (!SQL_SUCCEEDED(ret)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "SQLColumnsW call did not succeed";
                r.suggestion = "Verify driver supports Unicode catalog functions";
                return;
            }

            int col_count = 0;
            while (SQL_SUCCEEDED(SQLFetch(stmt.get_handle())) && col_count < 50) {
                col_count++;
            }

            std::ostringstream actual;
            actual << "SQLColumnsW returned " << col_count << " column(s) for " << table_name;
            if (!table_catalog.empty()) actual << " (catalog=" << table_catalog << ")";
            r.actual = actual.str();

            if (col_count == 0) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.suggestion = "SQLColumnsW returned no columns; table may not exist in catalog";
            }
        });
}

TestResult UnicodeTests::test_string_truncation_wchar() {
    return run_test(
        "test_string_truncation_wchar", "SQLGetInfo",
        "String truncation with SQLWCHAR* buffers returns 01004 and correct byte length",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 String Truncation: 01004 with byte-based length for Unicode",
        [&](TestResult& r) {
            // Step 1: Probe several info types with a full-size buffer to find one
            // that returns a string long enough to guarantee truncation.
            // The actual string length varies by driver and server.
            struct InfoProbe { SQLUSMALLINT type; const char* name; };
            InfoProbe probes[] = {
                { SQL_DBMS_NAME, "SQL_DBMS_NAME" },
                { SQL_DBMS_VER, "SQL_DBMS_VER" },
                { SQL_SERVER_NAME, "SQL_SERVER_NAME" },
                { SQL_DRIVER_VER, "SQL_DRIVER_VER" },
            };

            SQLUSMALLINT chosen_type = 0;
            SQLSMALLINT full_byte_len = 0;     // bytes, excluding NUL

            for (const auto& p : probes) {
                SQLWCHAR full_buf[256] = {0};
                SQLSMALLINT len = 0;
                SQLRETURN probe_ret = SQLGetInfoW(conn_.get_handle(), p.type,
                                                  full_buf, sizeof(full_buf), &len);
                if (SQL_SUCCEEDED(probe_ret) && len > static_cast<SQLSMALLINT>(sizeof(SQLWCHAR))) {
                    // Need at least 2 characters of data for truncation to be meaningful
                    if (len > full_byte_len) {
                        full_byte_len = len;
                        chosen_type = p.type;
                    }
                }
            }

            if (chosen_type == 0 || full_byte_len <= static_cast<SQLSMALLINT>(sizeof(SQLWCHAR))) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "All string info types returned very short values; truncation test inconclusive";
                return;
            }

            // Step 2: Craft a buffer that is exactly half the full data length.
            // This guarantees truncation.  BufferLength is in bytes for W-functions.
            // We need room for at least 1 wide char + NUL, but less than the full string.
            SQLSMALLINT tiny_byte_len = std::max(
                static_cast<SQLSMALLINT>(2 * sizeof(SQLWCHAR)),   // minimum: 1 char + NUL
                static_cast<SQLSMALLINT>(full_byte_len / 2));
            // Ensure it's a multiple of sizeof(SQLWCHAR)
            tiny_byte_len = static_cast<SQLSMALLINT>(
                (tiny_byte_len / sizeof(SQLWCHAR)) * sizeof(SQLWCHAR));

            std::vector<SQLWCHAR> tiny_buf(tiny_byte_len / sizeof(SQLWCHAR), 0);
            SQLSMALLINT needed_len = 0;

            SQLRETURN ret = SQLGetInfoW(conn_.get_handle(), chosen_type,
                                        tiny_buf.data(), tiny_byte_len, &needed_len);

            if (ret == SQL_SUCCESS_WITH_INFO) {
                // A21: both `expected` and `actual` claim this checks for
                // 01004, and it only ever checked that *some* warning came
                // back. Any unrelated 01000 satisfied it.
                const std::string state =
                    first_sqlstate(SQL_HANDLE_DBC, conn_.get_handle());

                std::ostringstream actual;
                actual << "Truncation returned " << (state.empty() ? "no SQLSTATE" : state)
                       << ", needed " << needed_len
                       << " bytes, buffer was " << tiny_byte_len
                       << " bytes (full=" << full_byte_len << ")";
                r.actual = actual.str();

                if (state != "01004") {
                    r.status = TestStatus::FAIL;
                    r.suggestion =
                        "Truncating a string output must post SQLSTATE 01004 "
                        "(String data, right-truncated). A bare "
                        "SQL_SUCCESS_WITH_INFO with some other state leaves the "
                        "application unable to tell truncation from any other "
                        "warning.";
                } else if (needed_len <= 0) {
                    r.status = TestStatus::FAIL;
                    r.suggestion = "pcbInfoValue should report total bytes needed (excl NUL) even on truncation";
                } else if (needed_len < tiny_byte_len) {
                    r.status = TestStatus::FAIL;
                    r.suggestion = "pcbInfoValue (" + std::to_string(needed_len) +
                                       ") is less than buffer size (" + std::to_string(tiny_byte_len) +
                                       ") despite truncation";
                }
            } else if (ret == SQL_SUCCESS) {
                // String fit in the tiny buffer — shouldn't happen with our probing
                r.actual = "String fit in " + std::to_string(tiny_byte_len) +
                               " byte buffer (full=" + std::to_string(full_byte_len) +
                               " bytes); truncation test inconclusive";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
            } else {
                r.actual = "SQLGetInfoW failed unexpectedly (ret=" + std::to_string(ret) + ")";
                r.status = TestStatus::SKIP_INCONCLUSIVE;
            }
        });
}

// ── PORT plan §4.7 — WCHAR round-trip with non-ASCII / non-BMP codepoints ─

namespace {

// Build an SQLWCHAR buffer from a list of explicit codepoints, encoded for
// *this build's* SQLWCHAR width. NUL-terminated.
//
// A10. This used to emit a UTF-16 surrogate pair for every supplementary
// codepoint regardless of width. SQLWCHAR is 2 bytes on Windows, but on a
// unixODBC build where SQLWCHAR is wchar_t it is 4, and there a "surrogate
// pair" is two lone surrogates — ill-formed UTF-32 that iconv rejects or
// replaces. A correct driver was therefore FAILed on those platforms for
// refusing to round-trip data that was invalid to begin with.
//
// With a 4-byte unit the encoding is simply the scalar value.
std::vector<SQLWCHAR> make_wchar_buf(std::initializer_list<uint32_t> codepoints) {
    std::vector<SQLWCHAR> out;
    out.reserve(codepoints.size() + 2);
    for (uint32_t cp : codepoints) {
        if (sizeof(SQLWCHAR) >= 4 || cp <= 0xFFFFu) {
            out.push_back(static_cast<SQLWCHAR>(cp));
        } else {
            // UTF-16 surrogate pair encoding
            const uint32_t v = cp - 0x10000u;
            out.push_back(static_cast<SQLWCHAR>(0xD800u | (v >> 10)));
            out.push_back(static_cast<SQLWCHAR>(0xDC00u | (v & 0x3FFu)));
        }
    }
    out.push_back(0);
    return out;
}

// How U+1F600 must come back in this build's SQLWCHAR width, and a
// description of it for the report. A10: the probe below asserted the
// two-unit UTF-16 answer unconditionally.
std::vector<SQLWCHAR> expected_emoji_units() {
    auto v = make_wchar_buf({0x1F600u});
    v.pop_back();   // drop the terminator
    return v;
}

std::string expected_emoji_description() {
    return sizeof(SQLWCHAR) >= 4
               ? std::string("scalar [U+1F600] in a 4-byte SQLWCHAR")
               : std::string("surrogate pair [U+D83D U+DE00] in a 2-byte SQLWCHAR");
}

// INSERT a WCHAR parameter into ODBC_TEST_NVARCHAR via SQLBindParameter +
// SQL_C_WCHAR / SQL_WVARCHAR. Returns false on bind/execute failure with
// `err` filled.
bool insert_wchar_value(core::OdbcConnection& conn,
                        const std::string& table_name,
                        std::vector<SQLWCHAR>& wbuf,
                        std::string& err) {
    core::OdbcStatement stmt(conn);
    const std::string sql = "INSERT INTO " + table_name +
                            " (ID, VAL) VALUES (1, ?)";
    SQLRETURN rc = SQLPrepare(stmt.get_handle(),
                              reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql.c_str())),
                              SQL_NTS);
    if (!SQL_SUCCEEDED(rc)) {
        err = "SQLPrepare returned " + std::to_string(rc);
        return false;
    }
    const SQLLEN char_count = static_cast<SQLLEN>(wbuf.size() - 1); // excl NUL
    SQLLEN ind_bytes = char_count * static_cast<SQLLEN>(sizeof(SQLWCHAR));
    rc = SQLBindParameter(stmt.get_handle(), 1, SQL_PARAM_INPUT,
                          SQL_C_WCHAR, SQL_WVARCHAR,
                          /*column_size*/ static_cast<SQLULEN>(char_count),
                          /*decimal_digits*/ 0,
                          wbuf.data(),
                          static_cast<SQLLEN>(wbuf.size() * sizeof(SQLWCHAR)),
                          &ind_bytes);
    if (!SQL_SUCCEEDED(rc)) {
        err = "SQLBindParameter returned " + std::to_string(rc);
        return false;
    }
    rc = SQLExecute(stmt.get_handle());
    if (!SQL_SUCCEEDED(rc)) {
        err = "SQLExecute returned " + std::to_string(rc);
        return false;
    }
    return true;
}

std::string wchars_to_hex(const SQLWCHAR* p, size_t n) {
    std::ostringstream os;
    os << std::hex << std::setfill('0');
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) os << ' ';
        os << "U+" << std::setw(4) << static_cast<unsigned>(p[i]);
    }
    return os.str();
}

} // namespace

TestResult UnicodeTests::test_wchar_roundtrip_non_ascii() {
    return run_test(
        "test_wchar_roundtrip_non_ascii", "SQLBindParameter+SQLGetData(SQL_C_WCHAR)",
        "Non-ASCII WCHAR codepoints (Latin accents, Euro symbol, CJK) "
        "round-trip byte-for-byte through INSERT/SELECT",
        Severity::ERR, ConformanceLevel::CORE,
        "ODBC 3.8 SQL_C_WCHAR — UTF-16; system codepage must not be involved",
        [&](TestResult& r) {
            const std::string table = "ODBC_TEST_NVARCHAR";
            // Try NVARCHAR first; fall back to VARCHAR for engines where
            // the regular VARCHAR is already Unicode-capable.
            RoundTripTableGuard tbl(conn_, table, "NVARCHAR(64)");
            if (!tbl.ok()) {
                RoundTripTableGuard tbl2(conn_, table, "VARCHAR(64)");
                if (!tbl2.ok()) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not create round-trip table "
                               "(neither NVARCHAR nor VARCHAR DDL accepted): "
                             + tbl2.last_error();
                    return;
                }
                // Move ownership: drop guard 1 (no-op since !ok) keep guard 2
                // by reusing this scope. RoundTripTableGuard is non-movable,
                // so we restructure: do the work inline instead.
                std::vector<SQLWCHAR> input = make_wchar_buf({
                    'c','a','f',0x00E9,           // café
                    ' ',
                    0x20AC,                       // €
                    ' ',
                    0x6F22,0x5B57,                // 漢字
                });
                std::string err;
                if (!insert_wchar_value(conn_, table, input, err)) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "INSERT WCHAR via VARCHAR fallback: " + err;
                    return;
                }
                core::OdbcStatement sel(conn_);
                sel.execute("SELECT VAL FROM " + table);
                SQLRETURN rc = SQLFetch(sel.get_handle());
                if (!SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed";
                    return;
                }
                SQLWCHAR out[128] = {0};
                SQLLEN ind = 0;
                rc = SQLGetData(sel.get_handle(), 1, SQL_C_WCHAR,
                                out, sizeof(out), &ind);
                if (!SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLGetData(SQL_C_WCHAR) returned " + std::to_string(rc);
                    return;
                }
                const size_t expected_chars = input.size() - 1;
                size_t out_chars = 0;
                while (out_chars < 127 && out[out_chars] != 0) ++out_chars;
                if (out_chars != expected_chars ||
                    std::memcmp(out, input.data(),
                                expected_chars * sizeof(SQLWCHAR)) != 0) {
                    r.status = TestStatus::FAIL;
                    r.actual = "expected [" + wchars_to_hex(input.data(), expected_chars)
                             + "] got [" + wchars_to_hex(out, out_chars) + "]";
                    r.suggestion = "Driver appears to re-encode through a "
                                   "narrow codepage. SQL_C_WCHAR data must "
                                   "preserve every codepoint regardless of "
                                   "system locale.";
                    return;
                }
                r.actual = "VARCHAR fallback: round-trip preserved "
                         + std::to_string(expected_chars) + " WCHARs ["
                         + wchars_to_hex(input.data(), expected_chars) + "]";
                return;
            }

            std::vector<SQLWCHAR> input = make_wchar_buf({
                'c','a','f',0x00E9,
                ' ',
                0x20AC,
                ' ',
                0x6F22,0x5B57,
            });
            std::string err;
            if (!insert_wchar_value(conn_, table, input, err)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = "INSERT WCHAR: " + err;
                return;
            }

            core::OdbcStatement sel(conn_);
            sel.execute("SELECT VAL FROM " + table);
            SQLRETURN rc = SQLFetch(sel.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLFetch failed";
                return;
            }
            SQLWCHAR out[128] = {0};
            SQLLEN ind = 0;
            rc = SQLGetData(sel.get_handle(), 1, SQL_C_WCHAR,
                            out, sizeof(out), &ind);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData(SQL_C_WCHAR) returned " + std::to_string(rc);
                return;
            }
            const size_t expected_chars = input.size() - 1;
            size_t out_chars = 0;
            while (out_chars < 127 && out[out_chars] != 0) ++out_chars;
            if (out_chars != expected_chars ||
                std::memcmp(out, input.data(),
                            expected_chars * sizeof(SQLWCHAR)) != 0) {
                r.status = TestStatus::FAIL;
                r.actual = "expected [" + wchars_to_hex(input.data(), expected_chars)
                         + "] got [" + wchars_to_hex(out, out_chars) + "]";
                r.suggestion = "Driver appears to re-encode through a narrow "
                               "codepage. SQL_C_WCHAR data must preserve every "
                               "codepoint regardless of system locale.";
                return;
            }
            r.actual = "round-trip preserved " + std::to_string(expected_chars)
                     + " WCHARs [" + wchars_to_hex(input.data(), expected_chars) + "]";
        });
}

TestResult UnicodeTests::test_wchar_surrogate_pair_preserved() {
    return run_test(
        "test_wchar_surrogate_pair_preserved", "SQLBindParameter+SQLGetData(SQL_C_WCHAR)",
        "Supplementary-plane codepoint U+1F600 round-trips intact in this "
        "build's SQLWCHAR width",
        Severity::WARNING, ConformanceLevel::CORE,
        "ODBC 3.8 SQL_C_WCHAR — UTF-16 includes surrogate pairs for non-BMP",
        [&](TestResult& r) {
            const std::string table = "ODBC_TEST_NVARCHAR_SP";
            RoundTripTableGuard tbl(conn_, table, "NVARCHAR(16)");
            if (!tbl.ok()) {
                RoundTripTableGuard tbl2(conn_, table, "VARCHAR(16)");
                if (!tbl2.ok()) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = "Could not create table: " + tbl2.last_error();
                    return;
                }
                std::vector<SQLWCHAR> input = make_wchar_buf({0x1F600u});
                std::string err;
                if (!insert_wchar_value(conn_, table, input, err)) {
                    r.status = TestStatus::SKIP_INCONCLUSIVE;
                    r.actual = err;
                    return;
                }
                core::OdbcStatement sel(conn_);
                sel.execute("SELECT VAL FROM " + table);
                SQLRETURN rc = SQLFetch(sel.get_handle());
                if (!SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLFetch failed";
                    return;
                }
                SQLWCHAR out[8] = {0};
                SQLLEN ind = 0;
                rc = SQLGetData(sel.get_handle(), 1, SQL_C_WCHAR,
                                out, sizeof(out), &ind);
                if (!SQL_SUCCEEDED(rc)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "SQLGetData returned " + std::to_string(rc);
                    return;
                }
                size_t out_chars = 0;
                while (out_chars < 7 && out[out_chars] != 0) ++out_chars;
                const auto want = expected_emoji_units();
                if (out_chars != want.size() ||
                    !std::equal(want.begin(), want.end(), out)) {
                    r.status = TestStatus::FAIL;
                    r.actual = "expected " + expected_emoji_description() + " got ["
                             + wchars_to_hex(out, out_chars) + "]";
                    r.suggestion = "Driver dropped or normalized the supplementary "
                                   "codepoint. SQL_C_WCHAR must preserve every "
                                   "codepoint in the platform's UTF-16 or UTF-32 "
                                   "encoding.";
                    return;
                }
                r.actual = "VARCHAR fallback: " + expected_emoji_description() +
                           " preserved";
                return;
            }

            std::vector<SQLWCHAR> input = make_wchar_buf({0x1F600u});
            std::string err;
            if (!insert_wchar_value(conn_, table, input, err)) {
                r.status = TestStatus::SKIP_INCONCLUSIVE;
                r.actual = err;
                return;
            }
            core::OdbcStatement sel(conn_);
            sel.execute("SELECT VAL FROM " + table);
            SQLRETURN rc = SQLFetch(sel.get_handle());
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLFetch failed";
                return;
            }
            SQLWCHAR out[8] = {0};
            SQLLEN ind = 0;
            rc = SQLGetData(sel.get_handle(), 1, SQL_C_WCHAR,
                            out, sizeof(out), &ind);
            if (!SQL_SUCCEEDED(rc)) {
                r.status = TestStatus::FAIL;
                r.actual = "SQLGetData returned " + std::to_string(rc);
                return;
            }
            size_t out_chars = 0;
            while (out_chars < 7 && out[out_chars] != 0) ++out_chars;
            const auto want = expected_emoji_units();
            if (out_chars != want.size() ||
                !std::equal(want.begin(), want.end(), out)) {
                r.status = TestStatus::FAIL;
                r.actual = "expected " + expected_emoji_description() + " got ["
                         + wchars_to_hex(out, out_chars) + "]";
                r.suggestion = "Driver dropped or normalized the supplementary "
                               "codepoint. SQL_C_WCHAR must preserve every "
                               "codepoint in the platform's UTF-16 or UTF-32 "
                               "encoding.";
                return;
            }
            r.actual = expected_emoji_description() + " preserved (U+1F600)";
        });
}

} // namespace odbc_crusher::tests
