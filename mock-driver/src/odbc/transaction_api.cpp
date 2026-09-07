// Transaction API - SQLEndTran

#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/behaviors.hpp"
#include "mock/mock_catalog.hpp"
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLEndTran(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT fType) MOCK_ENTRY_TRY {
    
    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLEndTran")) {
        if (fHandleType == SQL_HANDLE_DBC) {
            auto* conn = validate_dbc_handle(hHandle);
            if (conn) {
                conn->add_diagnostic(config.error_code, 0, "Simulated transaction failure");
            }
        }
        // D40: a COMMIT that returns SQL_ERROR did not commit, so its rows
        // must stop being visible. The mock used to return the error and
        // leave every inserted row in place, so a probe that INSERTs,
        // commits, and then verifies persistence still *passed* while the
        // driver was reporting a failed commit - the fixture could not
        // produce the one shape A22 exists to report. A failed ROLLBACK is
        // left alone: there, the rows may legitimately still be there.
        if (fType == SQL_COMMIT) {
            MockCatalog::instance().clear_inserted_data();
        }
        return SQL_ERROR;
    }
    
    config.apply_latency();
    
    if (fHandleType == SQL_HANDLE_ENV) {
        auto* env = validate_env_handle(hHandle);
        if (!env) return SQL_INVALID_HANDLE;
        
        // Commit/rollback all connections
        for (auto* conn : env->connections_) {
            // Close all cursors
            for (auto* stmt : conn->statements_) {
                stmt->cursor_open_ = false;
                if (fType == SQL_ROLLBACK) {
                    stmt->executed_ = false;
                    stmt->result_data_.clear();
                }
            }
        }
        
        if (fType == SQL_ROLLBACK) {
            MockCatalog::instance().clear_inserted_data();
        }
    } else if (fHandleType == SQL_HANDLE_DBC) {
        auto* conn = validate_dbc_handle(hHandle);
        if (!conn) return SQL_INVALID_HANDLE;
        
        conn->clear_diagnostics();
        
        if (!conn->is_connected()) {
            conn->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                                "Connection not open");
            return SQL_ERROR;
        }
        
        // Close all cursors on this connection
        for (auto* stmt : conn->statements_) {
            stmt->cursor_open_ = false;
            if (fType == SQL_ROLLBACK) {
                stmt->executed_ = false;
                stmt->result_data_.clear();
            }
        }
        
        if (fType == SQL_ROLLBACK) {
            MockCatalog::instance().clear_inserted_data();
        }
    } else {
        return SQL_INVALID_HANDLE;
    }
    
    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hHandle)

} // extern "C"
