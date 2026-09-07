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
    // G4: version of the report contract, emitted as the top-level
    // "schema_version" key. Bump it whenever an existing key changes meaning,
    // is renamed or is removed; purely additive changes keep the number.
    //
    //   1 — initial versioned schema. Same shape as the unversioned reports
    //       that preceded it, except "timestamp" is now an ISO-8601 UTC
    //       string rather than a raw epoch integer.
    static constexpr int kSchemaVersion = 1;

    explicit JsonReporter(const std::string& output_file = "")
        : output_file_(output_file) {}
    
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
    std::string output_file_;
    nlohmann::json root_;
    nlohmann::json categories_;
};

} // namespace odbc_crusher::reporting
