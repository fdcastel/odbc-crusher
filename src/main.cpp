#include <algorithm>
#include <cctype>
#include <functional>
#include <type_traits>
#include <utility>
#include <vector>
#include <iostream>
#include <memory>
#include <CLI/CLI.hpp>
#include "odbc_crusher/version.hpp"
#include "core/odbc_environment.hpp"
#include "core/odbc_connection.hpp"
#include "core/odbc_error.hpp"
#include "core/crash_guard.hpp"
#include "tests/category_teardown.hpp"
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

// G2: one list, used by --list-categories, by --category and by the run.
//
// Factories only, deliberately. The first cut of this paired each factory
// with a hand-written display name, and 16 of the 23 disagreed with what the
// class actually reports - so --list-categories printed names that --category
// would then refuse to match. `category_name()` is where a category's name is
// defined; anything else is a copy waiting to drift.
// C13: the connection string travels with the connection, so a probe that
// needs a second one it may break can open it.
using CategoryFactory = std::function<std::unique_ptr<tests::TestBase>(
    core::OdbcConnection&, const std::string&)>;

// Only the categories that need a sibling connection take the string, and
// they say so by declaring the two-argument constructor. Adding it to all 23
// would be 46 files of boilerplate asserting a need that two of them have.
template <typename T>
CategoryFactory make_category() {
    return [](core::OdbcConnection& c, const std::string& cs)
               -> std::unique_ptr<tests::TestBase> {
        if constexpr (std::is_constructible_v<T, core::OdbcConnection&,
                                              const std::string&>) {
            return std::make_unique<T>(c, cs);
        } else {
            (void)cs;
            return std::make_unique<T>(c);
        }
    };
}

// Order is preserved in the report. Adding a category = one line here plus
// the usual hpp/cpp/cmake/gtest wiring.
const std::vector<CategoryFactory>& category_registry() {
    static const std::vector<CategoryFactory> kRegistry = {
        make_category<tests::ConnectionTests>(),
        make_category<tests::StatementTests>(),
        make_category<tests::MetadataTests>(),
        make_category<tests::DataTypeTests>(),
        make_category<tests::TransactionTests>(),
        make_category<tests::AdvancedTests>(),
        make_category<tests::BufferValidationTests>(),
        make_category<tests::ErrorQueueTests>(),
        make_category<tests::StateMachineTests>(),
        make_category<tests::DescriptorTests>(),
        make_category<tests::CancellationTests>(),
        make_category<tests::SqlstateTests>(),
        make_category<tests::BoundaryTests>(),
        make_category<tests::DataTypeEdgeCaseTests>(),
        make_category<tests::UnicodeTests>(),
        make_category<tests::CatalogDepthTests>(),
        make_category<tests::DiagnosticDepthTests>(),
        make_category<tests::CursorBehaviorTests>(),
        make_category<tests::ParameterBindingTests>(),
        make_category<tests::ArrayParamTests>(),
        make_category<tests::EscapeSequenceTests>(),
        make_category<tests::NumericStructTests>(),
        make_category<tests::CursorStressTests>(),
    };
    return kRegistry;
}

// The names, straight from the classes. `conn` need not be connected: every
// category's constructor only stores the reference, and category_name()
// returns a literal.
std::vector<std::string> category_names(core::OdbcConnection& conn) {
    std::vector<std::string> names;
    names.reserve(category_registry().size());
    for (const auto& factory : category_registry()) {
        names.push_back(factory(conn, std::string{})->category_name());
    }
    return names;
}

std::string to_lower_copy(std::string v) {
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v;
}

// G3: map the --fail-on word onto the enum. `none` never fails, so it is a
// sentinel rather than a severity.
bool severity_at_least(tests::Severity worst, const std::string& threshold) {
    if (threshold == "none") return false;
    tests::Severity limit = tests::Severity::INFO;
    if (threshold == "critical") limit = tests::Severity::CRITICAL;
    else if (threshold == "error") limit = tests::Severity::ERR;
    else if (threshold == "warning") limit = tests::Severity::WARNING;
    // Ordered CRITICAL < ERR < WARNING < INFO, so "at least as severe" is <=.
    return worst <= limit;
}

// G3: the highest severity carried by any FAIL or ERR seen so far. Severity
// is ordered CRITICAL < ERR < WARNING < INFO in the enum, so "at least as
// severe as" is `<=` and the worst is the smallest.
tests::Severity g_worst_failure_severity = tests::Severity::INFO;

void tally_results(const std::vector<tests::TestResult>& results,
                   size_t& total_tests, size_t& total_passed,
                   size_t& total_failed, size_t& total_skipped,
                   size_t& total_errors, size_t& total_informational) {
    for (const auto& r : results) {
        total_tests++;
        if (r.status == tests::TestStatus::FAIL ||
            r.status == tests::TestStatus::ERR) {
            if (r.severity < g_worst_failure_severity) {
                g_worst_failure_severity = r.severity;
            }
        }
        switch (r.status) {
            case tests::TestStatus::PASS: total_passed++; break;
            case tests::TestStatus::FAIL: total_failed++; break;
            case tests::TestStatus::SKIP_UNSUPPORTED:
            case tests::TestStatus::SKIP_INCONCLUSIVE: total_skipped++; break;
            case tests::TestStatus::ERR:  total_errors++; break;
            // B2: reported, and deliberately not scored.
            case tests::TestStatus::INFORMATIONAL: total_informational++; break;
        }
    }
}

