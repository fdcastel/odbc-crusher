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

TEST(ConfigTest, ParseSilentCorruptionDefault) {
    DriverConfig config = parse_connection_string("Driver={Mock};");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::None);
}

TEST(ConfigTest, ParseSilentCorruptionDropInserts) {
    DriverConfig config = parse_connection_string("SilentCorruption=DropInserts;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::DropInserts);
}

TEST(ConfigTest, ParseSilentCorruptionMangleVarchar) {
    DriverConfig config = parse_connection_string("SilentCorruption=MangleVarchar;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::MangleVarchar);
}

TEST(ConfigTest, ParseSilentCorruptionTruncateNumeric) {
    DriverConfig config = parse_connection_string("SilentCorruption=TruncateNumeric;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::TruncateNumeric);
}

TEST(ConfigTest, ParseSilentCorruptionNullAsEmpty) {
    DriverConfig config = parse_connection_string("SilentCorruption=NullAsEmpty;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::NullAsEmpty);
}

TEST(ConfigTest, ParseSilentCorruptionMangleUnicode) {
    DriverConfig config = parse_connection_string("SilentCorruption=MangleUnicode;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::MangleUnicode);
}

TEST(ConfigTest, ParseNativeSqlPassThroughDefaultsFalse) {
    DriverConfig config = parse_connection_string("");
    EXPECT_FALSE(config.native_sql_pass_through);
}

TEST(ConfigTest, ParseNativeSqlPassThroughTrue) {
    DriverConfig config = parse_connection_string("NativeSqlPassThrough=true;");
    EXPECT_TRUE(config.native_sql_pass_through);
}

TEST(ConfigTest, ParseNativeSqlPassThroughOnlyTrueLiteralEnables) {
    DriverConfig config = parse_connection_string("NativeSqlPassThrough=yes;");
    EXPECT_FALSE(config.native_sql_pass_through);
}

TEST(ConfigTest, ParseSilentCorruptionUnknownFallsBackToNone) {
    DriverConfig config = parse_connection_string("SilentCorruption=Bogus;");
    EXPECT_EQ(config.silent_corruption, DriverConfig::SilentCorruptionMode::None);
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

// ── Adversarial / edge-case connection strings ─────────────────────────────
//
// IMPROVEMENT_PLAN.md §3.7 — the parser is hand-rolled and used to accept
// plenty of malformed shapes silently. These cases document what the parser
// does under each malformed input so regressions trip a test.

TEST(ConfigTest, EmbeddedSemicolonInBracedValue) {
    // A semicolon inside braces must not split the pair.
    auto pairs = parse_connection_string_pairs(
        "Driver={Some;Odd;Driver};Mode=Success;");
    EXPECT_EQ(pairs["driver"], "Some;Odd;Driver");
    EXPECT_EQ(pairs["mode"], "Success");
}

TEST(ConfigTest, DoubleEqualsInValue) {
    // Only the first `=` separates key from value — the rest is part of the value.
    auto pairs = parse_connection_string_pairs("Password=a=b=c;");
    EXPECT_EQ(pairs["password"], "a=b=c");
}

TEST(ConfigTest, EmptyValue) {
    auto pairs = parse_connection_string_pairs("Key=;Mode=Success;");
    EXPECT_EQ(pairs["key"], "");
    EXPECT_EQ(pairs["mode"], "Success");
}

TEST(ConfigTest, KeyWithoutEquals) {
    // Malformed fragment without `=` must be silently skipped, not crash.
    auto pairs = parse_connection_string_pairs("Orphan;Mode=Success;");
    EXPECT_TRUE(pairs.find("orphan") == pairs.end());
    EXPECT_EQ(pairs["mode"], "Success");
}

TEST(ConfigTest, WhitespaceAroundKeyAndValue) {
    auto pairs = parse_connection_string_pairs("  Mode  =  Success  ;");
    EXPECT_EQ(pairs["mode"], "Success");
}

TEST(ConfigTest, UnbalancedOpeningBraceDoesNotCrash) {
    // Open brace without a matching close: parser must still terminate.
    auto pairs = parse_connection_string_pairs("Driver={Unterminated;Mode=Success;");
    // Everything after the `{` is in "brace mode" — the `;` doesn't split.
    // We don't pin the exact resulting pair, we just guarantee termination.
    SUCCEED();
    (void)pairs;
}

TEST(ConfigTest, RepeatedKeyLastWins) {
    auto pairs = parse_connection_string_pairs("Mode=Success;Mode=Failure;");
    EXPECT_EQ(pairs["mode"], "Failure");
}

TEST(ConfigTest, CaseInsensitiveKey) {
    auto pairs = parse_connection_string_pairs("MODE=Success;CaTaLoG=Large;");
    EXPECT_EQ(pairs["mode"], "Success");
    EXPECT_EQ(pairs["catalog"], "Large");
}

TEST(ConfigTest, NegativeIntegerValue) {
    // ResultSetSize=-1 used to round-trip as -1; document current behaviour.
    DriverConfig config = parse_connection_string("ResultSetSize=-5;");
    EXPECT_EQ(config.result_set_size, -5);
}

TEST(ConfigTest, NonNumericIntegerFallsBackToDefault) {
    DriverConfig config = parse_connection_string("ResultSetSize=not-a-number;");
    EXPECT_EQ(config.result_set_size, 100);  // default
}

TEST(ConfigTest, VeryLongValueDoesNotCrash) {
    std::string long_value(8192, 'x');
    auto pairs = parse_connection_string_pairs("Key=" + long_value + ";");
    EXPECT_EQ(pairs["key"].size(), 8192u);
}
