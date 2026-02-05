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
    TestResult test_statistics();
    TestResult test_special_columns();
};

} // namespace odbc_crusher::tests
