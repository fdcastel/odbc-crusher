#pragma once

// Exception barrier for the C ABI boundary — IMPROVEMENT_PLAN.md D9.
//
// Every SQL_API function in this driver is called across a C ABI, from the
// Driver Manager (odbc32.dll / libodbc.so), which is C and has no idea what
// a C++ exception is. Letting one unwind out of an entry point is undefined
// behaviour and in practice terminates the host process: it takes the
// application down with it, from inside a *mock* whose entire purpose is to
// let a test harness survive misbehaving drivers.
//
// This was not hypothetical. `ResultSetSize=-1` reached
// `std::vector::reserve(SIZE_MAX)` and threw std::length_error;
// `ResultSetSize=500000000` threw std::bad_alloc. Neither had a handler
// anywhere between the throw and odbc32.dll. The value is clamped at parse
// time now, but the barrier is what makes the *class* of bug survivable.
//
// Usage — a function-try-block, so the body keeps its original indentation
// and the diff stays reviewable:
//
//     SQLRETURN SQL_API SQLFoo(SQLHSTMT hstmt, ...) MOCK_ENTRY_TRY {
//         ... body unchanged ...
//     }
//     MOCK_ENTRY_CATCH(hstmt)
//
// Function parameters are in scope inside the handlers of a function-try-
// block, so the handle argument is just the entry point's own parameter.
// Pass SQL_NULL_HANDLE when the function has no handle to post on.

#include "handles.hpp"

#include <exception>
#include <new>

namespace mock_odbc {

// Post a diagnostic for an exception that escaped an entry-point body, and
// return the code the DM should see.
//
// `handle` may be null, or a pointer the caller never validated; both are
// handled. `what` may be null for a non-std exception. Never throws — a
// barrier that can fail is not a barrier.
SQLRETURN report_entry_exception(SQLHANDLE handle,
                                 const char* function,
                                 const char* what,
                                 bool out_of_memory) noexcept;

}  // namespace mock_odbc

#define MOCK_ENTRY_TRY try

#define MOCK_ENTRY_CATCH(handle)                                               \
    catch (const std::bad_alloc& e) {                                          \
        return ::mock_odbc::report_entry_exception((handle), __func__,         \
                                                   e.what(), true);            \
    }                                                                          \
    catch (const std::exception& e) {                                          \
        return ::mock_odbc::report_entry_exception((handle), __func__,         \
                                                   e.what(), false);           \
    }                                                                          \
    catch (...) {                                                              \
        return ::mock_odbc::report_entry_exception((handle), __func__,         \
                                                   nullptr, false);            \
    }
