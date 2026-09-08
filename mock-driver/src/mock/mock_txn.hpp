#pragma once

// Per-connection uncommitted writes — IMPROVEMENT_PLAN.md I6.
//
// Until this existed the mock had **no uncommitted state at all**. Every INSERT
// and DELETE landed in the one process-global row store immediately, and
// ROLLBACK approximated "undo" by calling `MockCatalog::clear_inserted_data()`,
// which deletes every row of every table for every connection. Two consequences
// that a driver-conformance fixture cannot afford:
//
//   * a connection rolling back destroyed data other connections had
//     *committed*, so the two sibling-connection probes in the tool were reading
//     a wiped store and passing whatever the driver did; and
//   * no isolation-level probe could be given a configuration that makes it
//     fail, because there was no such thing as a row that was written but not
//     yet visible.
//
// The row's title says "per-connection isolation of inserted rows", and that is
// the wrong destination: two connections to one database share **committed**
// data by definition, and modelling them as separate databases would break
// CatalogSharing.SecondConnectKeepsTheFirstConnectionsTables, which asserts
// exactly that and asserts it correctly. What was missing is transaction
// isolation - a per-connection buffer of writes that have not landed yet.
//
// **An ordered op log, not a snapshot of the table.** Deleting a *committed*
// row needs to identify it, and MockRow has no identity - no rowid, and
// duplicates are legal. Recording the predicate instead sidesteps that, and it
// makes COMMIT a *merge* against whatever is committed at commit time, so two
// connections writing to the same table do not clobber each other. Storing the
// predicate is safe: the lambdas `make_where_filter` builds capture by value.

#include "mock_catalog.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace mock_odbc {

// One buffered change. Kept in issue order, because a DELETE after an INSERT in
// the same transaction has to see that INSERT.
struct WriteOp {
    enum class Kind { Insert, Delete };

    Kind kind = Kind::Insert;
    std::string table;                          // upper-cased, as the store is
    MockRow row;                                // Kind::Insert
    std::function<bool(const MockRow&)> match;  // Kind::Delete
};

// Replay `ops` over `rows`. The one implementation, used both to answer a read
// inside a transaction and to merge at COMMIT — so the rows a transaction sees
// are produced by the same code that will eventually make them real.
void apply_ops(std::vector<MockRow>& rows,
               const std::vector<WriteOp>& ops,
               const std::string& table);

// The uncommitted writes of one connection.
//
// Its own mutex, and it is never held while MockCatalog's is: every path here
// is copy-then-apply, so there is no lock nesting to get the order wrong.
class TxnBuffer {
public:
    void record(WriteOp op);

    // The ops touching one table, copied so the caller can replay without
    // holding this lock.
    std::vector<WriteOp> ops_for(const std::string& table) const;

    // COMMIT: hand the ops over and forget them.
    std::vector<WriteOp> take_all();

    // ROLLBACK: forget them.
    void discard();

    bool empty() const;

private:
    mutable std::mutex mu_;
    std::vector<WriteOp> ops_;
};

// What the executor needs to know about the connection running a statement.
// Deliberately knows nothing about ODBC handles, so mock_data.cpp does not have
// to include the handle definitions.
struct TxnContext {
    // Writes are buffered rather than committed. Keyed on autocommit being OFF,
    // *not* on ConnectionHandle::in_transaction_, which is set after the
    // executor runs and so is still false during a transaction's first
    // statement.
    bool buffered = false;

    // Reads also see other connections' uncommitted writes.
    bool read_uncommitted = false;

    TxnBuffer* writes = nullptr;
    uint64_t conn_id = 0;
};

// Every live connection's buffer, so a READ UNCOMMITTED reader can see its
// peers' pending writes — I6's stated point, since without it no isolation
// probe has a configuration that can fail.
class TxnRegistry {
public:
    static TxnRegistry& instance();

    std::shared_ptr<TxnBuffer> open(uint64_t conn_id);
    void close(uint64_t conn_id);

    // The pending ops of every connection *other* than `self_id` that touch
    // `table`. Copied under this lock and replayed outside it.
    std::vector<WriteOp> peer_ops(uint64_t self_id,
                                  const std::string& table) const;

private:
    TxnRegistry() = default;

    mutable std::mutex mu_;
    std::unordered_map<uint64_t, std::shared_ptr<TxnBuffer>> buffers_;
};

} // namespace mock_odbc