void run_test_category(tests::TestBase& test_suite, reporting::Reporter& reporter,
                       size_t& total_tests, size_t& total_passed,
                       size_t& total_failed, size_t& total_skipped,
                       size_t& total_errors, size_t& total_informational) {
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
                  total_skipped, total_errors, total_informational);
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
    
    // Not `->required()`: --list-categories has nothing to connect to, and
    // making people pass a dummy connection string to read a list is the kind
    // of small rudeness that makes a tool annoying to adopt. Checked by hand
    // after parsing instead.
    std::string connection_string;
    app.add_option("connection", connection_string,
                   "ODBC connection string (Driver={...};... or DSN=...)");
    
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

    // G2: run a subset. The e2e harness worked around the absence of this
    // with a comment ("just ConnectionTests category isn't filterable from
    // CLI, so we live with a full run"), and bisecting a driver that hangs
    // meant bisecting the source.
    //
    // Categories only, and not individual probes: a probe's name is produced
    // by running it, so there is nothing to list before a run and nothing to
    // save by discarding results after one.
    std::vector<std::string> only_categories;
    app.add_option("-c,--category", only_categories,
                   "Run only these categories (repeatable, "
                   "case-insensitive; see --list-categories)");

    bool list_categories = false;
    app.add_flag("--list-categories", list_categories,
                 "List the test categories and exit");

    // G3: which severities make the exit code non-zero. main used to exit 1
    // for any FAIL or ERR whatever its severity, so against a real driver it
    // was always 1 - which is why the stress-test composite sets
    // continue-on-error and nobody reads it.
    std::string fail_on = "info";
    app.add_option("--fail-on", fail_on,
                   "Exit non-zero only for failures at least this severe: "
                   "critical, error, warning, info (default), or none")
        ->check(CLI::IsMember({"critical", "error", "warning", "info", "none"},
                              CLI::ignore_case));

    CLI11_PARSE(app, argc, argv);

    if (list_categories) {
        // An environment and an unconnected handle are enough to ask each
        // category its name, and neither touches a driver.
        core::OdbcEnvironment list_env;
        core::OdbcConnection list_conn(list_env);
        for (const auto& name : category_names(list_conn)) {
            std::cout << name << "\n";
        }
        return 0;
    }

    if (connection_string.empty()) {
        std::cerr << "Error: a connection string is required.\n"
                     "Run with --help for usage, or --list-categories to see "
                     "what this build can run.\n";
        return 3;
    }
    
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
        size_t total_informational = 0;   // B2
        auto overall_start = std::chrono::high_resolution_clock::now();
        
        // G2: the registry above is the one list; --list-categories and
        // --category read the same names the run uses.
        std::vector<std::unique_ptr<tests::TestBase>> categories;
        for (const auto& factory : category_registry()) {
            auto category = factory(conn, connection_string);
            if (!only_categories.empty()) {
                const std::string lowered = to_lower_copy(category->category_name());
                bool wanted = false;
                for (const auto& want : only_categories) {
                    if (lowered == to_lower_copy(want)) { wanted = true; break; }
                }
                if (!wanted) continue;
            }
            categories.emplace_back(std::move(category));
        }

        // A --category that matches nothing is a mistake worth failing on: it
        // otherwise produces a clean, empty, entirely meaningless report.
        if (categories.empty()) {
            std::cerr << "Error: no test category matched. Known categories:\n";
            for (const auto& name : category_names(conn)) {
                std::cerr << "  " << name << "\n";
            }
            return 3;
        }

        // The report has to say what was asked for, or a consumer comparing
        // pass rates will compare two different subsets and see a regression
        // that is really a filter.
        reporter->report_selected_categories(category_names(conn), [&] {
            std::vector<std::string> selected;
            for (const auto& c : categories) selected.push_back(c->category_name());
            return selected;
        }());

        for (auto& category : categories) {
            run_test_category(*category, *reporter, total_tests, total_passed,
                              total_failed, total_skipped, total_errors,
                              total_informational);
        }

        // D76: destroy the categories here, under the crash guard, rather than
        // letting the vector go out of scope at the end of this block.
        //
        // Three of them hold a RoundTripTableGuard past run(), and
        // ~RoundTripTableGuard issues SQLSetConnectAttr and a DROP TABLE. At
        // end of scope that ran unguarded *and* after report_end(), so a
        // driver that faults while dropping a table killed the process with
        // nothing in the report to say why - and no report left to write it
        // into even if it had been caught.
        if (auto teardown_crashes = tests::teardown_categories(categories);
            !teardown_crashes.empty()) {
            reporter->report_category("Teardown", teardown_crashes);
            tally_results(teardown_crashes, total_tests, total_passed,
                          total_failed, total_skipped, total_errors,
                          total_informational);
        }

        auto overall_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::microseconds>(
            overall_end - overall_start);
        
        // Report summary
        reporter->report_summary(total_tests, total_passed, total_failed,
                                total_skipped, total_errors,
                                total_informational, total_duration);
        
        reporter->report_end();
        
        // G3: non-zero only when something failed at or above the threshold.
        // The default is `info`, the least severe level, so every FAIL and
        // ERR still counts and the exit code is exactly what it always was.
        if (total_failed == 0 && total_errors == 0) return 0;
        return severity_at_least(g_worst_failure_severity, to_lower_copy(fail_on))
                   ? 1 : 0;
        
    } catch (const core::OdbcError& e) {
        std::cerr << "\nODBC Error: " << e.what() << "\n";
        std::cerr << e.format_diagnostics() << "\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 3;
    }
}
