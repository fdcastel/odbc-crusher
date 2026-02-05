#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Connection-related tests (Phase 3)
class ConnectionTests : public TestBase {
public:
    explicit ConnectionTests(core::OdbcConnection& conn)
        : TestBase(conn) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Connection Tests"; }
    
private:
    TestResult test_connection_info();
    TestResult test_connection_timeout();
    TestResult test_connection_string_format();
    TestResult test_multiple_statements();
    TestResult test_connection_attributes();
    TestResult test_connection_pooling();
};

} // namespace odbc_crusher::tests
