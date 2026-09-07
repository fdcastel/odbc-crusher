// TestBase::get_data_full / quote_identifier / verify_rows_persisted —
// IMPROVEMENT_PLAN.md A19.
//
// verify_rows_persisted is the helper that decides whether a driver silently
// dropped or corrupted rows, and it reports at CRITICAL. Before A19 it read
// every value into a bare `char buf[256]`: SQL_SUCCEEDED accepts the 01004
// that comes with truncation, so a value of 256 characters or more came back
// short and was then compared against what the probe had inserted — a correct
// driver reported as corrupting data. NULL arrived as an empty std::string,
// indistinguishable from '', in the one helper the header advertises for
// NULL-versus-empty work.
//
// The tests that need a driver use the mock and skip when it isn't
// registered; the pure ones (is_bare_identifier) always run.
#include <gtest/gtest.h>

#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_statement.hpp"
#include "tests/test_base.hpp"

#include <memory>
#include <string>

using namespace odbc_crusher;

namespace {

// Minimal concrete TestBase — verify_rows_persisted and quote_identifier are
// instance methods that need a connection.
class Probe : public tests::TestBase {
public:
    using tests::TestBase::TestBase;
    std::vector<tests::TestResult> run() override { return {}; }
    std::string category_name() const override { return "Probe"; }

    // verify_rows_persisted is protected because probes call it on
    // themselves; a subclass is the ordinary way to reach it from a test.
    using tests::TestBase::verify_rows_persisted;
};

class RowVerificationFixture : public ::testing::Test {
protected:
    void SetUp() override {
        env_ = std::make_unique<core::OdbcEnvironment>();
        conn_ = std::make_unique<core::OdbcConnection>(*env_);
        try {
            conn_->connect(
                "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;");
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Mock ODBC Driver not registered on this host: "
                         << e.what();
        }
        probe_ = std::make_unique<Probe>(*conn_);
        drop(kTable);
    }

    void TearDown() override {
        if (probe_) drop(kTable);
    }

    void drop(const std::string& table) {
        try {
            core::OdbcStatement stmt(*conn_);
            stmt.execute("DROP TABLE " + table);
        } catch (...) {
            // Absent is the state we want; failing to drop is not a finding.
        }
    }

    void exec(const std::string& sql) {
        core::OdbcStatement stmt(*conn_);
        stmt.execute(sql);
    }

    static constexpr const char* kTable = "ODBC_TEST_A19";

    std::unique_ptr<core::OdbcEnvironment> env_;
    std::unique_ptr<core::OdbcConnection> conn_;
    std::unique_ptr<Probe> probe_;
};

}  // namespace

// ── is_bare_identifier — pure, no driver ──────────────────────────────────

TEST(BareIdentifier, AcceptsTheNamesThisToolActuallyGenerates) {
    EXPECT_TRUE(tests::TestBase::is_bare_identifier("ODBC_TEST_TXN"));
    EXPECT_TRUE(tests::TestBase::is_bare_identifier("VAL"));
    EXPECT_TRUE(tests::TestBase::is_bare_identifier("_leading_underscore"));
    EXPECT_TRUE(tests::TestBase::is_bare_identifier("T1"));
}

// The whole point of the predicate: anything it accepts is interpolated into
// SQL unquoted, so a name that would change the statement's meaning must be
// rejected. Erring towards "needs quoting" is the safe way to be wrong.
TEST(BareIdentifier, RejectsAnythingThatWouldChangeTheStatement) {
    EXPECT_FALSE(tests::TestBase::is_bare_identifier(""));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("1_starts_with_digit"));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("has space"));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("has-dash"));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("drop;--"));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("qu\"ote"));
    EXPECT_FALSE(tests::TestBase::is_bare_identifier("SCHEMA.TABLE"));
}

// ── get_data_full — needs a driver ────────────────────────────────────────

// The A19 regression itself. 700 characters is deliberately several times the
// 256-byte buffer get_data_full reads into, so the value only arrives whole if
// the continuation loop runs. The pre-A19 code returned the first 255.
TEST_F(RowVerificationFixture, ReadsAValueLongerThanTheInternalBuffer) {
    const std::string long_value(700, 'x');

    exec(std::string("CREATE TABLE ") + kTable +
         " (ID INTEGER, VAL VARCHAR(1000))");
    exec(std::string("INSERT INTO ") + kTable + " (ID, VAL) VALUES (1, '" +
         long_value + "')");

    core::OdbcStatement sel(*conn_);
    sel.execute(std::string("SELECT VAL FROM ") + kTable);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(sel.get_handle())));

    std::string out;
    bool is_null = true;
    std::string err;
    ASSERT_TRUE(tests::TestBase::get_data_full(sel.get_handle(), 1, out,
                                               is_null, err))
        << err;
    EXPECT_FALSE(is_null);
    EXPECT_EQ(out.size(), long_value.size());
    EXPECT_EQ(out, long_value);
}

