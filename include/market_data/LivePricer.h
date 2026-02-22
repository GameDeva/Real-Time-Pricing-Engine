#pragma once

#include <memory>
#include <stdexcept>

#include "market_data/OrderBook.h"
#include "BlackScholesPricer.h"
#include "OptionParams.h"

// ── LiveOptionResult ───────────────────────────────────────────────────────────
// Snapshot returned by getLiveOptionPrice(). Bundles the spot feed values
// alongside the derived BS price so callers never need to query the LOB twice.

struct LiveOptionResult {
    double spot;         // mid-price used as S in Black-Scholes
    double option_price; // Black-Scholes fair value
    double best_bid;     // best bid in the order book
    double best_ask;     // best ask in the order book
    double spread;       // best_ask - best_bid (convenience)
};

// ── LivePricer ─────────────────────────────────────────────────────────────────
// Bridges the order book with the Black-Scholes analytical pricer.
// Holds a shared reference to any IOrderBook implementation so the concrete
// OrderBook can later be swapped for a flat-array DOD variant transparently.
//
// Thread-safety: getLiveOptionPrice() is fully re-entrant. It reads
// getMidPrice() (lock-free atomic) and getBestBid/Ask() (shared_lock),
// then calls the stateless BlackScholesPricer. Multiple Python threads
// can call this simultaneously without any additional locking.

class LivePricer {
public:
    explicit LivePricer(std::shared_ptr<IOrderBook> book);

    // Compute a live Black-Scholes option price using the current mid-price as S.
    // Throws std::runtime_error("No market data available") if mid-price == 0.
    LiveOptionResult getLiveOptionPrice(double strike,
                                        double time_to_maturity,
                                        double risk_free_rate,
                                        double volatility,
                                        bool   is_call = true) const;

private:
    std::shared_ptr<IOrderBook> book_;
    BlackScholesPricer          bs_;   // stateless — safe to share / copy
};
