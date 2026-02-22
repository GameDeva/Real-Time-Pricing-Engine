#include "market_data/OrderBook.h"

#include <mutex>
#include <stdexcept>

// ── Private helpers ────────────────────────────────────────────────────────────

template<typename Map>
void OrderBook::pruneToMax(Map& m) {
    while (static_cast<int>(m.size()) > kMaxLevels) {
        // end() is the worst-priority entry for both bid (lowest) and ask (highest).
        auto last = m.end();
        --last;
        m.erase(last);
    }
}

void OrderBook::refreshMidCache() {
    // Called while holding the exclusive write lock — reads are safe without an
    // additional lock here. Stores atomically so getMidPrice() never sees a torn
    // value from another thread.
    double bid = bids_.empty() ? 0.0 : bids_.begin()->first;
    double ask = asks_.empty() ? 0.0 : asks_.begin()->first;
    double mid = (bid > 0.0 && ask > 0.0) ? (bid + ask) * 0.5 : 0.0;
    mid_cache_.store(mid, std::memory_order_release);
}

// ── updateLevel ────────────────────────────────────────────────────────────────

void OrderBook::updateLevel(double price, double qty, bool is_bid) {
    if (price <= 0.0) return; // defensive: ignore invalid prices

    std::unique_lock lock(mutex_);

    // Dispatch directly to the correct map — the two maps have different
    // comparator types, so we cannot unify them behind a single reference.
    auto apply = [&](auto& side) {
        if (qty == 0.0) {
            side.erase(price);
        } else {
            side[price] = qty;
            pruneToMax(side);
        }
    };

    if (is_bid) apply(bids_);
    else        apply(asks_);

    refreshMidCache();
}

// ── getBestBid ─────────────────────────────────────────────────────────────────

double OrderBook::getBestBid() const {
    std::shared_lock lock(mutex_);
    return bids_.empty() ? 0.0 : bids_.begin()->first;
}

// ── getBestAsk ─────────────────────────────────────────────────────────────────

double OrderBook::getBestAsk() const {
    std::shared_lock lock(mutex_);
    return asks_.empty() ? 0.0 : asks_.begin()->first;
}

// ── getMidPrice ────────────────────────────────────────────────────────────────

double OrderBook::getMidPrice() const {
    // Lock-free: the atomic cache is always consistent because refreshMidCache()
    // is called under the exclusive write lock before the lock is released.
    return mid_cache_.load(std::memory_order_acquire);
}
