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

    // S8: the most categories that can be missing from a report whose process
    // died without warning is this minus one. Public because it is a promise
    // about the artifact rather than an implementation detail - the test that
    // pins it asserts against this constant rather than a literal, so changing
    // the trade here cannot silently weaken the guarantee.
    static constexpr size_t kMaxUnwrittenCategories = 4;

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

    // Snapshot after a category. Always writes the first one, so a report
    // exists from early on; after that when *either* bound below is reached.
    //
    // S8: the time bound used to be the only one, and its justification was
    // "a snapshot up to a second stale is exactly as useful as one that is
    // current". That is false whenever the run is fast, and it cost a real
    // report: against Firebird on localhost the whole suite is **0.7 seconds
    // of probe time**, so on the U2 Linux run nine categories completed inside
    // one window, every one of their writes was skipped, and the process then
    // aborted (P17). The text report held nineteen categories; the JSON held
    // ten — and the JSON is the half every triage and every report diff reads.
    //
    // The risk is measured in categories lost, so the bound is too. At most
    // kMaxUnwrittenCategories - 1 categories can now be missing from a report
    // whose process died without warning, however fast they ran.
    //
    // Both bounds are kept because they cover opposite cases: against a slow
    // driver each category exceeds the time bound and writes anyway, exactly
    // as before; against a fast one the count bound is what fires.
    //
    // Cost, measured on this build against the mock (24 categories, ~120 KB
    // document), median of five runs:
    //
    //                        Debug     Release (what CI runs)
    //   time bound only      393 ms    261 ms
    //   every category       582 ms    306 ms
    //   both bounds (this)     -       263 ms
    //
    // Writing every time costs ~45 ms in Release, which is noise beside the
    // stress-test job's install steps, and ~190 ms in Debug, which is not: the
    // e2e suite spawns crusher 55 times. Both bounds together cost **~2 ms** in
    // Release and about 4.7 s across the Debug e2e suite, for a worst case of
    // three categories lost instead of all of them. The earlier "~150 ms" in
    // this comment was a Debug figure presented as though it were the cost in
    // CI.
    void maybe_write_snapshot();

    static constexpr std::chrono::seconds kSnapshotInterval{1};

    std::string output_file_;
    nlohmann::json root_;
    nlohmann::json categories_;
    bool write_failed_ = false;   // Report a bad output path once, not 23 times
    bool wrote_snapshot_ = false;
    std::chrono::steady_clock::time_point last_snapshot_{};
    size_t unwritten_categories_ = 0;   // S8
};

// D82: replace any element of `array_of_categories` that will not survive a
// JSON round-trip, and return how many. Declared here rather than kept
// file-local so it can be tested directly: the states it exists for - a json
// node with a corrupt type byte - cannot be reached through the reporter's
// public API, and a guard that cannot be exercised is the thing this project
// keeps finding.
size_t quarantine_unserialisable(nlohmann::json& array_of_categories);

} // namespace odbc_crusher::reporting
