#pragma once

#include "test_base.hpp"

namespace odbc_crusher::tests {

// Connection-related tests (Phase 3)
class ConnectionTests : public TestBase {
public:
    // C13: this category takes the connection string, because
    // test_reused_connection_starts_clean needs a second connection it can
    // leave in a bad state. Declaring the two-argument constructor is how a
    // category asks for one.
    explicit ConnectionTests(core::OdbcConnection& conn,
                             const std::string& connection_string = {})
        : TestBase(conn, connection_string) {}
    
    std::vector<TestResult> run() override;
    std::string category_name() const override { return "Connection Tests"; }
    
private:
    TestResult test_connection_info();
    TestResult test_connection_timeout();
    TestResult test_connection_string_format();
    TestResult test_multiple_statements();
    TestResult test_connection_attributes();
    TestResult test_connection_pooling();
    TestResult test_reused_connection_starts_clean();   // I8
};

} // namespace odbc_crusher::tests
