#include <gtest/gtest.h>
#include "core/crash_guard.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <cstdlib>

using namespace odbc_crusher::core;

// ── Crash Guard Tests ────────────────────────────────────────

TEST(CrashGuardTest, NormalExecutionNoCrash) {
    int value = 0;
    auto result = execute_with_crash_guard([&]() {
        value = 42;
    });
    
    EXPECT_FALSE(result.crashed);
    EXPECT_EQ(result.crash_code, 0u);
    EXPECT_TRUE(result.description.empty());
    EXPECT_EQ(value, 42);
}

TEST(CrashGuardTest, CppExceptionPropagatesThrough) {
    // C++ exceptions should propagate normally through the crash guard.
    // Only real crashes (access violations, etc.) are caught.
    EXPECT_THROW({
        execute_with_crash_guard([]() {
            throw std::runtime_error("test exception");
        });
    }, std::runtime_error);
}

TEST(CrashGuardTest, CatchesAccessViolation) {
    auto result = execute_with_crash_guard([]() {
        // Deliberately cause an access violation
        volatile int* ptr = nullptr;
        *ptr = 42;  // BOOM
    });
    
    EXPECT_TRUE(result.crashed);
    EXPECT_NE(result.crash_code, 0u);
    EXPECT_FALSE(result.description.empty());
    EXPECT_NE(result.description.find("likely a bug"), std::string::npos);
}

TEST(CrashGuardTest, ContinuesAfterCrash) {
    // First call crashes
    auto result1 = execute_with_crash_guard([]() {
        volatile int* ptr = nullptr;
        *ptr = 42;
    });
    EXPECT_TRUE(result1.crashed);
    
    // Second call should work fine
    int value = 0;
    auto result2 = execute_with_crash_guard([&]() {
        value = 99;
    });
    EXPECT_FALSE(result2.crashed);
    EXPECT_EQ(value, 99);
}

// ── Statement Recycle Tests ──────────────────────────────────

class StatementRecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        env = std::make_unique<OdbcEnvironment>();
        const char* conn_str = std::getenv("FIREBIRD_ODBC_CONNECTION");
        if (!conn_str) {
            // Try mock driver
            conn_str_ = "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;ResultSetSize=100;";
        } else {
            conn_str_ = conn_str;
        }
        conn = std::make_unique<OdbcConnection>(*env);
        try {
            conn->connect(conn_str_);
        } catch (...) {
            conn.reset();
        }
    }
    
    std::unique_ptr<OdbcEnvironment> env;
    std::unique_ptr<OdbcConnection> conn;
    std::string conn_str_;
};

TEST_F(StatementRecycleTest, RecycleOnFreshStatement) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";
    
    OdbcStatement stmt(*conn);
    // recycle() on a fresh statement should be a no-op (no crash)
    EXPECT_NO_THROW(stmt.recycle());
}

TEST_F(StatementRecycleTest, RecycleAfterExecute) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";
    
    OdbcStatement stmt(*conn);
    
    // Execute a query (try different SQL dialects)
    try {
        stmt.execute("SELECT 1 FROM RDB$DATABASE");
    } catch (...) {
        try {
            stmt.execute("SELECT * FROM USERS");
        } catch (...) {
            GTEST_SKIP() << "Could not execute any test query";
        }
    }
    
    // Recycle should not throw
    EXPECT_NO_THROW(stmt.recycle());
    
    // Should be able to execute again after recycle
    try {
        stmt.execute("SELECT 1 FROM RDB$DATABASE");
    } catch (...) {
        stmt.execute("SELECT * FROM USERS");
    }
}

TEST_F(StatementRecycleTest, MultipleExecutesWithoutExplicitClose) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";
    
    OdbcStatement stmt(*conn);
    
    // Execute multiple queries on the same statement handle without
    // explicitly closing the cursor. execute() calls recycle() internally.
    // This is the pattern that used to crash with Firebird.
    for (int i = 0; i < 5; i++) {
        try {
            stmt.execute("SELECT 1 FROM RDB$DATABASE");
        } catch (...) {
            stmt.execute("SELECT * FROM USERS");
        }
        // No close_cursor() call between iterations!
    }
    // If we get here without crashing, the test passes
}

TEST_F(StatementRecycleTest, ExecuteAfterFailedExecute) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";
    
    OdbcStatement stmt(*conn);
    
    // Try an invalid query first (should fail)
    try {
        stmt.execute("THIS IS NOT VALID SQL !!!");
    } catch (const OdbcError&) {
        // Expected to fail
    }
    
    // Now execute a valid query - should work because execute() calls recycle()
    EXPECT_NO_THROW({
        try {
            stmt.execute("SELECT 1 FROM RDB$DATABASE");
        } catch (...) {
            stmt.execute("SELECT * FROM USERS");
        }
    });
}

TEST_F(StatementRecycleTest, PrepareAfterFailedExecute) {
    if (!conn) GTEST_SKIP() << "No ODBC driver available";
    
    OdbcStatement stmt(*conn);
    
    // Try an invalid query first
    try {
        stmt.execute("INVALID SQL STATEMENT");
    } catch (const OdbcError&) {
        // Expected
    }
    
    // prepare() also calls recycle(), so this should work
    EXPECT_NO_THROW({
        try {
            stmt.prepare("SELECT 1 FROM RDB$DATABASE");
        } catch (...) {
            stmt.prepare("SELECT * FROM USERS");
        }
    });
}
