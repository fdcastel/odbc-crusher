#include "e2e_harness.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <process.h>   // _getpid
#include <windows.h>
#else
#include <csignal>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>    // getpid
extern char** environ;
#endif

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

namespace {

// D55: how long a single crusher invocation may take before the harness gives
// up on it. The whole 30-scenario suite runs in about a second on a CI runner,
// so this is not a performance bound - it is the difference between a hung
// child failing one test with its partial report attached and stalling the job
// until GitHub's six-hour cap. Found the hard way: a sanitizer build wedged on
// the first scenario and burned an hour before anyone looked.
//
// D57 lowered it from 180s. A scenario can run two children - one for
// has_runnable_mock(), one of its own - and two 180s deadlines do not fit
// inside ctest's 300s, so a wedge showed up as a ctest timeout with no output
// rather than as the harness reporting what it killed. At 60s both fit, and
// 60x the natural runtime is still generous for a sanitizer build.
long long spawn_timeout_seconds() {
    if (const char* env = std::getenv("ODBC_CRUSHER_E2E_TIMEOUT_SECONDS")) {
        const long long parsed = std::atoll(env);
        if (parsed > 0) return parsed;
    }
    return 60;
}

// Shell out to the binary, capturing stderr to a file and stdout either to
// the null device (when the report goes to `-f`) or to a file (when it goes
// to stdout). Returns the exit code; sets `timed_out` and kills the child if
// it outlives spawn_timeout_seconds().
int spawn(const std::string& connection_string,
          const fs::path& report_file,       // empty => report goes to stdout
          const fs::path& stdout_log,        // empty => stdout to null device
          const fs::path& stderr_log,
          bool& timed_out,
          const std::vector<std::string>& extra_args = {}) {
    std::string cmd = quote_arg(CRUSHER_BIN_PATH);
    cmd += ' ';
    cmd += quote_arg(connection_string);
    cmd += " -o json";
    for (const auto& arg : extra_args) {      // G2/G3
        cmd += ' ';
        cmd += quote_arg(arg);
    }
    if (!report_file.empty()) {
        cmd += " -f ";
        cmd += quote_arg(report_file.string());
    }
    if (stdout_log.empty()) {
#ifdef _WIN32
        cmd += " > NUL";
#else
        cmd += " > /dev/null";
#endif
    } else {
        cmd += " > ";
        cmd += quote_arg(stdout_log.string());
    }
    cmd += " 2> ";
    cmd += quote_arg(stderr_log.string());

    // Wrapping the whole command in quotes is required on Windows when
    // both the program and an argument are quoted — cmd.exe strips the
    // outermost pair before parsing.
    // D55: std::system() waits forever, so this runs the shell itself and
    // enforces a deadline. A shell is still in the middle rather than an exec
    // of the binary directly because of the redirections built above.
    timed_out = false;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(spawn_timeout_seconds());

#ifdef _WIN32
    std::string command_line = "cmd.exe /c \"" + cmd + "\"";

    // A job object, because the process this spawns is cmd.exe and the one
    // that wedges is odbc-crusher underneath it: TerminateProcess on the
    // shell leaves the grandchild running, still holding the inherited
    // stderr handle. Observed exactly that on the first attempt. Everything
    // in the job dies together, and KILL_ON_JOB_CLOSE covers the paths that
    // return early as well.
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                &limits, sizeof(limits));
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // Suspended, so the child is in the job before it can spawn anything of
    // its own that would escape it.
    if (!CreateProcessA(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        if (job) CloseHandle(job);
        return -1;
    }
    if (job) AssignProcessToJobObject(job, pi.hProcess);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (WaitForSingleObject(
            pi.hProcess,
            static_cast<DWORD>(remaining.count() > 0 ? remaining.count() : 0))
        == WAIT_TIMEOUT) {
        timed_out = true;
        if (job) {
            TerminateJobObject(job, 1);
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        WaitForSingleObject(pi.hProcess, 5000);
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    if (job) CloseHandle(job);
    return static_cast<int>(exit_code);
#else
    const char* argv[] = {"/bin/sh", "-c", cmd.c_str(), nullptr};

    // Own process group, for the same reason as the job object above: the
    // process spawned here is /bin/sh and the one that wedges is odbc-crusher
    // underneath it, so the kill has to name the group rather than the shell.
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);

    pid_t pid = 0;
    const int spawn_rc = posix_spawn(&pid, "/bin/sh", nullptr, &attr,
                                     const_cast<char* const*>(argv), environ);
    posix_spawnattr_destroy(&attr);
    if (spawn_rc != 0) {
        return -1;
    }

    int status = 0;
    for (;;) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) return -1;
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            // SIGKILL, not SIGTERM: the child being killed here is by
            // definition one that stopped making progress, and a driver that
            // wedged inside an ODBC call will not run a signal handler out
            // of it either. Negative pid: the whole group, so odbc-crusher
            // goes with the shell that launched it.
            kill(-pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
#endif
}

std::string slurp_and_remove(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return {};
    std::ifstream in(p, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    fs::remove(p, ec);
    return ss.str();
}

}  // namespace

CrusherRun run_crusher(const std::string& connection_string) {
    CrusherRun out;

    auto tmp = unique_tmp_json();
    auto stderr_log = tmp;
    stderr_log.replace_extension(".stderr.log");

    std::error_code ec;
    fs::remove(tmp, ec);
    fs::remove(stderr_log, ec);

    out.exit_code = spawn(connection_string, tmp, fs::path{}, stderr_log,
                          out.timed_out);
    out.launched = true;
    out.raw_stderr = slurp_and_remove(stderr_log);
    if (out.timed_out) {
        // D55: F2 makes crusher snapshot its report after every category, so
        // what it left on disk names the last category it finished - which is
        // the diagnosis, and the reason the report is still read below.
        out.raw_stderr += "\n[harness] crusher did not exit within " +
                          std::to_string(spawn_timeout_seconds()) +
                          "s and was killed. The report below is what it had "
                          "written when that happened.";
    }

    if (fs::exists(tmp, ec)) {
        std::ifstream in(tmp);
        try {
            in >> out.report;
        } catch (const std::exception& e) {
            out.raw_stderr += "\n[harness] JSON parse error: ";
            out.raw_stderr += e.what();
        }
        in.close();
        fs::remove(tmp, ec);
    }

    return out;
}

CrusherRun run_crusher_with_args(const std::string& connection_string,
                                 const std::vector<std::string>& extra_args) {
    CrusherRun out;

    auto tmp = unique_tmp_json();
    auto stderr_log = tmp;
    stderr_log.replace_extension(".stderr.log");

    std::error_code ec;
    fs::remove(tmp, ec);
    fs::remove(stderr_log, ec);

    out.exit_code = spawn(connection_string, tmp, fs::path{}, stderr_log,
                          out.timed_out, extra_args);
    out.launched = true;
    out.raw_stderr = slurp_and_remove(stderr_log);

    if (fs::exists(tmp, ec)) {
        std::ifstream in(tmp);
        try {
            in >> out.report;
        } catch (const std::exception& e) {
            out.raw_stderr += "\n[harness] JSON parse error: ";
            out.raw_stderr += e.what();
        }
        in.close();
        fs::remove(tmp, ec);
    }
    return out;
}

CrusherRun run_crusher_stdout(const std::string& connection_string,
                              const std::vector<std::string>& extra_args) {
    CrusherRun out;

    auto base = unique_tmp_json();
    auto stdout_log = base;
    stdout_log.replace_extension(".stdout.log");
    auto stderr_log = base;
    stderr_log.replace_extension(".stderr.log");

    std::error_code ec;
    fs::remove(stdout_log, ec);
    fs::remove(stderr_log, ec);

    out.exit_code = spawn(connection_string, fs::path{}, stdout_log, stderr_log,
                          out.timed_out, extra_args);
    out.launched = true;
    out.raw_stderr = slurp_and_remove(stderr_log);
    out.raw_stdout = slurp_and_remove(stdout_log);

    if (!out.raw_stdout.empty()) {
        try {
            out.report = nlohmann::json::parse(out.raw_stdout);
        } catch (const std::exception& e) {
            out.raw_stderr += "\n[harness] stdout JSON parse error: ";
            out.raw_stderr += e.what();
        }
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
