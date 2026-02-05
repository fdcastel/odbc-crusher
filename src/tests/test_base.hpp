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
    SKIP_UNSUPPORTED,   // Driver doesn't support this optional feature
    SKIP_INCONCLUSIVE,  // Test couldn't determine result
    SKIP,               // Legacy - treated as SKIP_INCONCLUSIVE
    ERR                 // Changed from ERROR to avoid Windows macro conflict
};

// Severity level
enum class Severity {
    CRITICAL,
    ERR,      // Changed from ERROR to avoid Windows macro conflict
    WARNING,
    INFO
};

// ODBC conformance level
enum class ConformanceLevel {
    CORE,
    LEVEL_1,
    LEVEL_2
};

// Test result
struct TestResult {
    std::string test_name;
    std::string function;              // ODBC function tested
    TestStatus status;
    Severity severity;
    ConformanceLevel conformance = ConformanceLevel::CORE;
    std::string spec_reference;        // e.g. "ODBC 3.x, SQLGetInfo"
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
        Severity severity = Severity::INFO,
        ConformanceLevel conformance = ConformanceLevel::CORE,
        const std::string& spec_reference = ""
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

// Helper to convert conformance level to string
inline const char* conformance_to_string(ConformanceLevel level) {
    switch (level) {
        case ConformanceLevel::CORE: return "Core";
        case ConformanceLevel::LEVEL_1: return "Level 1";
        case ConformanceLevel::LEVEL_2: return "Level 2";
        default: return "Unknown";
    }
}

// Helper to convert test status to string
inline const char* status_to_string(TestStatus status) {
    switch (status) {
        case TestStatus::PASS: return "PASS";
        case TestStatus::FAIL: return "FAIL";
        case TestStatus::SKIP_UNSUPPORTED: return "SKIP_UNSUPPORTED";
        case TestStatus::SKIP_INCONCLUSIVE: return "SKIP_INCONCLUSIVE";
        case TestStatus::SKIP: return "SKIP";
        case TestStatus::ERR: return "ERROR";
        default: return "UNKNOWN";
    }
}

// Helper to convert severity to string
inline const char* severity_to_string(Severity sev) {
    switch (sev) {
        case Severity::CRITICAL: return "CRITICAL";
        case Severity::ERR: return "ERROR";
        case Severity::WARNING: return "WARNING";
        case Severity::INFO: return "INFO";
        default: return "UNKNOWN";
    }
}

} // namespace odbc_crusher::tests
