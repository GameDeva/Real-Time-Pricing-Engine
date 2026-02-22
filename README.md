# Quant Pricing Engine

High-performance C++20 options pricing engine with Python bindings and web interface.

## Features

- **Black-Scholes Analytical Pricing** — Sub-microsecond latency for European options
- **Monte Carlo Simulation** — Multi-threaded with antithetic variance reduction
- **Full Greeks Suite** — Delta, Gamma, Vega, Theta, Rho
- **Python Bindings** — Zero-copy Pybind11 integration
- **Web Interface** — Interactive Flask-based pricing dashboard
- **Comprehensive Testing** — 48 C++ unit tests + 25 Python tests

## Performance

| Method | Latency | Throughput |
|--------|---------|------------|
| Black-Scholes | 0.64 µs/call | 1.5M prices/sec |
| Monte Carlo (100K paths) | 0.20 ms/call | 5K prices/sec |
| Monte Carlo (1M paths) | 1.59 ms/call | 630 prices/sec |
| Greeks | 0.33-0.46 µs/call | 2-3M/sec |

## Quick Start

### Prerequisites

- **C++ Compiler:** MSVC 2019+, GCC 10+, or Clang 12+ with C++20 support
- **CMake:** 3.14 or higher
- **Python:** 3.8+ with pip

### Build

```bash
# Clone repository
git clone <your-repo-url>
cd quant_pricing_engine

# Build C++ library and Python bindings
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run C++ tests
ctest --test-dir build --build-config Release
# Output: 48/48 tests passed

# Install Python dependencies
cd python_app
pip install -r requirements.txt

# Run Python tests
pytest test_python_bindings.py -v
# Output: 25/25 tests passed
```

### Run Web Interface

```bash
cd python_app
python app.py
# Open browser to http://localhost:5000
```

### Run Analytics Demo

```bash
cd python_app
python main.py
# Generates 3 PNG plots + timing table
```

## Project Structure

```
quant_pricing_engine/
├── include/                    # C++ headers
│   ├── OptionParams.h         # Option parameters & validation
│   ├── IPricer.h              # Pricer interface
│   ├── MathUtils.h            # Normal CDF/PDF
│   ├── BlackScholesPricer.h   # Analytical pricer
│   └── MonteCarloPricer.h     # MC pricer
├── src/                       # C++ implementations
│   ├── MathUtils.cpp
│   ├── BlackScholesPricer.cpp
│   └── MonteCarloPricer.cpp
├── bindings/                  # Python bindings
│   └── PybindModule.cpp
├── tests/                     # C++ unit tests
│   └── test_pricers.cpp
├── python_app/                # Python interface
│   ├── app.py                 # Flask web server
│   ├── main.py                # Analytics demo
│   ├── test_python_bindings.py
│   └── static/
│       └── index.html         # Web UI
└── CMakeLists.txt
```

## Architecture

### C++ Core

**Black-Scholes Pricer:**
- Closed-form solution using `std::erfc` for numerical stability
- Degenerate case handling (v=0, T=0)
- All 5 first-order Greeks with finite-difference validation

**Monte Carlo Pricer:**
- Multi-threaded via `std::async` (scales to hardware threads)
- Antithetic variates for 50% variance reduction
- Per-thread RNG (no locks, no shared mutable state)
- Bessel-corrected standard error estimation

### Python Bindings

Pybind11 exposes:
- `OptionType` enum (Call/Put)
- `OptionParams` struct (mutable fields)
- `PricingResult` struct (read-only)
- `BlackScholesPricer` (price + 5 Greeks)
- `MonteCarloPricer` (price with optional seed)

### Web Interface

- **Backend:** Flask REST API (`/api/price`, `/api/greeks`)
- **Frontend:** Vanilla JS + CSS Grid (no frameworks)
- **Features:** Real-time pricing, Greeks visualization, MC convergence

## Testing

### C++ Tests (GoogleTest)

```bash
ctest --test-dir build --build-config Release --output-on-failure
```

**Coverage:**
- MathUtils: norm_cdf/pdf validation
- OptionParams: input validation
- BlackScholes: known values, put-call parity, Greeks numerical validation
- MonteCarlo: determinism, convergence, std_error properties

### Python Tests (Pytest)

```bash
cd python_app
pytest test_python_bindings.py -v
```

**Coverage:**
- Bindings correctness (field access, method calls)
- BS pricing accuracy
- Greeks bounds and parity relationships
- MC determinism and convergence

## API Reference

### Python

```python
import quant_pricer

# Create option parameters
params = quant_pricer.OptionParams(
    S=100.0,  # Spot price
    K=100.0,  # Strike price
    T=1.0,    # Time to maturity (years)
    r=0.05,   # Risk-free rate
    v=0.20,   # Volatility
    type=quant_pricer.OptionType.Call
)

# Black-Scholes pricing
bs = quant_pricer.BlackScholesPricer()
result = bs.price(params)
print(f"Price: ${result.price:.4f}")
print(f"Delta: {bs.delta(params):.4f}")

# Monte Carlo pricing
mc = quant_pricer.MonteCarloPricer(num_paths=1_000_000, seed=42)
result = mc.price(params)
print(f"Price: ${result.price:.4f} ± {result.std_error:.4f}")
```

### REST API

**Price an option:**
```bash
curl -X POST http://localhost:5000/api/price \
  -H "Content-Type: application/json" \
  -d '{
    "S": 100, "K": 100, "T": 1, "r": 0.05, "v": 0.2,
    "type": "Call",
    "method": "BlackScholes"
  }'
```

**Response:**
```json
{
  "price": 10.4506,
  "std_error": 0.0,
  "greeks": {
    "delta": 0.6368,
    "gamma": 0.0193,
    "vega": 38.60,
    "theta": -6.41,
    "rho": 53.23
  },
  "method": "Black-Scholes"
}
```

## Technical Highlights

1. **Modern C++20** — `std::numbers`, structured bindings, `std::optional`
2. **Lock-free parallelism** — Each worker owns its RNG, single sync point
3. **Variance reduction** — Antithetic variates halve MC error
4. **Numerical stability** — Degenerate guards, Bessel correction, guarded sqrt
5. **Zero-copy bindings** — Pybind11 passes C++ objects by reference
6. **Production-ready** — 73 automated tests, comprehensive error handling

## License

MIT License - see LICENSE file for details

## Author

Built as a demonstration of quantitative finance systems programming.
