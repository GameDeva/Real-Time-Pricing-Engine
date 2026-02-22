#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "market_data/OrderBook.h"

// ── MarketDataClient ───────────────────────────────────────────────────────────
// Connects to the Binance partial-depth WebSocket stream for a given symbol and
// feeds level updates into an OrderBook.
//
// Protocol (Binance depthUpdate):
//   1. On connect:  GET /api/v3/depth?symbol=<SYMBOL>&limit=20  (REST snapshot)
//      → populate the OrderBook with the full initial state.
//   2. Subscribe:   wss://stream.binance.com:9443/ws/<symbol>@depth@100ms
//      → receive delta frames; discard any with lastUpdateId <= snapshot id.
//   3. On each delta: call book_->updateLevel() for every bid/ask entry.
//   4. On disconnect: reconnect with exponential back-off (up to kMaxRetries).
//
// Thread model:
//   start() is non-blocking — it spawns ixwebsocket's internal thread.
//   stop()  signals the thread to exit and joins it synchronously.
//   All OrderBook writes happen on that background thread; reads from any thread
//   are safe due to OrderBook's shared_mutex / atomic design.

class MarketDataClient {
public:
    static constexpr int    kMaxRetries     = 5;
    static constexpr double kRetryBackoffSec = 1.0;

    // symbol should be lowercase, e.g. "btcusdt".
    explicit MarketDataClient(std::shared_ptr<OrderBook> book,
                              std::string symbol = "btcusdt");
    ~MarketDataClient();

    // Starts the background WebSocket thread (non-blocking).
    // Safe to call once; subsequent calls are no-ops if already running.
    void start();

    // Signals the WebSocket thread to stop and blocks until it exits.
    void stop();

    bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

    // Symbol this client is subscribed to (for introspection / bindings).
    const std::string& symbol() const noexcept { return symbol_; }

private:
    // Boot the REST snapshot then open the WebSocket stream.
    // Returns true on a clean shutdown, false on unrecoverable error.
    bool runSession();

    // Fetch the depth snapshot from Binance REST and populate the OrderBook.
    // Returns the lastUpdateId from the snapshot (used to filter stale deltas).
    long long fetchSnapshot();

    // Parse one depthUpdate message and apply to the OrderBook.
    void applyDelta(const std::string& msg, long long snapshotId);

    std::shared_ptr<OrderBook> book_;
    std::string                symbol_;      // lowercase, e.g. "btcusdt"
    std::string                symbolUpper_; // uppercase, e.g. "BTCUSDT"
    std::atomic<bool>          running_{false};
    std::atomic<bool>          stopRequested_{false};

    // Pimpl: keeps ixwebsocket headers away from code that only includes this header.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
