#include "market_data/LivePricer.h"

#include <stdexcept>

// ── Constructor ────────────────────────────────────────────────────────────────

LivePricer::LivePricer(std::shared_ptr<IOrderBook> book)
    : book_(std::move(book))
{}

// ── getLiveOptionPrice ─────────────────────────────────────────────────────────

LiveOptionResult LivePricer::getLiveOptionPrice(double strike,
                                                 double time_to_maturity,
                                                 double risk_free_rate,
                                                 double volatility,
                                                 bool   is_call) const {
    // getMidPrice() is lock-free (atomic load) — hot path.
    const double spot = book_->getMidPrice();

    if (spot <= 0.0)
        throw std::runtime_error(
            "No market data available: mid-price is zero. "
            "Has the stream connected and the order book been populated?");

    OptionParams params;
    params.S    = spot;
    params.K    = strike;
    params.T    = time_to_maturity;
    params.r    = risk_free_rate;
    params.v    = volatility;
    params.type = is_call ? OptionType::Call : OptionType::Put;

    validate(params); // throws std::invalid_argument on bad inputs

    const double bid = book_->getBestBid();
    const double ask = book_->getBestAsk();

    return LiveOptionResult{
        .spot         = spot,
        .option_price = bs_.price(params).price,
        .best_bid     = bid,
        .best_ask     = ask,
        .spread       = ask - bid,
    };
}
