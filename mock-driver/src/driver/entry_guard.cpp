#include "entry_guard.hpp"

#include "diagnostics.hpp"

#include <string>

namespace mock_odbc {

SQLRETURN report_entry_exception(SQLHANDLE handle,
                                 const char* function,
                                 const char* what,
                                 bool out_of_memory) noexcept {
    // Everything here runs while unwinding a failure we did not anticipate,
    // so it is wrapped in its own catch-all: a barrier that can itself throw
    // is worse than no barrier.
    try {
        if (handle) {
            // The same unchecked static_cast every validate_*_handle in this
            // driver performs; is_valid() then rejects anything that is not
            // one of ours. By the time an entry point body throws, its handle
            // has almost always already been validated by that same body.
            auto* h = static_cast<OdbcHandle*>(handle);
            if (h->is_valid()) {
                std::string message = "Unhandled exception in ";
                message += (function ? function : "entry point");
                message += ": ";
                message += (what ? what : "non-std::exception");
                message +=
                    ". This is a defect in the mock driver, not a condition "
                    "the application caused.";
                h->add_diagnostic(out_of_memory
                                      ? sqlstate::MEMORY_ALLOCATION_ERROR
                                      : sqlstate::GENERAL_ERROR,
                                  0, message);
            }
        }
    } catch (...) {
        // Deliberately swallowed — see above.
    }
    return SQL_ERROR;
}

}  // namespace mock_odbc