// A value that fits in one buffer must not be affected by the continuation
// logic — no doubled bytes, no spurious second call.
TEST_F(RowVerificationFixture, ReadsAShortValueExactlyOnce) {
    exec(std::string("CREATE TABLE ") + kTable +
         " (ID INTEGER, VAL VARCHAR(100))");
    exec(std::string("INSERT INTO ") + kTable +
         " (ID, VAL) VALUES (1, 'short')");

    core::OdbcStatement sel(*conn_);
    sel.execute(std::string("SELECT VAL FROM ") + kTable);
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(sel.get_handle())));

    std::string out;
    bool is_null = true;
    std::string err;
    ASSERT_TRUE(tests::TestBase::get_data_full(sel.get_handle(), 1, out,
                                               is_null, err))
        << err;
    EXPECT_FALSE(is_null);
    EXPECT_EQ(out, "short");
}

// ── verify_rows_persisted end to end ──────────────────────────────────────

// The long value has to survive the helper too, not just get_data_full: this
// is the path that reports CRITICAL data corruption, and before A19 it
// compared a 255-character prefix against the 700 characters inserted.
TEST_F(RowVerificationFixture, LongValuesSurviveVerifyRowsPersisted) {
    const std::string long_value(700, 'y');

    exec(std::string("CREATE TABLE ") + kTable +
         " (ID INTEGER, VAL VARCHAR(1000))");
    exec(std::string("INSERT INTO ") + kTable + " (ID, VAL) VALUES (1, '" +
         long_value + "')");

    tests::RowVerification v =
        probe_->verify_rows_persisted(kTable, "ID", "VAL", 1);

    ASSERT_TRUE(v.ok) << v.diagnostic;
    ASSERT_EQ(v.actual_values.size(), 1u);
    ASSERT_TRUE(v.actual_values[0].has_value());
    EXPECT_EQ(*v.actual_values[0], long_value);
}

// NULL must be distinguishable from the empty string. The mock's
// SilentCorruption=NullAsEmpty mode is the Oracle-style driver that conflates
// them; this connection is not in that mode, so a NULL column must arrive as
// nullopt and render as <NULL> rather than as "".
TEST_F(RowVerificationFixture, NullIsDistinguishableFromEmptyString) {
    exec(std::string("CREATE TABLE ") + kTable +
         " (ID INTEGER, VAL VARCHAR(100))");
    exec(std::string("INSERT INTO ") + kTable + " (ID, VAL) VALUES (1, NULL)");
    exec(std::string("INSERT INTO ") + kTable + " (ID, VAL) VALUES (2, '')");

    tests::RowVerification v =
        probe_->verify_rows_persisted(kTable, "ID", "VAL", 2);

    ASSERT_TRUE(v.ok) << v.diagnostic;
    ASSERT_EQ(v.actual_values.size(), 2u);
    EXPECT_FALSE(v.actual_values[0].has_value()) << "NULL read back as a value";
    ASSERT_TRUE(v.actual_values[1].has_value()) << "'' read back as NULL";
    EXPECT_EQ(*v.actual_values[1], "");

    // The rendering probes print into `actual`. "" for a NULL reads as a
    // driver that stored an empty string, which is a different finding.
    EXPECT_EQ(v.display(0), "<NULL>");
    EXPECT_EQ(v.display(1), "");
}

// quote_identifier leaves the names this codebase generates alone. Quoting
// them would break PostgreSQL, which folds an unquoted CREATE TABLE name
// *down* while the quoted SELECT would ask for the upper-case spelling.
TEST_F(RowVerificationFixture, BareSafeNamesAreNotQuoted) {
    EXPECT_EQ(probe_->quote_identifier("ODBC_TEST_TXN"), "ODBC_TEST_TXN");
    EXPECT_EQ(probe_->quote_identifier("VAL"), "VAL");

    // A name that could not have been created unquoted does get quoted, with
    // whatever the driver reports as its quote character — " for the mock.
    EXPECT_EQ(probe_->quote_identifier("has space"), "\"has space\"");
}
