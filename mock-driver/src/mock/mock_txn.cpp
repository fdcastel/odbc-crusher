#include "mock_txn.hpp"

#include <algorithm>

namespace mock_odbc {

void apply_ops(std::vector<MockRow>& rows,
               const std::vector<WriteOp>& ops,
               const std::string& table) {
    for (const auto& op : ops) {
        if (op.table != table) continue;
        if (op.kind == WriteOp::Kind::Insert) {
            rows.push_back(op.row);
            continue;
        }
        if (!op.match) continue;
        if (op.kind == WriteOp::Kind::Update) {
            // D83: applied to rows this replay has already inserted as well as
            // committed ones, which is why ops are replayed in issue order.
            for (auto& row : rows) {
                if (!op.match(row)) continue;
                for (const auto& [index, value] : op.assignments) {
                    if (index < row.size()) row[index] = value;
                }
            }
            continue;
        }
        // A DELETE inside the transaction removes rows this replay has already
        // inserted as well as committed ones, for the same reason.
        rows.erase(std::remove_if(rows.begin(), rows.end(), op.match),
                   rows.end());
    }
}

// ── TxnBuffer ─────────────────────────────────────────────────────────────

void TxnBuffer::record(WriteOp op) {
    std::lock_guard<std::mutex> g(mu_);
    ops_.push_back(std::move(op));
}

std::vector<WriteOp> TxnBuffer::ops_for(const std::string& table) const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<WriteOp> out;
    for (const auto& op : ops_) {
        if (op.table == table) out.push_back(op);
    }
    return out;
}

std::vector<WriteOp> TxnBuffer::take_all() {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<WriteOp> out;
    out.swap(ops_);
    return out;
}

void TxnBuffer::discard() {
    std::lock_guard<std::mutex> g(mu_);
    ops_.clear();
}

bool TxnBuffer::empty() const {
    std::lock_guard<std::mutex> g(mu_);
    return ops_.empty();
}

// ── TxnRegistry ───────────────────────────────────────────────────────────

TxnRegistry& TxnRegistry::instance() {
    static TxnRegistry instance;
    return instance;
}

std::shared_ptr<TxnBuffer> TxnRegistry::open(uint64_t conn_id) {
    auto buffer = std::make_shared<TxnBuffer>();
    std::lock_guard<std::mutex> g(mu_);
    buffers_[conn_id] = buffer;
    return buffer;
}

void TxnRegistry::close(uint64_t conn_id) {
    std::lock_guard<std::mutex> g(mu_);
    buffers_.erase(conn_id);
}

std::vector<WriteOp> TxnRegistry::peer_ops(uint64_t self_id,
                                           const std::string& table) const {
    // Copy the shared_ptrs out under this lock, then read the buffers with it
    // released: TxnBuffer has its own mutex, and taking one while holding the
    // other is the only way to build a lock order in this file that could be
    // got wrong.
    std::vector<std::shared_ptr<TxnBuffer>> peers;
    {
        std::lock_guard<std::mutex> g(mu_);
        for (const auto& [id, buffer] : buffers_) {
            if (id != self_id && buffer) peers.push_back(buffer);
        }
    }

    std::vector<WriteOp> out;
    for (const auto& buffer : peers) {
        auto ops = buffer->ops_for(table);
        out.insert(out.end(), ops.begin(), ops.end());
    }
    return out;
}

} // namespace mock_odbc
