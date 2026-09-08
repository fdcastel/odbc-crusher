// Transaction API - SQLEndTran

#include <string>
#include "driver/handles.hpp"
#include "driver/diagnostics.hpp"
#include "mock/behaviors.hpp"
#include "mock/mock_catalog.hpp"
#include "mock/mock_txn.hpp"   // I6
#include "driver/entry_guard.hpp"

using namespace mock_odbc;

extern "C" {

SQLRETURN SQL_API SQLEndTran(
    SQLSMALLINT fHandleType,
    SQLHANDLE hHandle,
    SQLSMALLINT fType) MOCK_ENTRY_TRY {

    // D28: validate the handle before anything else. The fault-injection
    // check used to run first, so a garbage handle came back SQL_ERROR - the
    // one return code that says "the handle was fine, the call was not".
    if (fHandleType != SQL_HANDLE_ENV && fHandleType != SQL_HANDLE_DBC) {
        return SQL_INVALID_HANDLE;
    }
    auto* tx_env = (fHandleType == SQL_HANDLE_ENV)
                 ? validate_env_handle(hHandle) : nullptr;
    auto* tx_conn = (fHandleType == SQL_HANDLE_DBC)
                  ? validate_dbc_handle(hHandle) : nullptr;
    if (!tx_env && !tx_conn) return SQL_INVALID_HANDLE;

    // D28: only SQL_COMMIT and SQL_ROLLBACK are completion types. Treating
    // anything that was not SQL_ROLLBACK as a commit meant a typo in the
    // caller's argument silently committed.
    if (fType != SQL_COMMIT && fType != SQL_ROLLBACK) {
        if (tx_conn) {
            tx_conn->clear_diagnostics();
            tx_conn->add_diagnostic(sqlstate::INVALID_TRANSACTION_OPERATION_CODE, 0,
                                    "Invalid transaction operation code: "
                                    + std::to_string(fType));
        }
        return SQL_ERROR;
    }

    const auto& config = BehaviorController::instance().config();
    if (config.should_fail("SQLEndTran")) {
        if (tx_conn) {
            tx_conn->add_diagnostic(config.error_code, 0,
                                    "Simulated transaction failure");
        }
        // D40: a COMMIT that returns SQL_ERROR did not commit, so its rows
        // must stop being visible. The mock used to return the error and
        // leave every inserted row in place, so a probe that INSERTs,
        // commits, and then verifies persistence still *passed* while the
        // driver was reporting a failed commit - the fixture could not
        // produce the one shape A22 exists to report. A failed ROLLBACK is
        // left alone: there, the rows may legitimately still be there.
        //
        // I6: D40's rationale, without D40's collateral damage. This used to
        // call clear_inserted_data(), which deletes every row of every table
        // for every connection - so a failed commit on one connection erased
        // work another had committed. Discarding this connection's buffer is
        // what "did not commit" actually means.
        if (fType == SQL_COMMIT) {
            if (tx_conn && tx_conn->pending_writes()) {
                tx_conn->pending_writes()->discard();
            }
            if (tx_env) {
                for (auto* conn : tx_env->connections_) {
                    if (conn && conn->pending_writes()) {
                        conn->pending_writes()->discard();
                    }
                }
            }
        }
        return SQL_ERROR;
    }

    config.apply_latency();

    // D28: a rollback only discards what an *open transaction* accumulated.
    // This used to call clear_inserted_data() on any rollback, autocommit ON
    // included - so a probe that inserted rows in autocommit mode and then
    // rolled back (which should do nothing) lost them, and the mock whose
    // purpose is validating "the rows persisted" assertions was the thing
    // deleting the rows.
    auto end_transaction = [&](ConnectionHandle* conn) {
        conn->in_transaction_ = false;
        for (auto* stmt : conn->statements_) {
            stmt->cursor_open_ = false;
            if (fType == SQL_ROLLBACK) {
                stmt->executed_ = false;
                stmt->result_data_.clear();
            }
        }

        // I6: the transaction's own writes, and nobody else's.
        //
        // This used to be `clear_inserted_data()` guarded by
        // `had_transaction` - it deleted every row of every table for every
        // connection, so a rollback here destroyed data another connection had
        // committed. Two probes in the tool were reading a store this had
        // already wiped and passing whatever the driver did.
        //
        // The guard goes with it. It existed because a rollback in autocommit
        // mode used to delete data (D28); with a buffer there is nothing to
        // discard in autocommit mode, so the right answer falls out instead of
        // being special-cased.
        auto* writes = conn->pending_writes();
        if (!writes) return;
        if (fType == SQL_ROLLBACK) {
            writes->discard();
        } else {
            MockCatalog::instance().apply_write_ops(writes->take_all());
        }
    };

    if (tx_env) {
        for (auto* conn : tx_env->connections_) {
            end_transaction(conn);
        }
        return SQL_SUCCESS;
    }

    tx_conn->clear_diagnostics();
    if (!tx_conn->is_connected()) {
        tx_conn->add_diagnostic(sqlstate::CONNECTION_NOT_OPEN, 0,
                                "Connection not open");
        return SQL_ERROR;
    }
    end_transaction(tx_conn);

    return SQL_SUCCESS;
}
MOCK_ENTRY_CATCH(hHandle)

} // extern "C"
