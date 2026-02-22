# Quant Pricing Engine

High-performance C++20 options pricing engine with real-time Binance LOB market data, Python bindings, and a live web dashboard.

## Features

- **Black-Scholes Analytical Pricing** — Sub-microsecond latency for European options
- **Monte Carlo Simulation** — Multi-threaded with antithetic variance reduction
- **Full Greeks Suite** — Delta, Gamma, Vega, Theta, Rho
- **Real-Time Limit Order Book** — Binance WebSocket feed (REST snapshot + delta stream), thread-safe, lock-free mid-price cache
- **Live Option Pricing** — LOB mid-price fed as spot price *S* into Black-Scholes in real time
- **Python Bindings** — Pybind11 with GIL-release on network calls
- **Web Interface** — Static pricer + live Chart.js streaming dashboard via SSE
- **Local Dashboard** — `matplotlib.animation` live chart (100 ms ticks)
- **67 C++ unit tests** — GoogleTest, including thread-safety smoke tests

## Performance

| Method | Latency | Throughput |
|---|---|---|
| Black-Scholes | 0.64 µs/call | 1.5 M prices/sec |
| Monte Carlo (100 K paths) | 0.20 ms/call | 5 K prices/sec |
| Monte Carlo (1 M paths) | 1.59 ms/call | 630 prices/sec |
| Greeks | 0.33–0.46 µs/call | 2–3 M/sec |
| LOB mid-price read | O(1) lock-free | — |

## Prerequisites

