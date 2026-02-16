#include "console_reporter.hpp"
#include "odbc_crusher/version.hpp"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace odbc_crusher::reporting {

void ConsoleReporter::report_start(const std::string& connection_string) {
    out_ << "ODBC Crusher v" << ODBC_CRUSHER_VERSION << " - Driver analysis report\n\n";
}

void ConsoleReporter::report_category(const std::string& category_name,
                                      const std::vector<tests::TestResult>& results) {
    size_t passed = 0, failed = 0, skipped = 0, errors = 0;
    
    for (const auto& result : results) {
        switch (result.status) {
            case tests::TestStatus::PASS: passed++; break;
            case tests::TestStatus::FAIL: failed++; break;
            case tests::TestStatus::SKIP:
            case tests::TestStatus::SKIP_UNSUPPORTED:
            case tests::TestStatus::SKIP_INCONCLUSIVE: skipped++; break;
            case tests::TestStatus::ERR: errors++; break;
        }
        all_results_.push_back(result);
    }
    
    // Build summary string
    std::ostringstream summary;
    if (passed > 0) summary << passed << " passed";
    if (failed > 0) {
        if (summary.tellp() > 0) summary << ", ";
        summary << failed << " failed";
    }
    if (skipped > 0) {
        if (summary.tellp() > 0) summary << ", ";
        summary << skipped << " skipped";
    }
    if (errors > 0) {
        if (summary.tellp() > 0) summary << ", ";
        summary << errors << " errors";
    }
    
    // Calculate padding for right-aligned summary
    std::string summary_str = summary.str();
    int name_len = category_name.length();
    int summary_len = summary_str.length();
    int total_width = 80;
    int padding = total_width - name_len - summary_len - 2; // -2 for spaces
    if (padding < 2) padding = 2;
    
    out_ << category_name << ":" << std::string(padding, ' ') << summary_str << "\n";
    
    for (const auto& result : results) {
        std::string icon = status_icon(result.status);
        out_ << "  " << icon << " " << result.test_name;
        
        // Show conformance level tag
        out_ << " [" << tests::conformance_to_string(result.conformance) << "]";
        
        if (verbose_ || result.status == tests::TestStatus::FAIL || 
            result.status == tests::TestStatus::ERR) {
            out_ << "\n";
            out_ << "      Function:    " << result.function << "\n";
            if (!result.spec_reference.empty()) {
                out_ << "      Spec:        " << result.spec_reference << "\n";
            }
            out_ << "      Conformance: " << tests::conformance_to_string(result.conformance) << "\n";
            out_ << "      Expected:    " << result.expected << "\n";
            out_ << "      Actual:      " << result.actual << "\n";
            out_ << "      Duration:    " << format_duration(result.duration) << "\n";
            
            if (result.diagnostic && !result.diagnostic->empty()) {
                out_ << "      Diagnostic:  " << *result.diagnostic << "\n";
            }
            if (result.suggestion && !result.suggestion->empty()) {
                out_ << "      Suggestion:  " << *result.suggestion << "\n";
            }
        } else {
            out_ << " (" << format_duration(result.duration) << ")\n";
        }
    }
    
    out_ << "\n";
}

void ConsoleReporter::report_summary(size_t total_tests, size_t passed, size_t failed,
                                     size_t skipped, size_t errors,
                                     std::chrono::microseconds total_duration) {
    out_ << "SUMMARY:\n";
    out_ << "  Total Tests:  " << total_tests << "\n";
    out_ << "  Passed:       " << passed;
    if (total_tests > 0) {
        out_ << " (" << std::fixed << std::setprecision(1) 
             << (passed * 100.0 / total_tests) << "%)";
    }
    out_ << "\n";
    
    if (failed > 0) {
        out_ << "  Failed:       " << failed << "\n";
    }
    if (skipped > 0) {
        out_ << "  Skipped:      " << skipped << "\n";
    }
    if (errors > 0) {
        out_ << "  Errors:       " << errors << "\n";
    }
    
    out_ << "  Total Time:   " << format_duration(total_duration) << "\n";
    out_ << "\n";
    
    // Severity-ranked failure summary
    std::vector<const tests::TestResult*> failures;
    for (const auto& r : all_results_) {
        if (r.status == tests::TestStatus::FAIL || r.status == tests::TestStatus::ERR) {
            failures.push_back(&r);
        }
    }
    
    if (!failures.empty()) {
        // Sort by severity: CRITICAL > ERROR > WARNING > INFO
        std::sort(failures.begin(), failures.end(),
            [](const tests::TestResult* a, const tests::TestResult* b) {
                return static_cast<int>(a->severity) < static_cast<int>(b->severity);
            });
        
        out_ << "FAILURES BY SEVERITY:\n\n";
        for (const auto* r : failures) {
            out_ << "  [" << tests::severity_to_string(r->severity) << "] "
                 << r->test_name << " (" << r->function << ")\n";
            out_ << "    " << r->actual << "\n";
            if (r->suggestion && !r->suggestion->empty()) {
                out_ << "    Fix: " << *r->suggestion << "\n";
            }
            out_ << "\n";
        }
    }
    
    if (failed == 0 && errors == 0) {
        out_ << "  [PASS] ALL TESTS PASSED\n";
    } else {
        out_ << "  [FAIL] SOME TESTS FAILED\n";
    }
    out_ << "\n";
}

void ConsoleReporter::report_end() {
    out_ << std::flush;
}

