#pragma once

#include "core/odbc_connection.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace odbc_crusher::tests {

// Test status
enum class TestStatus {
    PASS,
    FAIL,
    SKIP,
    ERR  // Changed from ERROR to avoid Windows macro conflict
};

// Severity level
enum class Severity {
    CRITICAL,
    ERR,      // Changed from ERROR to avoid Windows macro conflict
    WARNING,
    INFO
};

// Test result
struct TestResult {
    std::string test_name;
    std::string function;           // ODBC function tested
    TestStatus status;
    Severity severity;
    std::string expected;
    std::string actual;
    std::optional<std::string> diagnostic;
    std::optional<std::string> suggestion;
    std::chrono::microseconds duration;
};

// Base class for all ODBC tests
class TestBase {
public:
    explicit TestBase(core::OdbcConnection& conn)
        : conn_(conn) {}
    
    virtual ~TestBase() = default;
    
    // Run all tests in this category
    virtual std::vector<TestResult> run() = 0;
    
    // Get test category name
    virtual std::string category_name() const = 0;
    
protected:
    core::OdbcConnection& conn_;
    
    // Helper to create test result
    TestResult make_result(
        const std::string& test_name,
        const std::string& function,
        TestStatus status,
        const std::string& expected,
        const std::string& actual,
        Severity severity = Severity::INFO
    );
    
    // Helper to time a test
    template<typename Func>
    auto time_test(Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }
};

} // namespace odbc_crusher::tests
