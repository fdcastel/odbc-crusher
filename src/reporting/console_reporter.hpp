#pragma once

#include "reporter.hpp"
#include <iostream>

namespace odbc_crusher::reporting {

// Console reporter with formatted output
class ConsoleReporter : public Reporter {
public:
    explicit ConsoleReporter(std::ostream& out = std::cout, bool verbose = false)
        : out_(out), verbose_(verbose) {}
    
    void report_start(const std::string& connection_string) override;
    void report_category(const std::string& category_name,
                        const std::vector<tests::TestResult>& results) override;
    void report_summary(size_t total_tests, size_t passed, size_t failed,
                       size_t skipped, size_t errors,
                       std::chrono::microseconds total_duration) override;
    void report_end() override;
    
private:
    std::ostream& out_;
    bool verbose_;
    
    std::string status_icon(tests::TestStatus status) const;
    std::string format_duration(std::chrono::microseconds duration) const;
};

} // namespace odbc_crusher::reporting
