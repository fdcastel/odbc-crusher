#include <iostream>
#include <memory>
#include <CLI/CLI.hpp>
#include "odbc_crusher/version.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include "tests/connection_tests.hpp"
#include "tests/statement_tests.hpp"
#include "tests/metadata_tests.hpp"
#include "tests/datatype_tests.hpp"
#include "tests/transaction_tests.hpp"
#include "tests/advanced_tests.hpp"
#include "tests/buffer_validation_tests.hpp"
#include "tests/error_queue_tests.hpp"
#include "tests/state_machine_tests.hpp"
#include "discovery/driver_info.hpp"
#include "discovery/type_info.hpp"
#include "discovery/function_info.hpp"
#include "reporting/console_reporter.hpp"
#include "reporting/json_reporter.hpp"

using namespace odbc_crusher;

namespace {

void tally_results(const std::vector<tests::TestResult>& results,
                   size_t& total_tests, size_t& total_passed,
                   size_t& total_failed, size_t& total_skipped,
                   size_t& total_errors) {
    for (const auto& r : results) {
        total_tests++;
        switch (r.status) {
            case tests::TestStatus::PASS: total_passed++; break;
            case tests::TestStatus::FAIL: total_failed++; break;
            case tests::TestStatus::SKIP: total_skipped++; break;
            case tests::TestStatus::ERR:  total_errors++; break;
        }
    }
}

template<typename T>
void run_test_category(T& test_suite, reporting::Reporter& reporter,
                       size_t& total_tests, size_t& total_passed,
                       size_t& total_failed, size_t& total_skipped,
                       size_t& total_errors) {
    auto results = test_suite.run();
    reporter.report_category(test_suite.category_name(), results);
    tally_results(results, total_tests, total_passed, total_failed,
                  total_skipped, total_errors);
}

} // anonymous namespace

int main(int argc, char** argv) {
    CLI::App app{"ODBC Crusher - ODBC Driver Testing Tool", "odbc-crusher"};
    
    app.set_version_flag("--version", ODBC_CRUSHER_VERSION);
    
    std::string connection_string;
    app.add_option("connection", connection_string, "ODBC connection string")
        ->required();
    
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose, "Verbose output");
    
    std::string output_format = "console";
    app.add_option("-o,--output", output_format, "Output format (console, json)")
        ->check(CLI::IsMember({"console", "json"}));
    
    std::string json_file;
    app.add_option("-f,--file", json_file, "JSON output file (for json format)");
    
    CLI11_PARSE(app, argc, argv);
    
    try {
        // Create reporter
        std::unique_ptr<reporting::Reporter> reporter;
        
        if (output_format == "json") {
            reporter = std::make_unique<reporting::JsonReporter>(json_file);
        } else {
            reporter = std::make_unique<reporting::ConsoleReporter>(std::cout, verbose);
        }
        
        reporter->report_start(connection_string);
        
        // Initialize ODBC
        core::OdbcEnvironment env;
        core::OdbcConnection conn(env);
        
        // Connect to database
        conn.connect(connection_string);
        
        // Phase 1: Collect driver information (for all output formats)
        discovery::DriverInfo driver_info(conn);
        driver_info.collect();
        
        discovery::TypeInfo type_info(conn);
        type_info.collect();
        
        discovery::FunctionInfo func_info(conn);
        func_info.collect();
        
        if (output_format == "console") {
            auto* console_rep = dynamic_cast<reporting::ConsoleReporter*>(reporter.get());
            if (console_rep) {
                console_rep->report_driver_info(driver_info.get_properties());
                console_rep->report_type_info(type_info.get_types());
                console_rep->report_function_info(func_info.get_support());
            }
        } else if (output_format == "json") {
            auto* json_rep = dynamic_cast<reporting::JsonReporter*>(reporter.get());
            if (json_rep) {
                json_rep->report_driver_info(driver_info.get_properties());
                json_rep->report_type_info(type_info.get_types());
                json_rep->report_function_info(func_info.get_support());
            }
        }
        
        std::cout << "Phase 2: Running ODBC tests...\n\n";
        
        // Track overall statistics
        size_t total_tests = 0;
        size_t total_passed = 0;
        size_t total_failed = 0;
        size_t total_skipped = 0;
        size_t total_errors = 0;
        auto overall_start = std::chrono::high_resolution_clock::now();
        
        // Run all test categories
        tests::ConnectionTests conn_tests(conn);
        run_test_category(conn_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::StatementTests stmt_tests(conn);
        run_test_category(stmt_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::MetadataTests meta_tests(conn);
        run_test_category(meta_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::DataTypeTests type_tests(conn);
        run_test_category(type_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::TransactionTests txn_tests(conn);
        run_test_category(txn_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::AdvancedTests adv_tests(conn);
        run_test_category(adv_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::BufferValidationTests buffer_tests(conn);
        run_test_category(buffer_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::ErrorQueueTests error_tests(conn);
        run_test_category(error_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        tests::StateMachineTests state_tests(conn);
        run_test_category(state_tests, *reporter, total_tests, total_passed, total_failed, total_skipped, total_errors);
        
        auto overall_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            overall_end - overall_start);
        
        // Report summary
        reporter->report_summary(total_tests, total_passed, total_failed,
                                total_skipped, total_errors, total_duration);
        
        reporter->report_end();
        
        // Return non-zero if any tests failed
        return (total_failed > 0 || total_errors > 0) ? 1 : 0;
        
    } catch (const core::OdbcError& e) {
        std::cerr << "\nODBC Error: " << e.what() << "\n";
        std::cerr << e.format_diagnostics() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 3;
    }
}
