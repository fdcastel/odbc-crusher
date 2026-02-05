#pragma once

#include "reporter.hpp"
#include <nlohmann/json.hpp>
#include <fstream>

namespace odbc_crusher::reporting {

// JSON reporter for structured output
class JsonReporter : public Reporter {
public:
    explicit JsonReporter(const std::string& output_file = "")
        : output_file_(output_file) {}
    
    void report_start(const std::string& connection_string) override;
    void report_category(const std::string& category_name,
                        const std::vector<tests::TestResult>& results) override;
    void report_summary(size_t total_tests, size_t passed, size_t failed,
                       size_t skipped, size_t errors,
                       std::chrono::microseconds total_duration) override;
    void report_end() override;
    
private:
    std::string output_file_;
    nlohmann::json root_;
    nlohmann::json categories_;
};

} // namespace odbc_crusher::reporting
