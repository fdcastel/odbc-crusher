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
            case tests::TestStatus::SKIP_UNSUPPORTED: test["status"] = "SKIP_UNSUPPORTED"; break;
            case tests::TestStatus::SKIP_INCONCLUSIVE: test["status"] = "SKIP_INCONCLUSIVE"; break;
            case tests::TestStatus::ERR:  test["status"] = "ERROR"; break;
        }
        
        // Severity
        test["severity"] = tests::severity_to_string(result.severity);
        
        // Conformance
        test["conformance_level"] = tests::conformance_to_string(result.conformance);
        if (!result.spec_reference.empty()) {
            test["spec_reference"] = result.spec_reference;
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

void JsonReporter::report_driver_info(const discovery::DriverInfo::Properties& props) {
    nlohmann::json driver_info;
    driver_info["driver_name"] = props.driver_name;
    driver_info["driver_version"] = props.driver_ver;
    driver_info["driver_odbc_version"] = props.driver_odbc_ver;
    driver_info["odbc_version"] = props.odbc_ver;
    driver_info["dbms_name"] = props.dbms_name;
    driver_info["dbms_version"] = props.dbms_ver;
    driver_info["database_name"] = props.database_name;
    driver_info["server_name"] = props.server_name;
    driver_info["user_name"] = props.user_name;
    driver_info["sql_conformance"] = props.sql_conformance;
    driver_info["catalog_term"] = props.catalog_term;
    driver_info["schema_term"] = props.schema_term;
    driver_info["table_term"] = props.table_term;
    driver_info["procedure_term"] = props.procedure_term;
    driver_info["identifier_quote_char"] = props.identifier_quote_char;
    root_["driver_info"] = driver_info;
}

void JsonReporter::report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) {
    nlohmann::json type_array = nlohmann::json::array();
    for (const auto& type : types) {
        nlohmann::json t;
        t["type_name"] = type.type_name;
        t["sql_data_type"] = type.sql_data_type;
        t["column_size"] = type.column_size;
        t["nullable"] = type.nullable;
        if (type.auto_unique_value.has_value()) {
            t["auto_unique_value"] = *type.auto_unique_value;
        }
        type_array.push_back(t);
    }
    root_["type_info"] = type_array;
}

void JsonReporter::report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) {
    nlohmann::json func_info;
    func_info["supported_count"] = funcs.supported_count;
    func_info["total_checked"] = funcs.total_checked;
    func_info["supported"] = funcs.supported;
    func_info["unsupported"] = funcs.unsupported;
    root_["function_info"] = func_info;
}

} // namespace odbc_crusher::reporting
