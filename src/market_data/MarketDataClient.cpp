#include "market_data/MarketDataClient.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

// ixwebsocket headers — kept inside the .cpp (pimpl) so they don't leak into
// anything that only includes MarketDataClient.h.
#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── Pimpl body ─────────────────────────────────────────────────────────────────

struct MarketDataClient::Impl {
    ix::WebSocket ws;
};

// ── Helpers ────────────────────────────────────────────────────────────────────

namespace {

// Convert uppercase symbol → REST depth URL.
std::string depthRestUrl(const std::string& upper) {
    return "https://api.binance.com/api/v3/depth?symbol=" + upper + "&limit=20";
}

// Convert lowercase symbol → WebSocket stream URL.
std::string depthWsUrl(const std::string& lower) {
    return "wss://stream.binance.com:9443/ws/" + lower + "@depth@100ms";
}

// Parse a Binance price/qty pair ["price_str", "qty_str"] → {price, qty}.
std::pair<double, double> parseLevel(const json& entry) {
    return {std::stod(entry[0].get<std::string>()),
            std::stod(entry[1].get<std::string>())};
}

} // namespace

// ── Constructor / Destructor ───────────────────────────────────────────────────

MarketDataClient::MarketDataClient(std::shared_ptr<OrderBook> book,
                                   std::string symbol)
    : book_(std::move(book))
    , symbol_(symbol)
    , impl_(std::make_unique<Impl>())
{
    // Normalise: internal WSS/REST expect lower and upper respectively.
    symbolUpper_ = symbol_;
    std::transform(symbolUpper_.begin(), symbolUpper_.end(),
                   symbolUpper_.begin(),
                   [](unsigned char c) { return static_cast<char>(::toupper(c)); });

    // Ensure Winsock (Windows) or socket layer (Linux) is initialised.
    ix::initNetSystem();
}

MarketDataClient::~MarketDataClient() {
    stop();
    ix::uninitNetSystem();
}

// ── fetchSnapshot ──────────────────────────────────────────────────────────────

long long MarketDataClient::fetchSnapshot() {
    ix::HttpClient http;
    ix::HttpRequestArgsPtr args = http.createRequest();
    args->connectTimeout = 10;
    args->transferTimeout = 10;

    auto response = http.get(depthRestUrl(symbolUpper_), args);

    if (response->statusCode != 200) {
        throw std::runtime_error(
            "Binance depth snapshot failed: HTTP " +
            std::to_string(response->statusCode));
    }

    auto j = json::parse(response->body);

    long long lastUpdateId = j.value("lastUpdateId", 0LL);

    // Populate the OrderBook with the snapshot data.
    for (auto& entry : j["bids"])
        book_->updateLevel(parseLevel(entry).first,
                           parseLevel(entry).second, /*is_bid=*/true);
    for (auto& entry : j["asks"])
        book_->updateLevel(parseLevel(entry).first,
                           parseLevel(entry).second, /*is_bid=*/false);

    std::cout << "[MarketDataClient] Snapshot loaded for " << symbolUpper_
              << " — lastUpdateId=" << lastUpdateId
              << "  bid=" << book_->getBestBid()
              << "  ask=" << book_->getBestAsk() << "\n";

    return lastUpdateId;
}

// ── applyDelta ─────────────────────────────────────────────────────────────────

void MarketDataClient::applyDelta(const std::string& msg, long long snapshotId) {
    // Guard against malformed JSON arriving during reconnects.
    json j;
    try {
        j = json::parse(msg);
    } catch (...) {
        return;
    }

    // "U" = first update id, "u" = last update id in this event.
    // Discard events that were already included in the snapshot.
    long long lastId = j.value("u", 0LL);
    if (lastId <= snapshotId) return;

    // Not all depthUpdate frames have an "e" field; accept messages with bids/asks.
    if (!j.contains("b") || !j.contains("a")) return;

    for (auto& entry : j["b"])  // bids
        book_->updateLevel(parseLevel(entry).first,
                           parseLevel(entry).second, /*is_bid=*/true);
    for (auto& entry : j["a"])  // asks
        book_->updateLevel(parseLevel(entry).first,
                           parseLevel(entry).second, /*is_bid=*/false);
}

// ── runSession ─────────────────────────────────────────────────────────────────

bool MarketDataClient::runSession() {
    // Step 1: REST snapshot — populates the book and records the baseline id.
    long long snapshotId = 0;
    try {
        snapshotId = fetchSnapshot();
    } catch (const std::exception& e) {
        std::cerr << "[MarketDataClient] Snapshot error: " << e.what() << "\n";
        return false;
    }

    // Step 2: Open the WebSocket delta stream.
    impl_->ws.setUrl(depthWsUrl(symbol_));
    impl_->ws.disableAutomaticReconnection(); // We handle reconnect ourselves.

    // Atomic flag used to signal session end from the message callback.
    std::atomic<bool> sessionDone{false};

    impl_->ws.setOnMessageCallback(
        [this, snapshotId, &sessionDone](const ix::WebSocketMessagePtr& msg) {
            switch (msg->type) {
            case ix::WebSocketMessageType::Message:
                applyDelta(msg->str, snapshotId);
                break;

            case ix::WebSocketMessageType::Open:
                std::cout << "[MarketDataClient] WebSocket connected: "
                          << depthWsUrl(symbol_) << "\n";
                break;

            case ix::WebSocketMessageType::Error:
                std::cerr << "[MarketDataClient] WebSocket error: "
                          << msg->errorInfo.reason << "\n";
                sessionDone.store(true, std::memory_order_release);
                break;

            case ix::WebSocketMessageType::Close:
                std::cout << "[MarketDataClient] WebSocket closed ("
                          << msg->closeInfo.code << ")\n";
                sessionDone.store(true, std::memory_order_release);
                break;

            default:
                break;
            }
        });

    impl_->ws.start();

    // Wait until either the user calls stop() or the session drops.
    while (!stopRequested_.load(std::memory_order_acquire) &&
           !sessionDone.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    impl_->ws.stop();
    return !sessionDone.load(std::memory_order_acquire); // true = clean shutdown
}

// ── start ──────────────────────────────────────────────────────────────────────

void MarketDataClient::start() {
    // No-op if already running.
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel)) {
        return;
    }

    stopRequested_.store(false, std::memory_order_release);

    // Spawn a detached management thread: handles retry loop.
    std::thread([this]() {
        int retries = 0;
        while (!stopRequested_.load(std::memory_order_acquire)) {
            bool cleanShutdown = runSession();

            if (cleanShutdown || stopRequested_.load(std::memory_order_acquire))
                break;

            ++retries;
            if (retries > kMaxRetries) {
                std::cerr << "[MarketDataClient] Exceeded max retries ("
                          << kMaxRetries << "). Giving up.\n";
                break;
            }

            double backoff = kRetryBackoffSec * retries;
            std::cerr << "[MarketDataClient] Reconnecting in "
                      << backoff << "s (attempt " << retries << "/"
                      << kMaxRetries << ")...\n";
            std::this_thread::sleep_for(
                std::chrono::duration<double>(backoff));
        }

        running_.store(false, std::memory_order_release);
    }).detach();
}

// ── stop ───────────────────────────────────────────────────────────────────────

void MarketDataClient::stop() {
    // No-op if already stopped.
    if (!running_.load(std::memory_order_acquire)) return;

    stopRequested_.store(true, std::memory_order_release);
    // Unblock the WebSocket receive loop immediately.
    try { impl_->ws.stop(); } catch (...) {}

    // Spin-wait for the management thread to set running_ = false (< 200ms).
    for (int i = 0; i < 200 && running_.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
