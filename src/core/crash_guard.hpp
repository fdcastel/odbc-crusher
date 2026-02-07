#pragma once

#include <string>
#include <functional>

namespace odbc_crusher::core {

// Result of a crash-guarded operation
struct CrashGuardResult {
    bool crashed = false;
    unsigned int crash_code = 0;
    std::string description;
};

// Execute a function with crash protection (catches access violations etc.)
// This uses Windows SEH on Windows and signal handlers on Unix.
CrashGuardResult execute_with_crash_guard(const std::function<void()>& func);

} // namespace odbc_crusher::core
