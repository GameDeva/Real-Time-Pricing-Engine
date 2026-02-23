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

// ── Pimpl body
// ─────────────────────────────────────────────────────────────────

struct MarketDataClient::Impl {
  ix::WebSocket ws;
};

// ── Helpers
// ────────────────────────────────────────────────────────────────────

namespace {

// Convert uppercase symbol → REST depth URL.
std::string depthRestUrl(const std::string &symbol) {
  std::string upper = symbol;
  std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
  return "https://data-api.binance.vision/api/v3/depth?symbol=" + upper +
         "&limit=20";
}

std::string depthWsUrl(const std::string &symbol) {
  std::string lower = symbol;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return "wss://stream.data-api.binance.vision:9443/ws/" + lower +
         "@depth@100ms";
}

// Parse a Binance price/qty pair ["price_str", "qty_str"] → {price, qty}.
std::pair<double, double> parseLevel(const json &entry) {
  return {std::stod(entry[0].get<std::string>()),
          std::stod(entry[1].get<std::string>())};
}

} // namespace

// ── Constructor / Destructor
// ───────────────────────────────────────────────────

MarketDataClient::MarketDataClient(std::shared_ptr<OrderBook> book,
                                   std::string symbol)
    : book_(std::move(book)), symbol_(symbol), impl_(std::make_unique<Impl>()) {
  // Normalise: internal WSS/REST expect lower and upper respectively.
  symbolUpper_ = symbol_;
  std::transform(
      symbolUpper_.begin(), symbolUpper_.end(), symbolUpper_.begin(),
      [](unsigned char c) { return static_cast<char>(::toupper(c)); });

  // Ensure Winsock (Windows) or socket layer (Linux) is initialised.
  ix::initNetSystem();
}

MarketDataClient::~MarketDataClient() {
  stop();
  ix::uninitNetSystem();
}

// ── fetchSnapshot
// ──────────────────────────────────────────────────────────────

long long MarketDataClient::fetchSnapshot() {
  ix::HttpClient http;
  ix::HttpRequestArgsPtr args = http.createRequest();

  // Railway-specific: increased timeouts for cloud environment
  args->connectTimeout = 30;  // Increased from 10 to 30 seconds
  args->transferTimeout = 30; // Increased from 10 to 30 seconds

  // Set user agent to avoid potential blocking
  args->extraHeaders["User-Agent"] = "QuantPricingEngine/1.0";

  std::cout << "[MarketDataClient] Fetching snapshot from: "
            << depthRestUrl(symbolUpper_) << "\n";

  auto response = http.get(depthRestUrl(symbolUpper_), args);

  std::cout << "[MarketDataClient] HTTP response status: "
            << response->statusCode << "\n";
  if (response->statusCode != 200) {
    std::cerr << "[MarketDataClient] Response body: " << response->body << "\n";
    throw std::runtime_error("Binance depth snapshot failed: HTTP " +
                             std::to_string(response->statusCode));
  }

  auto j = json::parse(response->body);

  long long lastUpdateId = j.value("lastUpdateId", 0LL);

  // Populate the OrderBook with the snapshot data.
  for (auto &entry : j["bids"])
    book_->updateLevel(parseLevel(entry).first, parseLevel(entry).second,
                       /*is_bid=*/true);
  for (auto &entry : j["asks"])
    book_->updateLevel(parseLevel(entry).first, parseLevel(entry).second,
                       /*is_bid=*/false);

  std::cout << "[MarketDataClient] Snapshot loaded for " << symbolUpper_
            << " — lastUpdateId=" << lastUpdateId
            << "  bid=" << book_->getBestBid()
            << "  ask=" << book_->getBestAsk() << "\n";

  return lastUpdateId;
}

// ── applyDelta
// ─────────────────────────────────────────────────────────────────

void MarketDataClient::applyDelta(const std::string &msg,
                                  long long snapshotId) {
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
  if (lastId <= snapshotId)
    return;

  // Not all depthUpdate frames have an "e" field; accept messages with
  // bids/asks.
  if (!j.contains("b") || !j.contains("a"))
    return;

  for (auto &entry : j["b"]) // bids
    book_->updateLevel(parseLevel(entry).first, parseLevel(entry).second,
                       /*is_bid=*/true);
  for (auto &entry : j["a"]) // asks
    book_->updateLevel(parseLevel(entry).first, parseLevel(entry).second,
                       /*is_bid=*/false);
}

// ── REST polling fallback
// ───────────────────────────────────────────────────────

