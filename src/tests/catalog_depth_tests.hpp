#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Catalog Function Depth Tests (Phase 15.2b)
class CatalogDepthTests : public TestBase {
public:
    explicit CatalogDepthTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Catalog Function Depth Tests"; }
    
private:
    TestResult test_tables_search_patterns();
    TestResult test_columns_result_set_shape();
    TestResult test_statistics_result();
    TestResult test_procedures_result();
    TestResult test_privileges_result();
    TestResult test_catalog_null_parameters();
};

} // namespace odbc_crusher::tests