| Tool | Version |
|---|---|
| C++ compiler | MSVC 2019+, GCC 10+, or Clang 12+ (C++20) |
| CMake | ≥ 3.14 |
| Python | ≥ 3.8 |
| OpenSSL | Windows: [OpenSSL-Win64](https://slproweb.com/products/Win32OpenSSL.html) at `C:\Program Files\OpenSSL-Win64`; Linux: `libssl-dev` (auto-installed by Dockerfile) |

All other C++ dependencies (`nlohmann/json`, `ixwebsocket`, `googletest`, `pybind11`) are fetched automatically by CMake's FetchContent.

## Quick Start

```bash
# Build C++ library & Python bindings
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run C++ tests (67 tests)
ctest --test-dir build --build-config Release
# 100% tests passed, 0 tests failed out of 67

# Install Python dependencies
pip install -r python_app/requirements.txt
```

### Live Local Dashboard (matplotlib)

```bash
python python_app/main.py                        # live BTC chart (default)
python python_app/main.py --symbol v       # different symbol
python python_app/main.py --strike 90000 --vol 0.55
python python_app/main.py --static               # original analytics plots
```

### Web Server

```bash
python -m flask --app python_app/app run --host 0.0.0.0 --port 5000
# Open http://localhost:5000 → Live Dashboard tab → ▶ Connect
```

### Deploy to Railway

Push to Railway — the existing `Dockerfile` and `railway.json` handle everything, including `libssl-dev` for TLS.

## Project Structure

```
quant_pricing_engine/
├── include/
│   ├── OptionParams.h              # Option parameter struct & validation
│   ├── IPricer.h                   # Pricer interface & PricingResult
│   ├── MathUtils.h                 # Normal CDF/PDF
│   ├── BlackScholesPricer.h        # Analytical pricer
│   ├── MonteCarloPricer.h          # MC pricer
│   └── market_data/
│       ├── OrderBook.h             # IOrderBook interface + OrderBook impl
│       ├── MarketDataClient.h      # WebSocket client (Binance)
│       └── LivePricer.h            # LOB → BlackScholes bridge
├── src/
│   ├── MathUtils.cpp
│   ├── BlackScholesPricer.cpp
│   ├── MonteCarloPricer.cpp
│   └── market_data/
│       ├── OrderBook.cpp           # thread-safe LOB, atomic mid-price
│       ├── MarketDataClient.cpp    # REST snapshot + WS delta stream
│       └── LivePricer.cpp          # live option pricing
├── bindings/
│   └── PybindModule.cpp            # Pybind11 — full API + market_data submodule
├── tests/
│   ├── test_pricers.cpp            # 48 pricer/Greeks/MC tests
│   └── test_orderbook.cpp          # 19 LOB tests (thread-safety included)
├── python_app/
│   ├── app.py                      # Flask REST + SSE (/api/live-stream)
│   ├── main.py                     # Live matplotlib dashboard & static plots
│   ├── smoke_test_live.py          # End-to-end Binance connectivity test
│   ├── test_flask.py               # Flask endpoint tests
│   ├── requirements.txt
│   └── static/index.html           # Two-tab dark UI (Chart.js)
├── CMakeLists.txt
├── Dockerfile
└── railway.json
```

## Architecture

```
Binance API
  ├── REST /api/v3/depth  ──► snapshot bootstrap
  └── WSS @depth@100ms   ──► delta stream
           │
           ▼
   MarketDataClient (C++ background thread)
           │  updateLevel()
           ▼
      OrderBook
      ├── bids: std::map (highest first)
      ├── asks: std::map (lowest first)
      └── mid_price: std::atomic<double>  ← lock-free hot path
           │  getMidPrice()
           ▼
       LivePricer ──► BlackScholesPricer ──► LiveOptionResult
           │
     Pybind11 (GIL released on network calls)
     ├── Python API  (get_live_option_price, make_live_engine)
     ├── Flask /api/live-stream  (SSE, 100 ms)
     ├── Flask /api/market-data  (snapshot)
     └── matplotlib.animation   (local dashboard)
```

## Python API

### Static pricing (unchanged)

```python
import quant_pricer

bs = quant_pricer.BlackScholesPricer()
params = quant_pricer.OptionParams(S=100, K=100, T=1.0, r=0.05, v=0.20,
                                    type=quant_pricer.OptionType.Call)
print(bs.price(params).price)   # 10.4506
print(bs.delta(params))         # 0.6368
```

### Live pricing

```python
import os, sys
if sys.platform == 'win32':
    os.add_dll_directory(r'C:\Program Files\OpenSSL-Win64\bin')
sys.path.insert(0, 'python_app')

import quant_pricer
client, pricer = quant_pricer.market_data.make_live_engine('btcusdt')
client.start()                                          # non-blocking

result = pricer.get_live_option_price(
    strike=95000, time_to_maturity=0.25,
    risk_free_rate=0.05, volatility=0.60, is_call=True)

print(result.spot)          # e.g. 67723.13
print(result.option_price)  # e.g. 1672.88
print(result.spread)        # e.g. 0.01

client.stop()
```

## REST API

### Static option pricing

```bash
curl -X POST http://localhost:5000/api/price \
  -H "Content-Type: application/json" \
  -d '{"S":100,"K":100,"T":1,"r":0.05,"v":0.2,"type":"Call","method":"BlackScholes"}'
```

### Live market snapshot

```bash
curl "http://localhost:5000/api/market-data?strike=95000&T=0.25&r=0.05&vol=0.60"
# {"spot":67684.14,"option_price":1666.08,"best_bid":67684.14,"best_ask":67684.15,"spread":0.01,"is_running":true}
```

### SSE live stream (browser / EventSource)

```
GET /api/live-stream?strike=95000&T=0.25&r=0.05&vol=0.60
→ data: {"spot":67685.50,"option_price":1666.22,"best_bid":67685.49,"best_ask":67685.51,"spread":0.02}
→ data: ...  (every 100 ms)
```

## Testing

```bash
# C++ (67 tests)
ctest --test-dir build --build-config Release --output-on-failure

# Flask endpoints + live Binance connectivity
python python_app/test_flask.py
python python_app/smoke_test_live.py
```

| Suite | Count | Coverage |
|---|---|---|
| MathUtils | 9 | norm_cdf/pdf values, symmetry |
| OptionParams validation | 8 | boundaries, zero/negative inputs |
| MonteCarlo | 8 | convergence, determinism, put-call parity |
| Black-Scholes + Greeks | 23 | known values, numerical finite-diff |
| **OrderBook** | **19** | empty book, insert/remove, level cap (top-20), atomic mid-price cache, 4-writer/4-reader thread-safety |

## Technical Notes

- **LOB concurrency:** `std::shared_mutex` for readers-writer lock; `std::atomic<double>` for the mid-price cache so `getMidPrice()` is lock-free on the hot path.
- **Binance protocol:** REST snapshot bootstraps the book; stale WS deltas (`lastUpdateId` ≤ snapshot) are discarded. Top-20 levels only, capped to bound map size.
- **TLS:** ixwebsocket uses OpenSSL backend (`USE_OPEN_SSL=1`, `USE_ZLIB=0`). Binance's `depth@100ms` stream does not require per-message deflate.
- **Windows note:** Add `os.add_dll_directory(r'C:\Program Files\OpenSSL-Win64\bin')` before importing `quant_pricer` in Python scripts. `app.py` and `main.py` do this automatically.
- **Railway / Docker:** The Dockerfile builder stage installs `libssl-dev` — no extra configuration needed.

## License

MIT License — see LICENSE file for details.
