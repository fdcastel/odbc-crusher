#include <iostream>
#include <memory>
#include <CLI/CLI.hpp>
#include "odbc_crusher/version.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include "core/crash_guard.hpp"
#include "tests/connection_tests.hpp"
#include "tests/statement_tests.hpp"
#include "tests/metadata_tests.hpp"
#include "tests/datatype_tests.hpp"
#include "tests/transaction_tests.hpp"
#include "tests/advanced_tests.hpp"
#include "tests/buffer_validation_tests.hpp"
#include "tests/error_queue_tests.hpp"
#include "tests/state_machine_tests.hpp"
#include "tests/descriptor_tests.hpp"
#include "tests/cancellation_tests.hpp"
#include "tests/sqlstate_tests.hpp"
#include "tests/boundary_tests.hpp"
#include "tests/datatype_edge_tests.hpp"
#include "tests/unicode_tests.hpp"
#include "tests/catalog_depth_tests.hpp"
#include "tests/diagnostic_depth_tests.hpp"
#include "tests/cursor_behavior_tests.hpp"
#include "tests/param_binding_tests.hpp"
#include "tests/array_param_tests.hpp"
#include "tests/escape_sequence_tests.hpp"
#include "tests/numeric_struct_tests.hpp"
#include "tests/cursor_stress_tests.hpp"
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
            case tests::TestStatus::SKIP_UNSUPPORTED:
            case tests::TestStatus::SKIP_INCONCLUSIVE: total_skipped++; break;
            case tests::TestStatus::ERR:  total_errors++; break;
        }
    }
}

void run_test_category(tests::TestBase& test_suite, reporting::Reporter& reporter,
                       size_t& total_tests, size_t& total_passed,
                       size_t& total_failed, size_t& total_skipped,
                       size_t& total_errors) {
    std::vector<tests::TestResult> results;

    auto guard = core::execute_with_crash_guard([&]() {
        results = test_suite.run();
    });

    if (guard.crashed) {
        // The test category caused a driver crash (e.g. access violation).
        // Report it as an error result so the tool keeps running.
        tests::TestResult crash_result;
        crash_result.test_name = test_suite.category_name() + " (DRIVER CRASH)";
        crash_result.function = "N/A";
        crash_result.status = tests::TestStatus::ERR;
        crash_result.severity = tests::Severity::CRITICAL;
        crash_result.conformance = tests::ConformanceLevel::CORE;
        crash_result.expected = "Test category completes without crashing";
        crash_result.actual = guard.description;
        crash_result.diagnostic = "The ODBC driver crashed during this test category. "
                                  "Some tests may have been lost. This is a driver bug.";
        crash_result.duration = std::chrono::microseconds(0);
        results.push_back(crash_result);
    }

    reporter.report_category(test_suite.category_name(), results);
    tally_results(results, total_tests, total_passed, total_failed,
                  total_skipped, total_errors);
    std::cout << std::flush;
}

} // anonymous namespace

