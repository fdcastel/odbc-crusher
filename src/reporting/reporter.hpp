#pragma once

#include "tests/test_base.hpp"
#include <vector>
#include <string>

namespace odbc_crusher::reporting {

// Reporter interface
class Reporter {
public:
    virtual ~Reporter() = default;
    
    // Report the start of testing
    virtual void report_start(const std::string& connection_string) = 0;
    
    // Report a test category
    virtual void report_category(const std::string& category_name,
                                 const std::vector<tests::TestResult>& results) = 0;
    
    // Report the final summary
    virtual void report_summary(size_t total_tests, size_t passed, size_t failed,
                               size_t skipped, size_t errors,
                               std::chrono::microseconds total_duration) = 0;
    
    // Report the end of testing
    virtual void report_end() = 0;
};

} // namespace odbc_crusher::reporting
