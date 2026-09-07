#include "json_reporter.hpp"
#include <ctime>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <system_error>

namespace odbc_crusher::reporting {

namespace {

// G4: ISO-8601 UTC, e.g. "2026-09-07T13:14:15Z". The report used to carry a
// raw std::time_t integer, which every consumer had to know was epoch seconds.
std::string iso8601_utc(std::time_t t) {
    std::tm tm_buf{};
#ifdef _WIN32
    if (gmtime_s(&tm_buf, &t) != 0) return {};
#else
    if (gmtime_r(&t, &tm_buf) == nullptr) return {};
#endif
    char buf[32];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf) == 0) return {};
    return buf;
}

}  // namespace

void JsonReporter::report_start(const std::string& connection_string) {
    root_ = nlohmann::json::object();
    // G4: version the output contract. Integer, matching the convention
    // .github/drivers.json already uses. Bump it whenever an existing key
    // changes meaning, is renamed or is removed; purely additive changes keep
    // the number. Consumers should reject a version they do not know.
    root_["schema_version"] = kSchemaVersion;
    root_["connection_string"] = connection_string;
    root_["timestamp"] = iso8601_utc(std::time(nullptr));
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
        
        test["status"] = tests::status_to_string(result.status);
        
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

    // F2: persist what we have, so a killed run still leaves a usable report.
    maybe_write_snapshot();
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
    // categories are attached by write_snapshot() / report_end(), which own
    // that key — see F2.
}

void JsonReporter::maybe_write_snapshot() {
    if (output_file_.empty()) return;

    const auto now = std::chrono::steady_clock::now();
    if (wrote_snapshot_ && (now - last_snapshot_) < kSnapshotInterval) return;

    write_snapshot(false);
    wrote_snapshot_ = true;
    last_snapshot_ = now;
}

void JsonReporter::write_snapshot(bool complete) {
    if (output_file_.empty()) return;

    nlohmann::json doc = root_;
    doc["categories"] = categories_;
    doc["complete"] = complete;

    // Write to a sibling temp file and rename over the target, so a signal
    // landing mid-write can never leave a truncated document behind. rename()
    // within a directory is atomic on POSIX, and MoveFileEx-with-replace on
    // Windows via std::filesystem.
    const std::filesystem::path target(output_file_);
    std::filesystem::path tmp = target;
    tmp += ".partial";

    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            // Report it once, on the first failure, then stay quiet: this runs
            // after every category and a broken path would otherwise emit 23
            // identical lines.
            if (!write_failed_) {
                write_failed_ = true;
                std::cerr << "Error: Could not write to " << tmp.string() << std::endl;
            }
            return;
        }
        // Intermediate snapshots are written compact: they exist to be
        // machine-read after a kill, and re-serialising a pretty-printed
        // ~100 KB document after each of 23 categories measurably lengthened
        // the run. Only the final report is indented.
        if (complete) {
            file << std::setw(2) << doc << std::endl;
        } else {
            file << doc << std::endl;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        if (!write_failed_) {
            write_failed_ = true;
            std::cerr << "Error: Could not write to " << output_file_ << ": "
                      << ec.message() << std::endl;
        }
        std::filesystem::remove(tmp, ec);
    }
}

void JsonReporter::report_end() {
    if (output_file_.empty()) {
        // Print to stdout: exactly one document, so there is nothing partial
        // to mark, but it must still carry the same keys as the file form.
        root_["categories"] = categories_;
        root_["complete"] = true;
        std::cout << std::setw(2) << root_ << std::endl;
    } else {
        write_snapshot(true);
        if (!write_failed_) {
            // G1: this is progress chatter, not report data. On stderr so that
            // `-o json -f report.json` leaves stdout completely empty.
            std::cerr << "JSON report written to: " << output_file_ << std::endl;
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

void JsonReporter::report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf) {
    nlohmann::json scalar;
    scalar["string_functions"] = sf.string_functions;
    scalar["numeric_functions"] = sf.numeric_functions;
    scalar["timedate_functions"] = sf.timedate_functions;
    scalar["system_functions"] = sf.system_functions;
    scalar["string_bitmask"] = sf.string_bitmask;
    scalar["numeric_bitmask"] = sf.numeric_bitmask;
    scalar["timedate_bitmask"] = sf.timedate_bitmask;
    scalar["system_bitmask"] = sf.system_bitmask;
    scalar["convert_functions_bitmask"] = sf.convert_functions_bitmask;
    scalar["oj_capabilities"] = sf.oj_capabilities;
    scalar["datetime_literals"] = sf.datetime_literals;

    nlohmann::json convert_matrix;
    for (const auto& [name, mask] : sf.convert_matrix) {
        convert_matrix[name] = mask;
    }
    scalar["convert_matrix"] = convert_matrix;

    root_["scalar_functions"] = scalar;
}

} // namespace odbc_crusher::reporting
