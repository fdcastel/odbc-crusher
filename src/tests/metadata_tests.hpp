#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Metadata/Catalog tests (Phase 5)
class MetadataTests : public TestBase {
public:
    explicit MetadataTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Metadata/Catalog Tests"; }
    
private:
    TestResult test_tables_catalog();
    TestResult test_columns_catalog();
    TestResult test_primary_keys();
    TestResult test_foreign_keys();
    TestResult test_statistics();
    TestResult test_special_columns();
    TestResult test_table_privileges();
    TestResult test_desc_unsigned_on_signed_integer();
    TestResult test_count_star_result_metadata();

    // PORT plan §4.8 — `SQLProcedures` / `SQLProcedureColumns` discovery.
    // Both are Core conformance and are commonly forgotten by driver
    // developers who tested the table-side catalog functions.
    TestResult test_sqlprocedures_smoke();
    TestResult test_sqlprocedurecolumns_smoke();

    // P13 (IMPROVEMENT_PLAN_V2): SQL_DBMS_VER is read three times in this
    // suite and its value has never been asserted. A prober cannot know
    // the product version independently - but the string carries two
    // claims about it, and they must agree.
    TestResult test_dbms_version_agrees_with_itself();
};

} // namespace odbc_crusher::tests