bool MarketDataClient::runRestPolling() {
  std::cout
      << "[MarketDataClient] Starting REST polling mode (WebSocket fallback)\n";

  while (!stopRequested_.load(std::memory_order_acquire)) {
    try {
      // Fetch fresh snapshot every 2 seconds
      fetchSnapshot();
    } catch (const std::exception &e) {
      std::cerr << "[MarketDataClient] REST polling error: " << e.what()
                << "\n";
      // Don't give up immediately on REST errors, just continue
      std::this_thread::sleep_for(std::chrono::seconds(5));
      continue;
    }

    // Sleep for 2 seconds between updates
    for (int i = 0; i < 20 && !stopRequested_.load(std::memory_order_acquire);
         ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  std::cout << "[MarketDataClient] REST polling stopped\n";
  return true;
}

// ── runSession
// ─────────────────────────────────────────────────────────────────

bool MarketDataClient::runSession() {
  // Step 1: REST snapshot — populates the book and records the baseline id.
  long long snapshotId = 0;
  try {
    snapshotId = fetchSnapshot();
  } catch (const std::exception &e) {
    std::cerr << "[MarketDataClient] Snapshot error: " << e.what() << "\n";
    return false;
  }

  // Step 2: Open the WebSocket delta stream.
  std::string wsUrl = depthWsUrl(symbol_);
  std::cout << "[MarketDataClient] Connecting WebSocket to: " << wsUrl << "\n";
  impl_->ws.setUrl(wsUrl);
  impl_->ws.disableAutomaticReconnection(); // We handle reconnect ourselves.

  // Railway-specific: increased WebSocket timeouts
  impl_->ws.setPingInterval(
      30); // Ping every 30 seconds to keep the connection alive
  impl_->ws.disablePerMessageDeflate(); // Binance depth stream doesn't need
                                        // compression

  // Atomic flag used to signal session end from the message callback.
  std::atomic<bool> sessionDone{false};

  impl_->ws.setOnMessageCallback(
      [this, snapshotId, &sessionDone](const ix::WebSocketMessagePtr &msg) {
        switch (msg->type) {
        case ix::WebSocketMessageType::Message:
          applyDelta(msg->str, snapshotId);
          break;

        case ix::WebSocketMessageType::Open:
          std::cout << "[MarketDataClient] WebSocket connected successfully\n";
          break;

        case ix::WebSocketMessageType::Error:
          std::cerr << "[MarketDataClient] WebSocket error: "
                    << msg->errorInfo.reason << " (HTTP "
                    << msg->errorInfo.http_status << ")\n";
          sessionDone.store(true, std::memory_order_release);
          break;

        case ix::WebSocketMessageType::Close:
          std::cout << "[MarketDataClient] WebSocket closed ("
                    << msg->closeInfo.code
                    << ") - reason: " << msg->closeInfo.reason << "\n";
          sessionDone.store(true, std::memory_order_release);
          break;

        default:
          break;
        }
      });

  impl_->ws.start();

  // Wait for WebSocket connection with timeout
  std::cout << "[MarketDataClient] Waiting for WebSocket connection...\n";
  auto startTime = std::chrono::steady_clock::now();
  const auto connectionTimeout =
      std::chrono::seconds(30); // Increased from 15 to 30 seconds

  bool connected = false;
  while (!sessionDone.load(std::memory_order_acquire) && !connected &&
         (std::chrono::steady_clock::now() - startTime) < connectionTimeout) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    connected = (impl_->ws.getReadyState() == ix::ReadyState::Open);
  }

  if (!connected && !sessionDone.load(std::memory_order_acquire)) {
    std::cerr
        << "[MarketDataClient] WebSocket connection timeout after 30 seconds\n";
    impl_->ws.stop();
    return false;
  }

  std::cout << "[MarketDataClient] WebSocket session started successfully\n";

  // Wait until either the user calls stop() or the session drops.
  while (!stopRequested_.load(std::memory_order_acquire) &&
         !sessionDone.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  impl_->ws.stop();
  return !sessionDone.load(std::memory_order_acquire); // true = clean shutdown
}

// ── start
// ──────────────────────────────────────────────────────────────────────

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
    int wsFailures = 0;
    const int maxWsFailures = 3; // Fall back to REST after 3 WebSocket failures

    while (!stopRequested_.load(std::memory_order_acquire)) {
      bool cleanShutdown = runSession();

      if (cleanShutdown || stopRequested_.load(std::memory_order_acquire))
        break;

      wsFailures++;

      // After multiple WebSocket failures, fall back to REST polling
      if (wsFailures >= maxWsFailures) {
        std::cout << "[MarketDataClient] WebSocket failed " << wsFailures
                  << " times, falling back to REST polling\n";
        runRestPolling();
        break;
      }

      ++retries;
      if (retries > kMaxRetries) {
        std::cerr << "[MarketDataClient] Exceeded max retries (" << kMaxRetries
                  << "). Falling back to REST polling.\n";
        runRestPolling();
        break;
      }

      double backoff = kRetryBackoffSec * retries;
      std::cerr << "[MarketDataClient] Reconnecting in " << backoff
                << "s (attempt " << retries << "/" << kMaxRetries << ")...\n";
      std::this_thread::sleep_for(std::chrono::duration<double>(backoff));
    }

    running_.store(false, std::memory_order_release);
  }).detach();
}

// ── stop
// ───────────────────────────────────────────────────────────────────────

void MarketDataClient::stop() {
  // No-op if already stopped.
  if (!running_.load(std::memory_order_acquire))
    return;

  stopRequested_.store(true, std::memory_order_release);
  // Unblock the WebSocket receive loop immediately.
  try {
    impl_->ws.stop();
  } catch (...) {
  }

  // Spin-wait for the management thread to set running_ = false (< 200ms).
  for (int i = 0; i < 200 && running_.load(std::memory_order_acquire); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