std::string ConsoleReporter::status_icon(tests::TestStatus status) const {
    switch (status) {
        case tests::TestStatus::PASS: return "[PASS]";
        case tests::TestStatus::FAIL: return "[FAIL]";
        case tests::TestStatus::SKIP: return "[SKIP]";
        case tests::TestStatus::SKIP_UNSUPPORTED: return "[NOT ]";
        case tests::TestStatus::SKIP_INCONCLUSIVE: return "[ ?? ]";
        case tests::TestStatus::ERR:  return "[ERR!]";
        default: return "[????]";
    }
}

std::string ConsoleReporter::format_duration(std::chrono::microseconds duration) const {
    auto us = duration.count();
    
    if (us < 1000) {
        return std::to_string(us) + " us";
    } else if (us < 1000000) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000.0) << " ms";
        return oss.str();
    } else {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << (us / 1000000.0) << " s";
        return oss.str();
    }
}

void ConsoleReporter::report_driver_info(const discovery::DriverInfo::Properties& props) {
    out_ << "DRIVER:\n";
    out_ << "  Driver Name:          " << props.driver_name << "\n";
    out_ << "  Driver Version:       " << props.driver_ver << "\n";
    out_ << "  Driver ODBC Version:  " << props.driver_odbc_ver << "\n";
    out_ << "  ODBC Version (DM):    " << props.odbc_ver << "\n";
    out_ << "\n";
    
    out_ << "DATABASE:\n";
    out_ << "  DBMS Name:            " << props.dbms_name << "\n";
    out_ << "  DBMS Version:         " << props.dbms_ver << "\n";
    if (!props.database_name.empty()) {
        out_ << "  Database:             " << props.database_name << "\n";
    }
    if (!props.server_name.empty()) {
        out_ << "  Server:               " << props.server_name << "\n";
    }
    if (!props.user_name.empty()) {
        out_ << "  User:                 " << props.user_name << "\n";
    }
    out_ << "  SQL Conformance:      " << props.sql_conformance << "\n";
    out_ << "\n";
    out_ << "  Catalog Term:         " << props.catalog_term << "\n";
    out_ << "  Schema Term:          " << props.schema_term << "\n";
    out_ << "  Table Term:           " << props.table_term << "\n";
    out_ << "  Procedure Term:       " << props.procedure_term << "\n";
    if (!props.identifier_quote_char.empty()) {
        out_ << "  Quote Character:      " << props.identifier_quote_char << "\n";
    }
    out_ << "\n";
}

void ConsoleReporter::report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) {
    out_ << "DATA TYPES:\n";
    out_ << "  Type Name                                            SQL Type       Max Size   Nullable   Auto-Inc  \n";
    out_ << "+--------------------------------------------------+------------+--------------+----------+----------+\n";
    
    for (const auto& type : types) {
        out_ << "| " << std::left << std::setw(48) << type.type_name.substr(0, 48)
             << " | " << std::right << std::setw(10) << type.sql_data_type
             << " | " << std::setw(12);
        
        if (type.column_size == 0) {
            out_ << "N/A";
        } else {
            out_ << type.column_size;
        }
        
        out_ << " | " << std::setw(8) << (type.nullable ? "Yes" : "No")
             << " | " << std::setw(8);
        
        if (type.auto_unique_value.has_value()) {
            out_ << (*type.auto_unique_value ? "Yes" : "No");
        } else {
            out_ << "";
        }
        
        out_ << " |\n";
    }
    
    out_ << "+--------------------------------------------------+------------+--------------+----------+----------+\n";
    out_ << "(" << types.size() << " types)\n\n";
}

void ConsoleReporter::report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) {
    out_ << "ODBC FUNCTIONS:\n";
    out_ << "  " << funcs.supported_count << "/" << funcs.total_checked 
         << " ODBC functions supported (as reported by SQLGetFunctions)\n\n";
    
    if (!funcs.unsupported.empty()) {
        out_ << "  MISSING functions:\n";
        for (const auto& func : funcs.unsupported) {
            out_ << "    " << func << "\n";
        }
        out_ << "\n";
    }
}

void ConsoleReporter::report_scalar_functions(const discovery::DriverInfo::ScalarFunctionSupport& sf) {
    out_ << "SCALAR FUNCTIONS:\n";
    
    auto print_list = [&](const char* label, const std::vector<std::string>& funcs) {
        if (funcs.empty()) return;
        out_ << "  " << label << " (" << funcs.size() << "): ";
        for (size_t i = 0; i < funcs.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << funcs[i];
        }
        out_ << "\n";
    };
    
    print_list("String", sf.string_functions);
    print_list("Numeric", sf.numeric_functions);
    print_list("Date/Time", sf.timedate_functions);
    print_list("System", sf.system_functions);
    
    // OJ capabilities
    if (sf.oj_capabilities) {
        out_ << "  Outer Join: ";
        if (sf.oj_capabilities & SQL_OJ_LEFT) out_ << "LEFT ";
        if (sf.oj_capabilities & SQL_OJ_RIGHT) out_ << "RIGHT ";
        if (sf.oj_capabilities & SQL_OJ_FULL) out_ << "FULL ";
        out_ << "\n";
    }
    
    // Convert matrix summary
    if (!sf.convert_matrix.empty()) {
        int supported = 0;
        for (const auto& [name, mask] : sf.convert_matrix) {
            if (mask != 0) ++supported;
        }
        out_ << "  Type Conversions: " << supported << "/" << sf.convert_matrix.size() << " types have conversion support\n";
    }
    
    out_ << "\n";
}

} // namespace odbc_crusher::reporting
