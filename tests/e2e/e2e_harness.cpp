#include "e2e_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifndef CRUSHER_BIN_PATH
#error "CRUSHER_BIN_PATH must be defined by the build (path to odbc-crusher binary)"
#endif

namespace odbc_crusher::e2e {

namespace fs = std::filesystem;

namespace {

std::string quote_arg(const std::string& s) {
    // Wrap in double quotes; escape internal double-quotes by doubling.
    // Sufficient for our scenarios — none of the connection strings or
    // paths we pass contain backslashes followed by quotes.
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += '"';
    return out;
}

fs::path unique_tmp_json() {
    static std::atomic<int> counter{0};
    auto pid = static_cast<long long>(
#ifdef _WIN32
        _getpid()
#else
        getpid()
#endif
    );
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    auto name = "crusher_e2e_" + std::to_string(pid) + "_" +
                std::to_string(stamp) + "_" +
                std::to_string(counter.fetch_add(1)) + ".json";
    return fs::temp_directory_path() / name;
}

} // namespace

CrusherRun run_crusher(const std::string& connection_string) {
    CrusherRun out;

    auto tmp = unique_tmp_json();
    auto stderr_log = tmp;
    stderr_log.replace_extension(".stderr.log");

    std::error_code ec;
    fs::remove(tmp, ec);
    fs::remove(stderr_log, ec);

    // Build command. We capture stderr via shell redirection — stdout is
    // a single "JSON report written to: …" line which we don't need.
    std::string cmd = quote_arg(CRUSHER_BIN_PATH);
    cmd += ' ';
    cmd += quote_arg(connection_string);
    cmd += " -o json -f ";
    cmd += quote_arg(tmp.string());
#ifdef _WIN32
    cmd += " > NUL 2> ";
#else
    cmd += " > /dev/null 2> ";
#endif
    cmd += quote_arg(stderr_log.string());

    // Wrapping the whole command in quotes is required on Windows when
    // both the program and an argument are quoted — cmd.exe strips the
    // outermost pair before parsing.
#ifdef _WIN32
    std::string wrapped = "\"" + cmd + "\"";
    int rc = std::system(wrapped.c_str());
#else
    int rc = std::system(cmd.c_str());
#endif

    out.launched = true;
    out.exit_code = rc;

    // Slurp stderr if present (best-effort).
    if (fs::exists(stderr_log, ec)) {
        std::ifstream in(stderr_log);
        std::stringstream ss;
        ss << in.rdbuf();
        out.raw_stderr = ss.str();
        fs::remove(stderr_log, ec);
    }

    if (fs::exists(tmp, ec)) {
        std::ifstream in(tmp);
        try {
            in >> out.report;
        } catch (const std::exception& e) {
            out.raw_stderr += "\n[harness] JSON parse error: ";
            out.raw_stderr += e.what();
        }
        fs::remove(tmp, ec);
    }

    return out;
}

bool has_runnable_mock() {
    static const bool cached = []() {
        // Use a fast, side-effect-free invocation: just ConnectionTests
        // category isn't filterable from CLI, so we live with a full run.
        // Run once and stash. Anyone calling this in test setup gets a
        // cached answer for the rest of the process lifetime.
        auto run = run_crusher(
            "Driver={Mock ODBC Driver};Mode=Success;Catalog=Default;"
            "ResultSetSize=1;");
        if (!run.launched) return false;
        if (run.report.is_null() || run.report.empty()) return false;
        // Driver-load failure shows up as an OdbcError before any tests
        // run — the report file never gets written.
        return run.report.contains("summary");
    }();
    return cached;
}

std::optional<nlohmann::json> find_category(const nlohmann::json& report,
                                             const std::string& category) {
    if (!report.is_object() || !report.contains("categories")) return std::nullopt;
    for (const auto& cat : report["categories"]) {
        if (cat.value("name", std::string{}) == category) return cat;
    }
    return std::nullopt;
}

std::optional<nlohmann::json> find_test(const nlohmann::json& report,
                                         const std::string& category,
                                         const std::string& test_name) {
    auto cat = find_category(report, category);
    if (!cat) return std::nullopt;
    if (!cat->contains("tests")) return std::nullopt;
    for (const auto& t : (*cat)["tests"]) {
        if (t.value("test_name", std::string{}) == test_name) return t;
    }
    return std::nullopt;
}

} // namespace odbc_crusher::e2e
