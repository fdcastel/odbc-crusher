#pragma once

#include "reporter.hpp"
#include "discovery/driver_info.hpp"
#include "discovery/type_info.hpp"
#include "discovery/function_info.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
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
    //   2 — S4. Adds "environment" (crusher version, platform, architecture,
    //       pointer and SQLLEN width, build type) and
    //       "driver_info.driver_manager_version", both additive. The bump is
    //       for the third change, which is not: "connection_string" now has
    //       the value of every secret-looking keyword replaced with `***`.
    //       The reports are published as CI artifacts and were carrying
    //       `PWD=masterkey` in clear. A consumer that reconnected with this
    //       string will no longer be able to, which is precisely the kind of
    //       meaning change this number exists to announce.
    static constexpr int kSchemaVersion = 2;

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
                        size_t informational,
                       std::chrono::microseconds total_duration) override;
    void report_end() override;
    void report_selected_categories(
        const std::vector<std::string>& available,
        const std::vector<std::string>& selected) override;
    
private:
    // F2: write the report as it stands to output_file_, atomically.
    //
    // CI wraps crusher in `timeout --kill-after=30 570`, and the report used
    // to be serialised only in report_end(), so a run that hung produced no
    // JSON at all — precisely when the report matters most. (The text run
    // survives because it is piped through tee.) A snapshot after every
    // category means a SIGKILL leaves a valid document containing everything
    // that completed. Chose this over the per-category watchdog the task also
    // offered: a watchdog inside the process cannot survive SIGKILL, and
    // interrupting a wedged SQLExecute would need SQLCancel from a second
    // thread — which a hang-prone driver is exactly the one not to honour.
    //
    // No-op when writing to stdout: there the document is emitted once, and
    // partial copies would break the single-document contract G1 established.
    void write_snapshot(bool complete);

    // Snapshot after a category, subject to a rate limit. Always writes the
    // first one, so a report exists from early on; after that at most one per
    // kSnapshotInterval. Rewriting the whole ~100 KB document after each of 23
    // categories cost ~150 ms per run and took the e2e suite from 5.6s to 10s,
    // for no benefit: CI kills crusher at 570 seconds, so a snapshot that is
    // up to a second stale is exactly as useful as one that is current.
    void maybe_write_snapshot();

    static constexpr std::chrono::seconds kSnapshotInterval{1};

    std::string output_file_;
    nlohmann::json root_;
    nlohmann::json categories_;
    bool write_failed_ = false;   // Report a bad output path once, not 23 times
    bool wrote_snapshot_ = false;
    std::chrono::steady_clock::time_point last_snapshot_{};
};

// D82: replace any element of `array_of_categories` that will not survive a
// JSON round-trip, and return how many. Declared here rather than kept
// file-local so it can be tested directly: the states it exists for - a json
// node with a corrupt type byte - cannot be reached through the reporter's
// public API, and a guard that cannot be exercised is the thing this project
// keeps finding.
size_t quarantine_unserialisable(nlohmann::json& array_of_categories);

} // namespace odbc_crusher::reporting
