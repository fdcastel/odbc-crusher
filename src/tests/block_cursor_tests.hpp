#pragma once

#include "test_base.hpp"

#include <optional>
#include <string>

namespace odbc_crusher::tests {

// Block Cursor Tests — IMPROVEMENT_PLAN_V2.md P1–P5.
//
// The suite had no block-cursor data path at all. `test_rowset_size` in
// Advanced Features set `SQL_ATTR_ROW_ARRAY_SIZE = 100`, read it back, and
// passed — it never fetched. Nothing bound an array of columns, fetched more
// than one row at a time, read a rows-fetched counter, inspected a row-status
// array, or applied a bind offset.
//
// That is four of the thirteen pull requests in Firebird ODBC 3.5.1-rc2 with
// no probe that could fail on them: #301 (SQLPrepare discarding the fetch
// pointers), #304 (eight settable attributes unreadable), #306 (column-wise
// rowsets ignoring the bind offset) and #307 (SQL_ATTR_KEYSET_SIZE overwriting
// the rowset size). Block cursors are also how every ODBC reporting tool reads
// bulk data, so the gap is not academic.
//
// Everything here is Level 2 and a driver may decline it. Declining is a skip;
// accepting the attribute and then not honouring it is the failure this
// category exists to name.
class BlockCursorTests : public TestBase {
public:
    explicit BlockCursorTests(core::OdbcConnection& conn,
                              std::string connection_string = {})
        : TestBase(conn, std::move(connection_string)) {}

    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Block Cursor Tests"; }

private:
    // Six rows in a table, and the query that reads them back in order.
    //
    // The first version of these probes built the six rows from a UNION ALL of
    // literal SELECTs, which assumes an engine that evaluates one. The mock
    // driver does not — it implements block fetch correctly (D11) but returned
    // a single row for that statement, and the probes reported it as a driver
    // defect. A table is what the rest of the suite uses for exactly this
    // reason, and RoundTripTableGuard already handles the DDL dialects.
    //
    // Created once for the category and dropped with it: six probes creating
    // and dropping the same table would be six chances for a cleanup to fail.
    std::optional<RoundTripTableGuard> table_;
    std::string last_setup_error_;

    bool ensure_block_table(TestResult& r, std::string& out_query);

    // P1 — the substrate: bind arrays, fetch a rowset, check every slot.
    TestResult test_rowset_fetch_fills_bound_arrays();

    // P2 — #301: statement attributes are not cleared by SQLPrepare.
    TestResult test_fetch_pointers_survive_prepare();

    // P3 — #306: a column-wise rowset honours SQL_ATTR_ROW_BIND_OFFSET_PTR.
    TestResult test_row_bind_offset_applies_column_wise();

    // P4 — #307: a keyset size is not a rowset size.
    TestResult test_keyset_size_is_not_the_rowset_size();

    // P5 — #304: everything SQLSetStmtAttr accepts, SQLGetStmtAttr returns.
    TestResult test_settable_pointer_attrs_round_trip();
};

} // namespace odbc_crusher::tests
