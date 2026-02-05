#include "json_reporter.hpp"
#include <iostream>
#include <iomanip>

namespace odbc_crusher::reporting {

void JsonReporter::report_start(const std::string& connection_string) {
    root_ = nlohmann::json::object();
    root_["connection_string"] = connection_string;
    root_["timestamp"] = std::time(nullptr);
    categories_ = nlohmann::json::array();
}

void JsonReporter::report_category(const std::string& category_name,
                                   const std::vector<tests::TestResult>& results) {
    nlohmann::json category;
    category["name"] = category_name;
    
    nlohmann::json tests_array = nlohmann::json::array();
    
    for (const auto& result : results) {
        nlohmann::json test;
        test["test_name"] = result.test_name;
        test["function"] = result.function;
        
        // Status
        switch (result.status) {
            case tests::TestStatus::PASS: test["status"] = "PASS"; break;
            case tests::TestStatus::FAIL: test["status"] = "FAIL"; break;
            case tests::TestStatus::SKIP: test["status"] = "SKIP"; break;
            case tests::TestStatus::ERR:  test["status"] = "ERROR"; break;
        }
        
        // Severity
        switch (result.severity) {
            case tests::Severity::CRITICAL: test["severity"] = "CRITICAL"; break;
            case tests::Severity::ERR:      test["severity"] = "ERROR"; break;
            case tests::Severity::WARNING:  test["severity"] = "WARNING"; break;
            case tests::Severity::INFO:     test["severity"] = "INFO"; break;
        }
        
        test["expected"] = result.expected;
        test["actual"] = result.actual;
        test["duration_us"] = result.duration.count();
        
        if (result.diagnostic) {
            test["diagnostic"] = *result.diagnostic;
        }
        if (result.suggestion) {
            test["suggestion"] = *result.suggestion;
        }
        
        tests_array.push_back(test);
    }
    
    category["tests"] = tests_array;
    categories_.push_back(category);
}

void JsonReporter::report_summary(size_t total_tests, size_t passed, size_t failed,
                                  size_t skipped, size_t errors,
                                  std::chrono::microseconds total_duration) {
    nlohmann::json summary;
    summary["total_tests"] = total_tests;
    summary["passed"] = passed;
    summary["failed"] = failed;
    summary["skipped"] = skipped;
    summary["errors"] = errors;
    summary["total_duration_us"] = total_duration.count();
    
    if (total_tests > 0) {
        summary["pass_rate"] = (passed * 100.0) / total_tests;
    } else {
        summary["pass_rate"] = 0.0;
    }
    
    root_["summary"] = summary;
    root_["categories"] = categories_;
}

void JsonReporter::report_end() {
    if (output_file_.empty()) {
        // Print to stdout
        std::cout << std::setw(2) << root_ << std::endl;
    } else {
        // Write to file
        std::ofstream file(output_file_);
        if (file.is_open()) {
            file << std::setw(2) << root_ << std::endl;
            std::cout << "JSON report written to: " << output_file_ << std::endl;
        } else {
            std::cerr << "Error: Could not write to " << output_file_ << std::endl;
        }
    }
}

} // namespace odbc_crusher::reporting
