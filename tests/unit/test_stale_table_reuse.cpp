// Stale-table reuse — IMPROVEMENT_PLAN.md A15.
//
// Both TransactionTests and ArrayParamTests probe for their test table before
// creating it, so a run without CREATE TABLE privileges can still work against
// a table left behind by an earlier run. They reused the table *with its rows*.
// That matters because main.cpp's crash guard keeps the process alive past an
// abort, so the cleanup DROP is skipped and the next run inherits the data —
// and the probes then assert exact counts against it:
//
//   test_manual_commit    expects COUNT(*) == 1
//   test_manual_rollback  expects COUNT(*) == 0
//   test_row_wise_array_binding  expects WHERE ID IN (9991, 9992) to give 2
//
// so yesterday's leftovers fail a correct driver today. These tests stage
// exactly that: seed the table, then ask the helper for it and require it to
// come back empty.
#include <gtest/gtest.h>

#include "core/odbc_connection.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_statement.hpp"
#include "tests/array_param_tests.hpp"
#include "tests/test_base.hpp"
#include "tests/transaction_tests.hpp"

#include <memory>
#include <string>

using namespace odbc_crusher;

namespace {

// create_test_table() is protected; a subclass is how a test reaches it.
class TxnProbe : public tests::TransactionTests {
public:
    using tests::TransactionTests::TransactionTests;
    using tests::TransactionTests::create_test_table;
    using tests::TransactionTests::drop_test_table;
};

class ArrayProbe : public tests::ArrayParamTests {
public:
    using tests::ArrayParamTests::ArrayParamTests;
    using tests::ArrayParamTests::create_test_table;
    using tests::ArrayParamTests::drop_test_table;
};

class StaleTableFixture : public ::testing::Test {
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
    }

    void exec(const std::string& sql) {
        core::OdbcStatement stmt(*conn_);
        stmt.execute(sql);
    }

    void try_exec(const std::string& sql) {
        try {
            exec(sql);
        } catch (...) {
            // Best effort — this is staging, not the assertion.
        }
    }

    // -1 when the table is not there at all: the SELECT throws, and "gone"
    // is a state these tests assert on.
    long count(const std::string& table) {
        try {
            return count_or_throw(table);
        } catch (...) {
            return -1;
        }
    }

    long count_or_throw(const std::string& table) {
        core::OdbcStatement stmt(*conn_);
        stmt.execute("SELECT COUNT(*) FROM " + table);
        if (!SQL_SUCCEEDED(SQLFetch(stmt.get_handle()))) return -1;
        SQLBIGINT n = -1;
        SQLLEN ind = 0;
        if (!SQL_SUCCEEDED(SQLGetData(stmt.get_handle(), 1, SQL_C_SBIGINT, &n,
                                      sizeof(n), &ind))) {
            return -1;
        }
        return static_cast<long>(n);
    }

    std::unique_ptr<core::OdbcEnvironment> env_;
    std::unique_ptr<core::OdbcConnection> conn_;
};

}  // namespace

TEST_F(StaleTableFixture, ReusedTransactionTableComesBackEmpty) {
    // Stage what a crashed run leaves behind: the table exists and has rows.
    try_exec("DROP TABLE ODBC_TEST_TXN");
    exec("CREATE TABLE ODBC_TEST_TXN (ID INTEGER, VAL VARCHAR(50))");
    exec("INSERT INTO ODBC_TEST_TXN (ID, VAL) VALUES (1, 'left over')");
    exec("INSERT INTO ODBC_TEST_TXN (ID, VAL) VALUES (2, 'left over')");
    ASSERT_EQ(count("ODBC_TEST_TXN"), 2) << "staging did not take";

    TxnProbe probe(*conn_);
    ASSERT_TRUE(probe.create_test_table())
        << "the table exists, so the reuse path must succeed";

    // Before A15 this was 2, and test_manual_commit's COUNT(*) == 1 then
    // failed a driver that had done nothing wrong.
    EXPECT_EQ(count("ODBC_TEST_TXN"), 0)
        << "create_test_table() handed back a table with the previous run's "
           "rows still in it";

    probe.drop_test_table();
}

