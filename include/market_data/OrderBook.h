#pragma once

#include <atomic>
#include <map>
#include <shared_mutex>

// ── IOrderBook ─────────────────────────────────────────────────────────────────
// Abstract interface for the Limit Order Book. Decouples LivePricer and bindings
// from the concrete implementation, allowing a future flat-array (DOD) swap-in
// without touching any other layer.

class IOrderBook {
public:
    virtual ~IOrderBook() = default;

    // Insert or update a price level. qty == 0.0 removes the level entirely.
    virtual void   updateLevel(double price, double qty, bool is_bid) = 0;

    // Best bid (highest), best ask (lowest), or 0.0 if the side is empty.
    virtual double getBestBid()  const = 0;
    virtual double getBestAsk()  const = 0;

    // (bestBid + bestAsk) / 2, or 0.0 if either side is empty.
    // Hot-path: implemented with a lock-free atomic cache in OrderBook.
    virtual double getMidPrice() const = 0;
};

// ── OrderBook ──────────────────────────────────────────────────────────────────
// std::map-based LOB with std::shared_mutex (readers-writer lock) and an
// atomic<double> mid-price cache for lock-free reads on the pricing hot-path.
//
// Thread-safety contract:
//   • updateLevel()  acquires an exclusive write lock, then atomically stores
//                    the new mid-price. O(log N) per call.
//   • getBestBid/Ask acquire a shared read lock.             O(log N).
//   • getMidPrice()  is fully lock-free via atomic load.     O(1).
//
// Only the top kMaxLevels price levels are retained on each side to bound
// memory and map-traversal cost.

class OrderBook : public IOrderBook {
public:
    static constexpr int kMaxLevels = 20;

    void   updateLevel(double price, double qty, bool is_bid) override;
    double getBestBid()  const override;
    double getBestAsk()  const override;
    double getMidPrice() const override;

private:
    // Prune map to at most kMaxLevels levels. Called inside the write lock.
    template<typename Map>
    static void pruneToMax(Map& m);

    // Recompute and atomically store mid-price. Called inside the write lock.
    void refreshMidCache();

    mutable std::shared_mutex mutex_;

    // Bids: highest price first.
    std::map<double, double, std::greater<double>> bids_;
    // Asks: lowest price first.
    std::map<double, double, std::less<double>>    asks_;

    // Lock-free cache updated after every write. Avoids shared_mutex overhead
    // on the pricing hot-path.
    std::atomic<double> mid_cache_{0.0};
};
