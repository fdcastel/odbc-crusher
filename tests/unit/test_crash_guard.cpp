#include <gtest/gtest.h>
#include "core/crash_guard.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_statement.hpp"
#include "core/odbc_error.hpp"
#include <csignal>
#ifndef _WIN32
#include <signal.h>
#endif
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
        // Deliberately cause an access violation.
        //
        // E5: -Wnull-dereference is right about this line and the test is the
        // one place in the tree where it is wrong to act on it - dereferencing
        // null is the entire point. Suppressed here, with the reason, rather
        // than by dropping the flag: it is worth having everywhere else, and a
        // warning switched off globally to accommodate one deliberate site
        // stops protecting the other several hundred.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#endif
        volatile int* ptr = nullptr;
        *ptr = 42;  // BOOM
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
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


// ── D73: the signals a platform raises when it traps rather than aborts ───
//
// macOS killed crusher with `Trace/BPT trap: 5` - SIGTRAP - on a fault Linux
// contained and reported, and three e2e scenarios failed with no report at
// all because the process died before one was written. On arm64 a failed
// bounds or fortify check is `brk`, which is SIGTRAP; the same check on
// x86-64 glibc calls abort() and raises SIGABRT.
//
// The existing crash tests all fault by dereferencing null, which every
// platform delivers as SIGSEGV - so none of them could have caught a missing
// signal. These raise the signals directly.
#ifndef _WIN32

TEST(CrashGuardTest, CatchesSigtrap) {
    auto result = execute_with_crash_guard([]() { std::raise(SIGTRAP); });

    EXPECT_TRUE(result.crashed)
        << "SIGTRAP escaped the guard, which is how macOS died before it "
           "could write a report";
    EXPECT_EQ(result.crash_code, static_cast<unsigned int>(SIGTRAP));
    EXPECT_NE(result.description.find("SIGTRAP"), std::string::npos)
        << "description was: " << result.description;
}

TEST(CrashGuardTest, CatchesSigill) {
    auto result = execute_with_crash_guard([]() { std::raise(SIGILL); });

    EXPECT_TRUE(result.crashed);
    EXPECT_EQ(result.crash_code, static_cast<unsigned int>(SIGILL));
}

// The guard must leave the process as it found it, or a later crash outside a
// guarded region would be swallowed by a handler nobody installed for it.
TEST(CrashGuardTest, RestoresTheHandlersItInstalled) {
    struct sigaction before {};
    ASSERT_EQ(sigaction(SIGTRAP, nullptr, &before), 0);

    auto result = execute_with_crash_guard([]() { std::raise(SIGTRAP); });
    ASSERT_TRUE(result.crashed);

    struct sigaction after {};
    ASSERT_EQ(sigaction(SIGTRAP, nullptr, &after), 0);
    EXPECT_EQ(before.sa_handler, after.sa_handler)
        << "the guard kept its SIGTRAP handler after returning";
}

// D70: with the guard off the body runs bare, which is what lets ASan report
// a fault instead of our handler swallowing it. Asserted on the no-crash path,
// because the whole point of the switch is that a crash is *not* contained.
TEST(CrashGuardTest, TheOptOutRunsTheBodyWithoutGuarding) {
    setenv("ODBC_CRUSHER_NO_CRASH_GUARD", "1", 1);
    int value = 0;
    auto result = execute_with_crash_guard([&]() { value = 7; });
    unsetenv("ODBC_CRUSHER_NO_CRASH_GUARD");

    EXPECT_EQ(value, 7);
    EXPECT_FALSE(result.crashed);
}

#endif  // !_WIN32
