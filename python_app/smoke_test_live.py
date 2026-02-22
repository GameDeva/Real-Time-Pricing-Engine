"""
Smoke test: verifies the full pipeline from Python to Binance real-time data.
Run from the quant_pricing_engine root:
    python python_app/smoke_test_live.py
"""
import sys, os, time

# Windows: ixwebsocket links OpenSSL dynamically — add bin dir to DLL search path
if sys.platform == 'win32':
    _ssl_bin = r'C:\Program Files\OpenSSL-Win64\bin'
    if os.path.isdir(_ssl_bin):
        os.add_dll_directory(_ssl_bin)

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
import quant_pricer

md = quant_pricer.market_data

# 1. Submodule symbols
print("=== Smoke Test: market_data bindings ===")
print("Symbols:", [s for s in dir(md) if not s.startswith("_")])

# 2. OrderBook from Python
book = md.OrderBook()
book.update_level(95000.0, 1.5, True)
book.update_level(95100.0, 0.8, False)
assert book.mid_price == (95000.0 + 95100.0) / 2.0, "Mid-price mismatch"
print(f"[OK] OrderBook: bid={book.best_bid}  ask={book.best_ask}  mid={book.mid_price}")

# 3. make_live_engine
client, pricer = md.make_live_engine("btcusdt")
print(f"[OK] make_live_engine: symbol={client.symbol}  running={client.is_running}")

# 4. Start and wait for snapshot
print("Starting Binance stream (waiting 5s for snapshot)...")
client.start()
time.sleep(5)

mid = book.mid_price   # NOTE: make_live_engine uses its own internal book
                        # not the hand-crafted one above; check via pricer.
print(f"     client.is_running={client.is_running}")

# 5. Price a live BTC call option
try:
    r = pricer.get_live_option_price(
        strike=95000.0,
        time_to_maturity=0.25,
        risk_free_rate=0.05,
        volatility=0.60,
        is_call=True,
    )
    print(f"[OK] LiveOptionResult: {r}")
    assert r.spot > 0,          "Spot must be > 0"
    assert r.option_price > 0,  "Option price must be > 0"
    assert r.best_bid > 0,      "Best bid must be > 0"
    assert r.best_ask >= r.best_bid, "Ask must be >= Bid"
    print("=== PASS ===")
except Exception as e:
    print(f"[FAIL] {e}")
    sys.exit(1)
finally:
    client.stop()
    print("Stream stopped.")
