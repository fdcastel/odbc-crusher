// Cross-connection catalog detection — IMPROVEMENT_PLAN.md H15.
//
// Three probes create a table on the primary connection and then work on a
// sibling: test_reconnected_handle_is_usable, test_uncommitted_row_isolation,
// test_disconnect_with_open_transaction. All three assume both handles address
// the same catalog. That holds for a server and does not hold for a data
// source that is per-connection — DuckDB with no `Database=` opens `:memory:`
// and then deliberately declines to cache the instance, so every SQLConnect
// gets its own database object and the sibling never sees the primary's table.
//
// The triage of DuckDB 1.5.2.0 recorded exactly that as three ERRORs and two
// empty-catalog results, blaming a driver that was behaving as documented. The
// probes now call sibling_shares_catalog() first and SKIP_UNSUPPORTED when the
// answer is no.
//
// What these tests pin is the helper's contract, which is what the guard rests
// on: it says "yes" for a table this connection can reach, "no" for one it
// cannot, and — the part that is easy to get wrong — it leaves the connection
// usable either way, because the probe still has to set its result and return
// after a "no".
//
// They do NOT simulate DuckDB. The mock driver's tables live in
// MockCatalog::instance(), a process-wide singleton, so every mock connection
// shares a catalog by construction; giving it a per-connection mode would mean
// dismantling that singleton, which is a much larger change than the guard it
// would be testing. A missing table is the same observation the guard makes —
// "this connection cannot see that table" — reached by the only route
// available here.
#include <gtest/gtest.h>

#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_statement.hpp"
#include "tests/test_base.hpp"

#include <memory>
#include <string>

using namespace odbc_crusher;

namespace {

// sibling_shares_catalog() is protected; a subclass is how a test reaches it.
class CatalogProbe : public tests::TestBase {
public:
    using tests::TestBase::TestBase;
    using tests::TestBase::sibling_shares_catalog;
    using tests::TestBase::per_connection_catalog_skip;

    std::vector<tests::TestResult> run() override { return {}; }
    std::string category_name() const override { return "CatalogProbe"; }
};

class SiblingCatalogFixture : public ::testing::Test {
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
        probe_ = std::make_unique<CatalogProbe>(*conn_);
    }

    void TearDown() override {
        if (conn_ && conn_->get_handle()) try_exec("DROP TABLE " + kTable);
    }

    void try_exec(const std::string& sql) {
        try {
            core::OdbcStatement stmt(*conn_);
            stmt.execute(sql);
        } catch (...) {
            // Staging, not an assertion.
        }
    }

    const std::string kTable = "ODBC_CRUSHER_SIBLING_CHECK";

    std::unique_ptr<core::OdbcEnvironment> env_;
    std::unique_ptr<core::OdbcConnection> conn_;
    std::unique_ptr<CatalogProbe> probe_;
};

TEST_F(SiblingCatalogFixture, SeesATableThisConnectionCanReach) {
    try_exec("DROP TABLE " + kTable);
    core::OdbcStatement stmt(*conn_);
    stmt.execute("CREATE TABLE " + kTable + " (ID INTEGER)");

    EXPECT_TRUE(probe_->sibling_shares_catalog(*conn_, kTable))
        << "A table created on this very connection must be visible to it; if "
           "this fails the guard would SKIP every cross-connection probe on "
           "every driver.";
}

TEST_F(SiblingCatalogFixture, DoesNotSeeATableThatIsNotThere) {
    try_exec("DROP TABLE " + kTable);

    EXPECT_FALSE(probe_->sibling_shares_catalog(*conn_, kTable))
        << "This is the DuckDB shape: the SELECT fails because the table is "
           "not reachable from this handle. Answering 'yes' here is what let "
           "three probes report a driver fault for a per-connection catalog.";
}

// The guard runs mid-probe: after a "no" the probe still has to set its result
// and return, and after a "yes" it goes on to do real work. Neither is possible
// if the failed SELECT left the connection wedged, which is why the helper
// rolls back on the way out.
TEST_F(SiblingCatalogFixture, LeavesTheConnectionUsableAfterANegativeAnswer) {
    try_exec("DROP TABLE " + kTable);
    ASSERT_FALSE(probe_->sibling_shares_catalog(*conn_, kTable));

    core::OdbcStatement stmt(*conn_);
    EXPECT_NO_THROW(stmt.execute("CREATE TABLE " + kTable + " (ID INTEGER)"))
        << "The connection must survive the probe's failed SELECT.";
    EXPECT_TRUE(probe_->sibling_shares_catalog(*conn_, kTable))
        << "And the same helper must now answer 'yes' for the table it just "
           "reported missing.";
}

TEST_F(SiblingCatalogFixture, SkipTextNamesTheTableAndTheRemedy) {
    const std::string msg = CatalogProbe::per_connection_catalog_skip(kTable);

    // The reader of a SKIP is someone deciding whether their driver is at
    // fault. The text has to say what was not found and what to change.
    EXPECT_NE(msg.find(kTable), std::string::npos);
    EXPECT_NE(msg.find("Database="), std::string::npos);
}

} // namespace