int main(int argc, char** argv) {
    CLI::App app{
        "ODBC Crusher - ODBC Driver Testing Tool\n"
        "\n"
        "  Connects to an ODBC driver and runs a comprehensive suite of\n"
        "  conformance tests covering connections, statements, metadata,\n"
        "  data types, transactions, error handling, and more.\n"
        "\n"
        "Examples:\n"
        "  odbc-crusher \"Driver={MySQL ODBC 9.2 Unicode Driver};Server=localhost;...\"\n"
        "  odbc-crusher \"DSN=MyFirebird\" -v\n"
        "  odbc-crusher \"Driver={PostgreSQL};...\" -o json -f report.json\n",
        "odbc-crusher"
    };
    
    app.set_version_flag("--version,-V", ODBC_CRUSHER_VERSION);
    
    std::string connection_string;
    app.add_option("connection", connection_string,
                   "ODBC connection string (Driver={...};... or DSN=...)")
        ->required();
    
    bool verbose = false;
    app.add_flag("-v,--verbose", verbose,
                 "Show detailed diagnostics and suggestions for each test");
    
    std::string output_format = "console";
    app.add_option("-o,--output", output_format,
                   "Output format: 'console' (default) or 'json'")
        ->check(CLI::IsMember({"console", "json"}));
    
    std::string json_file;
    app.add_option("-f,--file", json_file,
                   "Write JSON output to FILE instead of stdout");
    
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
        // Wrapped in crash guard because some drivers (e.g. DuckDB on Linux)
        // can SIGSEGV during SQLGetTypeInfo or SQLGetInfo.
        discovery::DriverInfo driver_info(conn);
        discovery::TypeInfo type_info(conn);
        discovery::FunctionInfo func_info(conn);
        
        bool discovery_ok = true;
        auto discovery_guard = core::execute_with_crash_guard([&]() {
            driver_info.collect();
            type_info.collect();
            func_info.collect();
        });
        
        if (discovery_guard.crashed) {
            discovery_ok = false;
            std::cerr << "\nWARNING: Driver crashed during discovery phase: " 
                      << discovery_guard.description << "\n"
                      << "Continuing with limited information...\n\n";
            std::cerr << std::flush;
        }
        
        if (discovery_ok) {
            reporter->report_driver_info(driver_info.get_properties());
            reporter->report_type_info(type_info.get_types());
            reporter->report_function_info(func_info.get_support());
            reporter->report_scalar_functions(driver_info.get_scalar_functions());
            if (output_format == "console") {
                std::cout << std::flush;
            }
        }
        
        // G1: progress chatter goes to stderr, never stdout. stdout is the
        // data channel — with `-o json` and no `-f`, the report is the only
        // thing on it, so `odbc-crusher ... -o json | jq` works as the README
        // documents. This line used to make that pipe unparseable.
        std::cerr << "Phase 2: Running ODBC tests...\n\n" << std::flush;
        
        // Track overall statistics
        size_t total_tests = 0;
        size_t total_passed = 0;
        size_t total_failed = 0;
        size_t total_skipped = 0;
        size_t total_errors = 0;
        auto overall_start = std::chrono::high_resolution_clock::now();
        
        // Registered test categories. Order is preserved in the report.
        // Adding a new category = one line here + the usual hpp/cpp/cmake/gtest wiring.
        std::vector<std::unique_ptr<tests::TestBase>> categories;
        categories.emplace_back(std::make_unique<tests::ConnectionTests>(conn));
        categories.emplace_back(std::make_unique<tests::StatementTests>(conn));
        categories.emplace_back(std::make_unique<tests::MetadataTests>(conn));
        categories.emplace_back(std::make_unique<tests::DataTypeTests>(conn));
        categories.emplace_back(std::make_unique<tests::TransactionTests>(conn));
        categories.emplace_back(std::make_unique<tests::AdvancedTests>(conn));
        categories.emplace_back(std::make_unique<tests::BufferValidationTests>(conn));
        categories.emplace_back(std::make_unique<tests::ErrorQueueTests>(conn));
        categories.emplace_back(std::make_unique<tests::StateMachineTests>(conn));
        categories.emplace_back(std::make_unique<tests::DescriptorTests>(conn));
        categories.emplace_back(std::make_unique<tests::CancellationTests>(conn));
        categories.emplace_back(std::make_unique<tests::SqlstateTests>(conn));
        categories.emplace_back(std::make_unique<tests::BoundaryTests>(conn));
        categories.emplace_back(std::make_unique<tests::DataTypeEdgeCaseTests>(conn));
        categories.emplace_back(std::make_unique<tests::UnicodeTests>(conn));
        categories.emplace_back(std::make_unique<tests::CatalogDepthTests>(conn));
        categories.emplace_back(std::make_unique<tests::DiagnosticDepthTests>(conn));
        categories.emplace_back(std::make_unique<tests::CursorBehaviorTests>(conn));
        categories.emplace_back(std::make_unique<tests::ParameterBindingTests>(conn));
        categories.emplace_back(std::make_unique<tests::ArrayParamTests>(conn));
        categories.emplace_back(std::make_unique<tests::EscapeSequenceTests>(conn));
        categories.emplace_back(std::make_unique<tests::NumericStructTests>(conn));
        categories.emplace_back(std::make_unique<tests::CursorStressTests>(conn));

        for (auto& category : categories) {
            run_test_category(*category, *reporter, total_tests, total_passed,
                              total_failed, total_skipped, total_errors);
        }
        
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
