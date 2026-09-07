#pragma once

#include "reporter.hpp"
#include "discovery/driver_info.hpp"
#include "discovery/type_info.hpp"
#include "discovery/function_info.hpp"
#include <iostream>

namespace odbc_crusher::reporting {

// Console reporter with formatted output
class ConsoleReporter : public Reporter {
public:
    explicit ConsoleReporter(std::ostream& out = std::cout, bool verbose = false)
        : out_(out), verbose_(verbose) {}
    
    void report_start(const std::string& connection_string) override;
    void report_driver_info(const discovery::DriverInfo::Properties& props) override;
    void report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) override;
    void report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) override;
    void report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf) override;
    void report_category(const std::string& category_name,
                        const std::vector<tests::TestResult>& results) override;
    void report_summary(size_t total_tests, size_t passed, size_t failed,
                       size_t skipped, size_t errors,
                       std::chrono::microseconds total_duration) override;
    void report_end() override;
    
private:
    std::ostream& out_;
    bool verbose_;
    // G5: only the FAIL/ERR results are ever read back, by the
    // severity-ranked summary in report_summary(). This used to be a full
    // second copy of every TestResult — 195 of them against the mock, each
    // carrying five std::strings and two optionals — to produce a list that
    // is empty on a clean run.
    std::vector<tests::TestResult> failures_;
    
    std::string status_icon(tests::TestStatus status) const;
    std::string format_duration(std::chrono::microseconds duration) const;
};

} // namespace odbc_crusher::reporting
