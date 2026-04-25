// E2E harness — IMPROVEMENT_PLAN.md §5.1
//
// Spawns the odbc-crusher binary as a subprocess against a mock connection
// string, captures the JSON report, and exposes per-test lookups so a
// scenario can assert on individual outcomes. The path to the binary is
// injected at compile time via CRUSHER_BIN_PATH (set in tests/e2e CMake).
//
// Tests must call has_runnable_mock() first and GTEST_SKIP() when false —
// the mock driver is registered globally on the host, and a stale or
// missing registration is a developer-environment problem, not a test
// failure.
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace odbc_crusher::e2e {

struct CrusherRun {
    int exit_code = -1;
    nlohmann::json report;          // The parsed JSON output (empty on failure)
    std::string raw_stderr;         // Captured for diagnostics
    bool launched = false;          // false iff the binary couldn't be invoked
};

// Run odbc-crusher against the given connection string. Always uses
// `-o json -f <tmp>` and reads the report back from the temp file.
// Returns CrusherRun{launched=false} if the binary itself cannot run.
CrusherRun run_crusher(const std::string& connection_string);

// Probe whether the mock driver is loadable on this host. Skips
// scenarios with GTEST_SKIP() when false. Caches the result.
bool has_runnable_mock();

// Lookups against a parsed report. Each returns nullopt when not present.
std::optional<nlohmann::json> find_test(const nlohmann::json& report,
                                         const std::string& category,
                                         const std::string& test_name);

std::optional<nlohmann::json> find_category(const nlohmann::json& report,
                                             const std::string& category);

} // namespace odbc_crusher::e2e
