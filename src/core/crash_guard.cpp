#include "crash_guard.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <csetjmp>
#endif

#include <sstream>

namespace odbc_crusher::core {

#ifdef _WIN32

// SEH exception filter: only catch real crashes, let C++ exceptions propagate
static int crash_filter(unsigned int code) {
    // 0xE06D7363 is the MSVC C++ exception code ('msc' | 0xE0000000)
    // Let these propagate so normal try/catch blocks can handle them.
    if (code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// SEH wrapper in a plain-C-style function (no C++ objects with destructors)
// The function pointer is passed as a raw pointer to avoid C++ unwind issues.
static int seh_call(void (*raw_func)(void*), void* ctx, unsigned int* out_code) {
    __try {
        raw_func(ctx);
        return 0;  // success
    }
    __except (crash_filter(GetExceptionCode())) {
        *out_code = GetExceptionCode();
        return 1;  // crashed
    }
}

// Trampoline that calls std::function via a raw context pointer
static void trampoline(void* ctx) {
    auto* func = static_cast<const std::function<void()>*>(ctx);
    (*func)();
}

CrashGuardResult execute_with_crash_guard(const std::function<void()>& func) {
    CrashGuardResult result;
    unsigned int code = 0;
    
    // Pass the std::function by pointer through a C-style trampoline
    auto* func_ptr = &func;
    if (seh_call(trampoline, const_cast<void*>(static_cast<const void*>(func_ptr)), &code)) {
        result.crashed = true;
        result.crash_code = code;
        
        std::ostringstream oss;
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
                oss << "Access violation (0xC0000005)";
                break;
            case EXCEPTION_STACK_OVERFLOW:
                oss << "Stack overflow (0xC00000FD)";
                break;
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                oss << "Division by zero (0xC0000094)";
                break;
            default:
                oss << "Structured exception (0x" << std::hex << code << ")";
                break;
        }
        oss << " - likely a bug in the ODBC driver";
        result.description = oss.str();
    }
    
    return result;
}

#else

// On non-Windows platforms, use signal handlers for SIGSEGV/SIGBUS
#include <setjmp.h>

static thread_local sigjmp_buf s_jmp_env;
static thread_local bool s_in_guard = false;

static void crash_signal_handler(int sig) {
    if (s_in_guard) {
        siglongjmp(s_jmp_env, sig);
    }
}

CrashGuardResult execute_with_crash_guard(const std::function<void()>& func) {
    CrashGuardResult result;
    
    struct sigaction sa_segv = {}, sa_bus = {}, sa_fpe = {};
    struct sigaction old_segv = {}, old_bus = {}, old_fpe = {};
    sa_segv.sa_handler = crash_signal_handler;
    sa_bus.sa_handler = crash_signal_handler;
    sa_fpe.sa_handler = crash_signal_handler;
    sigemptyset(&sa_segv.sa_mask);
    sigemptyset(&sa_bus.sa_mask);
    sigemptyset(&sa_fpe.sa_mask);
    
    sigaction(SIGSEGV, &sa_segv, &old_segv);
    sigaction(SIGBUS, &sa_bus, &old_bus);
    sigaction(SIGFPE, &sa_fpe, &old_fpe);
    
    s_in_guard = true;
    int sig = sigsetjmp(s_jmp_env, 1);
    
    if (sig == 0) {
        func();
    } else {
        result.crashed = true;
        result.crash_code = static_cast<unsigned int>(sig);
        
        std::ostringstream oss;
        switch (sig) {
            case SIGSEGV:
                oss << "Segmentation fault (SIGSEGV)";
                break;
            case SIGBUS:
                oss << "Bus error (SIGBUS)";
                break;
            case SIGFPE:
                oss << "Floating-point exception (SIGFPE)";
                break;
            default:
                oss << "Signal " << sig;
                break;
        }
        oss << " - likely a bug in the ODBC driver";
        result.description = oss.str();
    }
    
    s_in_guard = false;
    sigaction(SIGSEGV, &old_segv, nullptr);
    sigaction(SIGBUS, &old_bus, nullptr);
    sigaction(SIGFPE, &old_fpe, nullptr);
    
    return result;
}

#endif

} // namespace odbc_crusher::core

