#include "console_reporter.hpp"
#include <iomanip>
#include <sstream>

namespace odbc_crusher::reporting {

void ConsoleReporter::report_start(const std::string& connection_string) {
    out_ << "\n";
    out_ << "╔════════════════════════════════════════════════════════════════╗\n";
    out_ << "║           ODBC CRUSHER - Driver Testing Report                ║\n";
    out_ << "╚════════════════════════════════════════════════════════════════╝\n";
    out_ << "\n";
    out_ << "Connection: " << connection_string << "\n";
    out_ << "\n";
}

void ConsoleReporter::report_category(const std::string& category_name,
                                      const std::vector<tests::TestResult>& results) {
    out_ << "────────────────────────────────────────────────────────────────\n";
    out_ << "  " << category_name << "\n";
    out_ << "────────────────────────────────────────────────────────────────\n";
    
    size_t passed = 0, failed = 0, skipped = 0, errors = 0;
    
    for (const auto& result : results) {
        switch (result.status) {
            case tests::TestStatus::PASS: passed++; break;
            case tests::TestStatus::FAIL: failed++; break;
            case tests::TestStatus::SKIP: skipped++; break;
            case tests::TestStatus::ERR: errors++; break;
        }
        
        std::string icon = status_icon(result.status);
        out_ << "  " << icon << " " << result.test_name;
        
        if (verbose_ || result.status == tests::TestStatus::FAIL || 
            result.status == tests::TestStatus::ERR) {
            out_ << "\n";
            out_ << "      Function: " << result.function << "\n";
            out_ << "      Expected: " << result.expected << "\n";
            out_ << "      Actual:   " << result.actual << "\n";
            out_ << "      Duration: " << format_duration(result.duration) << "\n";
            
            if (result.diagnostic && !result.diagnostic->empty()) {
                out_ << "      Diagnostic: " << *result.diagnostic << "\n";
            }
            if (result.suggestion && !result.suggestion->empty()) {
                out_ << "      Suggestion: " << *result.suggestion << "\n";
            }
        } else {
            out_ << " (" << format_duration(result.duration) << ")\n";
        }
    }
    
    out_ << "\n";
    out_ << "  Category Summary: " << passed << " passed";
    if (failed > 0) out_ << ", " << failed << " failed";
    if (skipped > 0) out_ << ", " << skipped << " skipped";
    if (errors > 0) out_ << ", " << errors << " errors";
    out_ << "\n\n";
}

void ConsoleReporter::report_summary(size_t total_tests, size_t passed, size_t failed,
                                     size_t skipped, size_t errors,
                                     std::chrono::microseconds total_duration) {
    out_ << "════════════════════════════════════════════════════════════════\n";
    out_ << "                        FINAL SUMMARY\n";
    out_ << "════════════════════════════════════════════════════════════════\n";
    out_ << "\n";
    out_ << "  Total Tests:  " << total_tests << "\n";
    out_ << "  Passed:       " << passed;
    if (total_tests > 0) {
        out_ << " (" << std::fixed << std::setprecision(1) 
             << (passed * 100.0 / total_tests) << "%)";
    }
    out_ << "\n";
    
    if (failed > 0) {
        out_ << "  Failed:       " << failed << "\n";
    }
    if (skipped > 0) {
        out_ << "  Skipped:      " << skipped << "\n";
    }
    if (errors > 0) {
        out_ << "  Errors:       " << errors << "\n";
    }
    
    out_ << "  Total Time:   " << format_duration(total_duration) << "\n";
    out_ << "\n";
    
    if (failed == 0 && errors == 0) {
        out_ << "  ✓ ALL TESTS PASSED!\n";
    } else {
        out_ << "  ✗ SOME TESTS FAILED\n";
    }
    out_ << "\n";
}

void ConsoleReporter::report_end() {
    out_ << "════════════════════════════════════════════════════════════════\n";
    out_ << std::flush;
}

std::string ConsoleReporter::status_icon(tests::TestStatus status) const {
    switch (status) {
        case tests::TestStatus::PASS: return "✓";
        case tests::TestStatus::FAIL: return "✗";
        case tests::TestStatus::SKIP: return "-";
        case tests::TestStatus::ERR:  return "!";
        default: return "?";
    }
}

std::string ConsoleReporter::format_duration(std::chrono::microseconds duration) const {
    auto us = duration.count();
    
    if (us < 1000) {
        return std::to_string(us) + " μs";
    } else if (us < 1000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000.0) << " ms";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000000.0) << " s";
        return oss.str();
    }
}

} // namespace odbc_crusher::reporting