TEST_F(StaleTableFixture, ReusedArrayTableComesBackEmpty) {
    try_exec("DROP TABLE ODBC_TEST_ARRAY");
    exec("CREATE TABLE ODBC_TEST_ARRAY (ID INTEGER, NAME VARCHAR(50))");
    // The exact IDs the row-wise binding probes insert and never delete.
    exec("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (9991, 'x')");
    exec("INSERT INTO ODBC_TEST_ARRAY (ID, NAME) VALUES (9992, 'x')");
    ASSERT_EQ(count("ODBC_TEST_ARRAY"), 2) << "staging did not take";

    ArrayProbe probe(*conn_);
    ASSERT_TRUE(probe.create_test_table())
        << "the table exists, so the reuse path must succeed";

    EXPECT_EQ(count("ODBC_TEST_ARRAY"), 0)
        << "create_test_table() handed back 9991/9992 from the previous run, "
           "which is what made WHERE ID IN (9991, 9992) return four rows";

    probe.drop_test_table();
}

// The other half of the contract: when the table does *not* exist, the helper
// still creates one, and it is empty. A DELETE bolted onto the reuse path
// must not have broken the ordinary path.
TEST_F(StaleTableFixture, FreshTransactionTableIsCreatedAndEmpty) {
    try_exec("DROP TABLE ODBC_TEST_TXN");

    TxnProbe probe(*conn_);
    ASSERT_TRUE(probe.create_test_table());
    EXPECT_EQ(count("ODBC_TEST_TXN"), 0);

    probe.drop_test_table();
}

// ── The guard itself — C4 ─────────────────────────────────────────────────
//
// After C4 the three create_test_table()/create_roundtrip_table() helpers are
// thin adapters over RoundTripTableGuard, so the reuse-and-clear behaviour
// they used to carry each has to live in the guard. These pin it there.

TEST_F(StaleTableFixture, GuardReusesAnExistingTableAndEmptiesIt) {
    try_exec("DROP TABLE ODBC_TEST_A15G");
    exec("CREATE TABLE ODBC_TEST_A15G (ID INTEGER, VAL VARCHAR(50))");
    exec("INSERT INTO ODBC_TEST_A15G (ID, VAL) VALUES (1, 'left over')");
    ASSERT_EQ(count("ODBC_TEST_A15G"), 1) << "staging did not take";

    {
        tests::RoundTripTableGuard g(*conn_, "ODBC_TEST_A15G", "VARCHAR(50)");
        ASSERT_TRUE(g.ok()) << g.last_error();
        EXPECT_TRUE(g.was_reused())
            << "the table was already there; the guard should not have "
               "needed DDL privileges";
        EXPECT_EQ(count("ODBC_TEST_A15G"), 0)
            << "a reused table must not arrive with the previous run's rows";
        // The requested DDL is not what made this table, and probes print
        // val_ddl() into their report, so it must not claim otherwise.
        EXPECT_NE(g.val_ddl(), "VARCHAR(50)");
    }
    // The guard drops on scope exit, reused or not.
    EXPECT_EQ(count("ODBC_TEST_A15G"), -1)
        << "the table should be gone after the guard went out of scope";
}

TEST_F(StaleTableFixture, GuardCreatesWhenTheTableIsAbsent) {
    try_exec("DROP TABLE ODBC_TEST_A15G");

    tests::RoundTripTableGuard g(*conn_, "ODBC_TEST_A15G", "VARCHAR(50)");
    ASSERT_TRUE(g.ok()) << g.last_error();
    EXPECT_FALSE(g.was_reused());
    EXPECT_EQ(g.val_ddl(), "VARCHAR(50)");
    EXPECT_EQ(count("ODBC_TEST_A15G"), 0);
}

// ArrayParamTests' table calls its value column NAME, which is the reason the
// guard takes the column name at all. A wrong name here would make every
// array probe's INSERT fail.
TEST_F(StaleTableFixture, GuardHonoursTheValueColumnName) {
    try_exec("DROP TABLE ODBC_TEST_A15G");

    tests::RoundTripTableGuard g(
        *conn_, "ODBC_TEST_A15G", "VARCHAR(50)",
        tests::RoundTripTableGuard::default_id_ddl_variants(), "NAME");
    ASSERT_TRUE(g.ok()) << g.last_error();
    EXPECT_EQ(g.val_column(), "NAME");

    // Provable rather than assumed: the column exists under that name.
    exec("INSERT INTO ODBC_TEST_A15G (ID, NAME) VALUES (1, 'x')");
    EXPECT_EQ(count("ODBC_TEST_A15G"), 1);
}
