#pragma once

#include "discovery/driver_info.hpp"
#include "discovery/function_info.hpp"
#include "discovery/type_info.hpp"
#include "tests/test_base.hpp"
#include <string>
#include <vector>

namespace odbc_crusher::reporting {

// Reporter interface
class Reporter {
public:
    virtual ~Reporter() = default;

    // Report the start of testing
    virtual void report_start(const std::string& connection_string) = 0;

    // Report Phase 1 discovery output. Each concrete reporter chooses how
    // to present (plain text for console, structured JSON object for
    // JsonReporter). A reporter that doesn't care may override to no-op.
    virtual void report_driver_info(const discovery::DriverInfo::Properties& props) = 0;
    virtual void report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) = 0;
    virtual void report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) = 0;
    virtual void report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf) = 0;

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
