#pragma once

#include "reporter.hpp"

#include <memory>
#include <utility>

namespace odbc_crusher::reporting {

// S5 — one run, both reports.
//
// `run-crusher` used to invoke crusher twice: once for the console text, once
// for the JSON. Two runs of identical work, and the second one inheriting
// whatever the first left behind — which is how a wedged driver produced a
// **text** report covering ten categories and a **JSON** report covering one.
// The JSON is the machine-readable half: every triage, every
// `compare_reports.py` diff and every number in IMPROVEMENT_PLAN_V2 comes from
// it, so the half that survived worst was the half that mattered.
//
// The snapshot cadence was not the cause (`kSnapshotInterval` is one second).
// The cause was structural: the second invocation met a Firebird server whose
// connection the first invocation had left wedged, and stopped earlier.
//
// Forwarding to both reporters from one run removes the second invocation
// entirely. The JSON is then as complete as the text *by construction* rather
// than by luck, and the stress-test wall-clock halves for every driver in the
// manifest.
//
// Order matters and is fixed: the JSON child is written first so that a
// SIGKILL arriving mid-report has already updated the file on disk, and the
// console child — which only writes to a stream someone is watching — goes
// second.
class TeeReporter : public Reporter {
public:
    TeeReporter(std::unique_ptr<Reporter> first, std::unique_ptr<Reporter> second)
        : first_(std::move(first)), second_(std::move(second)) {}

    void report_start(const std::string& connection_string) override {
        first_->report_start(connection_string);
        second_->report_start(connection_string);
    }

    void report_driver_info(const discovery::DriverInfo::Properties& props) override {
        first_->report_driver_info(props);
        second_->report_driver_info(props);
    }

    void report_type_info(const std::vector<discovery::TypeInfo::DataType>& types) override {
        first_->report_type_info(types);
        second_->report_type_info(types);
    }

    void report_function_info(const discovery::FunctionInfo::FunctionSupport& funcs) override {
        first_->report_function_info(funcs);
        second_->report_function_info(funcs);
    }

    void report_scalar_functions(
        const discovery::DriverInfo::ScalarFunctionSupport& sf) override {
        first_->report_scalar_functions(sf);
        second_->report_scalar_functions(sf);
    }

    void report_category(const std::string& category_name,
                         const std::vector<tests::TestResult>& results) override {
        first_->report_category(category_name, results);
        second_->report_category(category_name, results);
    }

    void report_summary(size_t total_tests, size_t passed, size_t failed,
                        size_t skipped, size_t errors, size_t informational,
                        std::chrono::microseconds total_duration) override {
        first_->report_summary(total_tests, passed, failed, skipped, errors,
                               informational, total_duration);
        second_->report_summary(total_tests, passed, failed, skipped, errors,
                                informational, total_duration);
    }

    void report_end() override {
        first_->report_end();
        second_->report_end();
    }

    void report_selected_categories(const std::vector<std::string>& available,
                                    const std::vector<std::string>& selected) override {
        first_->report_selected_categories(available, selected);
        second_->report_selected_categories(available, selected);
    }

private:
    std::unique_ptr<Reporter> first_;
    std::unique_ptr<Reporter> second_;
};

} // namespace odbc_crusher::reporting
