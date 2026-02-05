#include "console_reporter.hpp"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace odbc_crusher::reporting {

void ConsoleReporter::report_start(const std::string& connection_string) {
    out_ << "\n";
    out_ << "================================================================================\n";
    out_ << "                   ODBC CRUSHER - Driver Testing Tool\n";
    out_ << "================================================================================\n";
    out_ << "\n";
    out_ << "Connection: " << connection_string << "\n";
    out_ << "\n";
}

void ConsoleReporter::report_category(const std::string& category_name,
                                      const std::vector<tests::TestResult>& results) {
    out_ << "--------------------------------------------------------------------------------\n";
    out_ << "  " << category_name << "\n";
    out_ << "--------------------------------------------------------------------------------\n";
    
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
        
        // Collect for severity-ranked summary
        all_results_.push_back(result);
    }
    
    out_ << "\n";
    out_ << "  Category Summary: " << passed << " passed";
    if (failed > 0) out_ << ", " << failed << " failed";
    if (skipped > 0) out_ << ", " << skipped << " skipped";
    if (errors > 0) out_ << ", " << errors << " errors";
    out_ << "\n\n";
}

void ConsoleReporter::report_summary(size_t total_tests, size_t passed, size_t failed,
                                     size_t skipped, size_t errors,
                                     std::chrono::microseconds total_duration) {
    out_ << "================================================================================\n";
    out_ << "                        FINAL SUMMARY\n";
    out_ << "================================================================================\n";
    out_ << "\n";
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
        
        out_ << "  --- FAILURES BY SEVERITY ---\n\n";
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
        out_ << "  [PASS] ALL TESTS PASSED!\n";
    } else {
        out_ << "  [FAIL] SOME TESTS FAILED\n";
    }
    out_ << "\n";
}

void ConsoleReporter::report_end() {
    out_ << "================================================================================\n";
    out_ << std::flush;
}

std::string ConsoleReporter::status_icon(tests::TestStatus status) const {
    switch (status) {
        case tests::TestStatus::PASS: return "[PASS]";
        case tests::TestStatus::FAIL: return "[FAIL]";
        case tests::TestStatus::SKIP: return "[SKIP]";
        case tests::TestStatus::SKIP_UNSUPPORTED: return "[N/S ]";
        case tests::TestStatus::SKIP_INCONCLUSIVE: return "[INC ]";
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
    out_ << "================================================================================\n";
    out_ << "                      DRIVER INFORMATION REPORT\n";
    out_ << "================================================================================\n\n";
    
    out_ << "=== DRIVER & DATABASE MANAGEMENT SYSTEM ===\n";
    out_ << "  Driver Name:          " << props.driver_name << "\n";
    out_ << "  Driver Version:       " << props.driver_ver << "\n";
    out_ << "  Driver ODBC Version:  " << props.driver_odbc_ver << "\n";
    out_ << "  ODBC Version (DM):    " << props.odbc_ver << "\n";
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
    out_ << "\n";
    
    out_ << "=== SQL CONFORMANCE ===\n";
    out_ << "  SQL Conformance:      " << props.sql_conformance << "\n";
    out_ << "\n";
    
    out_ << "=== TERMINOLOGY ===\n";
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
    out_ << "=== SUPPORTED DATA TYPES (" << types.size() << " types) ===\n\n";
    
    // Print table header
    out_ << "+--------------------------------------------------+------------+--------------+----------+----------+\n";
    out_ << "| Type Name                                        |   SQL Type |     Max Size | Nullable | Auto-Inc |\n";
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
    
    out_ << "+--------------------------------------------------+------------+--------------+----------+----------+\n\n";
}

void ConsoleReporter::report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) {
    out_ << "=== ODBC FUNCTIONS (via SQLGetFunctions) ===\n";
    out_ << "  " << funcs.supported_count << "/" << funcs.total_checked 
         << " ODBC functions supported\n\n";
    
    if (!funcs.supported.empty() && verbose_) {
        out_ << "  Supported functions:\n";
        int count = 0;
        out_ << "    ";
        for (const auto& func : funcs.supported) {
            out_ << func;
            count++;
            if (count < static_cast<int>(funcs.supported.size())) {
                out_ << ", ";
                if (count % 5 == 0) out_ << "\n    ";
            }
        }
        out_ << "\n\n";
    }
    
    if (!funcs.unsupported.empty()) {
        out_ << "  MISSING functions:\n";
        int count = 0;
        out_ << "    ";
        for (const auto& func : funcs.unsupported) {
            out_ << func;
            count++;
            if (count < static_cast<int>(funcs.unsupported.size())) {
                out_ << ", ";
                if (count % 5 == 0) out_ << "\n    ";
            }
        }
        out_ << "\n\n";
    }
    
    out_ << "================================================================================\n\n";
}

} // namespace odbc_crusher::reporting
