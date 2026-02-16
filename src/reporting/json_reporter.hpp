#pragma once

#include "reporter.hpp"
#include "discovery/driver_info.hpp"
#include "discovery/type_info.hpp"
#include "discovery/function_info.hpp"
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
    
    // Driver discovery reporting (mirrors ConsoleReporter)
    void report_driver_info(const discovery::DriverInfo::Properties& props);
    void report_type_info(const std::vector<discovery::TypeInfo::DataType>& types);
    void report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs);
    void report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf);
    
private:
    std::string output_file_;
    nlohmann::json root_;
    nlohmann::json categories_;
};

} // namespace odbc_crusher::reporting
