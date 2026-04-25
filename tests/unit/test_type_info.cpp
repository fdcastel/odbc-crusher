#include <gtest/gtest.h>
#include "discovery/type_info.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include <iostream>

using namespace odbc_crusher;

class TypeInfoTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<core::OdbcEnvironment>();
    }

    std::unique_ptr<core::OdbcEnvironment> env;
};

// Smoke test: TypeInfo::collect() round-trips through the mock driver
// without throwing and reports a non-zero type count. Skips when the
// mock driver isn't registered on the host — environment problem, not
// a regression.
TEST_F(TypeInfoTest, CollectMockDriverTypes) {
    core::OdbcConnection conn(*env);
    try {
        conn.connect("Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;");
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Mock ODBC Driver not registered on this host: " << e.what();
    }

    discovery::TypeInfo info(conn);
    EXPECT_NO_THROW(info.collect());
    EXPECT_GT(info.count(), 0);

    std::cout << "Found " << info.count() << " data types\n"
              << info.format_summary() << "\n";
}
