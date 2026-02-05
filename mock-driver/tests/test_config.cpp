// Tests for Connection String Configuration Parsing
#include <gtest/gtest.h>
#include "driver/config.hpp"

using namespace mock_odbc;

TEST(ConfigTest, ParseEmptyConnectionString) {
    DriverConfig config = parse_connection_string("");
    EXPECT_EQ(config.mode, BehaviorMode::Success);
    EXPECT_EQ(config.catalog, "Default");
    EXPECT_EQ(config.result_set_size, 100);
}

TEST(ConfigTest, ParseDriverOnly) {
    DriverConfig config = parse_connection_string("Driver={Mock ODBC Driver};");
    EXPECT_EQ(config.mode, BehaviorMode::Success);
}

TEST(ConfigTest, ParseModeSuccess) {
    DriverConfig config = parse_connection_string("Driver={Mock};Mode=Success;");
    EXPECT_EQ(config.mode, BehaviorMode::Success);
}

TEST(ConfigTest, ParseModeFailure) {
    DriverConfig config = parse_connection_string("Driver={Mock};Mode=Failure;");
    EXPECT_EQ(config.mode, BehaviorMode::Failure);
}

TEST(ConfigTest, ParseModeRandom) {
    DriverConfig config = parse_connection_string("Driver={Mock};Mode=Random;");
    EXPECT_EQ(config.mode, BehaviorMode::Random);
}

TEST(ConfigTest, ParseModePartial) {
    DriverConfig config = parse_connection_string("Driver={Mock};Mode=Partial;");
    EXPECT_EQ(config.mode, BehaviorMode::Partial);
}

TEST(ConfigTest, ParseCatalog) {
    DriverConfig config = parse_connection_string("Catalog=Empty;");
    EXPECT_EQ(config.catalog, "Empty");
}

TEST(ConfigTest, ParseResultSetSize) {
    DriverConfig config = parse_connection_string("ResultSetSize=50;");
    EXPECT_EQ(config.result_set_size, 50);
}

TEST(ConfigTest, ParseFailOn) {
    DriverConfig config = parse_connection_string("Mode=Partial;FailOn=SQLExecute,SQLFetch;");
    EXPECT_EQ(config.mode, BehaviorMode::Partial);
    ASSERT_EQ(config.fail_on.size(), 2u);
    EXPECT_EQ(config.fail_on[0], "SQLExecute");
    EXPECT_EQ(config.fail_on[1], "SQLFetch");
}

TEST(ConfigTest, ParseErrorCode) {
    DriverConfig config = parse_connection_string("ErrorCode=08001;");
    EXPECT_EQ(config.error_code, "08001");
}

TEST(ConfigTest, ParseLatency) {
    DriverConfig config = parse_connection_string("Latency=100ms;");
    EXPECT_EQ(config.latency.count(), 100);
}

TEST(ConfigTest, ParseMaxConnections) {
    DriverConfig config = parse_connection_string("MaxConnections=5;");
    EXPECT_EQ(config.max_connections, 5);
}

TEST(ConfigTest, ParseComplexConnectionString) {
    DriverConfig config = parse_connection_string(
        "Driver={Mock ODBC Driver};"
        "Mode=Partial;"
        "Catalog=Default;"
        "ResultSetSize=25;"
        "FailOn=SQLConnect;"
        "ErrorCode=08001;"
        "MaxConnections=10;"
    );
    
    EXPECT_EQ(config.mode, BehaviorMode::Partial);
    EXPECT_EQ(config.catalog, "Default");
    EXPECT_EQ(config.result_set_size, 25);
    ASSERT_EQ(config.fail_on.size(), 1u);
    EXPECT_EQ(config.fail_on[0], "SQLConnect");
    EXPECT_EQ(config.error_code, "08001");
    EXPECT_EQ(config.max_connections, 10);
}

TEST(ConfigTest, ShouldFailSuccess) {
    DriverConfig config;
    config.mode = BehaviorMode::Success;
    EXPECT_FALSE(config.should_fail("SQLExecute"));
    EXPECT_FALSE(config.should_fail("SQLFetch"));
}

TEST(ConfigTest, ShouldFailFailure) {
    DriverConfig config;
    config.mode = BehaviorMode::Failure;
    EXPECT_TRUE(config.should_fail("SQLExecute"));
    EXPECT_TRUE(config.should_fail("SQLFetch"));
}

TEST(ConfigTest, ShouldFailPartial) {
    DriverConfig config;
    config.mode = BehaviorMode::Partial;
    config.fail_on = {"SQLExecute", "SQLConnect"};
    
    EXPECT_TRUE(config.should_fail("SQLExecute"));
    EXPECT_TRUE(config.should_fail("sqlexecute"));  // Case insensitive
    EXPECT_TRUE(config.should_fail("SQLConnect"));
    EXPECT_FALSE(config.should_fail("SQLFetch"));
    EXPECT_FALSE(config.should_fail("SQLPrepare"));
}

TEST(ConfigTest, ParseConnectionStringPairs) {
    auto pairs = parse_connection_string_pairs(
        "Driver={Mock ODBC Driver};Server=localhost;Database=test;UID=user;PWD=pass;");
    
    EXPECT_EQ(pairs["driver"], "Mock ODBC Driver");
    EXPECT_EQ(pairs["server"], "localhost");
    EXPECT_EQ(pairs["database"], "test");
    EXPECT_EQ(pairs["uid"], "user");
    EXPECT_EQ(pairs["pwd"], "pass");
}

TEST(ConfigTest, ParseConnectionStringNoTrailingSemicolon) {
    auto pairs = parse_connection_string_pairs(
        "Driver={Mock};Mode=Success");
    
    EXPECT_EQ(pairs["driver"], "Mock");
    EXPECT_EQ(pairs["mode"], "Success");
}

TEST(ConfigTest, GetStringValue) {
    std::unordered_map<std::string, std::string> pairs = {
        {"driver", "Mock"},
        {"mode", "Success"}
    };
    
    EXPECT_EQ(get_string_value(pairs, "driver", ""), "Mock");
    EXPECT_EQ(get_string_value(pairs, "DRIVER", ""), "Mock");  // Case insensitive
    EXPECT_EQ(get_string_value(pairs, "missing", "default"), "default");
}

TEST(ConfigTest, GetIntValue) {
    std::unordered_map<std::string, std::string> pairs = {
        {"count", "42"},
        {"invalid", "abc"}
    };
    
    EXPECT_EQ(get_int_value(pairs, "count", 0), 42);
    EXPECT_EQ(get_int_value(pairs, "missing", 100), 100);
    EXPECT_EQ(get_int_value(pairs, "invalid", 50), 50);  // Returns default on parse error
}
