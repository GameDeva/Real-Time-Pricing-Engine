"""
Flask Web Server for Quant Pricing Engine
Exposes REST API endpoints for option pricing and Greeks calculation.
"""

import sys
import os

# Windows: ixwebsocket links OpenSSL dynamically — make the DLLs discoverable.
# On Linux/Railway this block is a no-op (OpenSSL is on the standard ld path).
if sys.platform == 'win32':
    _ssl_bin = r'C:\Program Files\OpenSSL-Win64\bin'
    if os.path.isdir(_ssl_bin):
        os.add_dll_directory(_ssl_bin)

# Resolve quant_pricer.so in all environments:
#   Railway  — PYTHONPATH=/app/build is set in Dockerfile ENV (already on path)
#   Local    — build/ is one level above python_app/
#   Fallback — python_app/ itself (legacy)
_here      = os.path.dirname(os.path.abspath(__file__))
_proj_root = os.path.dirname(_here)
_build_dir = os.path.join(_proj_root, 'build')
for _p in [_build_dir, _here]:
    if _p not in sys.path:
        sys.path.insert(0, _p)

from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
try:
    import quant_pricer
except ImportError as _e:
    raise ImportError(
        f"Cannot import quant_pricer: {_e}\n"
        "Run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
    ) from _e

app = Flask(__name__, static_folder='static')
CORS(app)

# Instantiate pricers once at startup
bs_pricer = quant_pricer.BlackScholesPricer()

@app.route('/')
def index():
    return send_from_directory('static', 'index.html')

@app.route('/api/price', methods=['POST'])
def price_option():
    """
    POST /api/price
    Body: {
        "S": 100.0,
        "K": 100.0,
        "T": 1.0,
        "r": 0.05,
        "v": 0.2,
        "type": "Call",
        "method": "BlackScholes",
        "mc_paths": 100000,
        "mc_seed": 42
    }
    Returns: {
        "price": 10.4506,
        "std_error": 0.0,
        "greeks": {...}  // only for BlackScholes
    }
    """
    try:
        data = request.get_json()
        
        # Parse option type
        opt_type = quant_pricer.OptionType.Call if data['type'] == 'Call' else quant_pricer.OptionType.Put
        
        # Create OptionParams
        params = quant_pricer.OptionParams(
            S=float(data['S']),
            K=float(data['K']),
            T=float(data['T']),
            r=float(data['r']),
            v=float(data['v']),
            type=opt_type
        )
        
        method = data.get('method', 'BlackScholes')
        
        if method == 'BlackScholes':
            result = bs_pricer.price(params)
            greeks = {
                'delta': bs_pricer.delta(params),
                'gamma': bs_pricer.gamma(params),
                'vega': bs_pricer.vega(params),
                'theta': bs_pricer.theta(params),
                'rho': bs_pricer.rho(params)
            }
            return jsonify({
                'price': result.price,
                'std_error': result.std_error,
                'greeks': greeks,
                'method': 'Black-Scholes'
            })
        
        elif method == 'MonteCarlo':
            mc_paths = int(data.get('mc_paths', 100000))
            mc_seed = data.get('mc_seed', None)
            if mc_seed is not None:
                mc_seed = int(mc_seed)
            
            mc_pricer = quant_pricer.MonteCarloPricer(num_paths=mc_paths, seed=mc_seed)
            result = mc_pricer.price(params)
            
            return jsonify({
                'price': result.price,
                'std_error': result.std_error,
                'mc_paths': mc_paths,
                'method': 'Monte Carlo'
            })
        
        else:
            return jsonify({'error': f'Unknown method: {method}'}), 400
            
    except Exception as e:
        return jsonify({'error': str(e)}), 400

@app.route('/api/greeks', methods=['POST'])
def calculate_greeks():
    """
    POST /api/greeks
    Body: {
        "S": 100.0,
        "K": 100.0,
        "T": 1.0,
        "r": 0.05,
        "v": 0.2,
        "type": "Call"
    }
    Returns: {
        "delta": 0.6368,
        "gamma": 0.0193,
        "vega": 38.6,
        "theta": -6.41,
        "rho": 53.23
    }
    """
    try:
        data = request.get_json()
        opt_type = quant_pricer.OptionType.Call if data['type'] == 'Call' else quant_pricer.OptionType.Put
        
        params = quant_pricer.OptionParams(
            S=float(data['S']),
            K=float(data['K']),
            T=float(data['T']),
            r=float(data['r']),
            v=float(data['v']),
            type=opt_type
        )
        
        return jsonify({
            'delta': bs_pricer.delta(params),
            'gamma': bs_pricer.gamma(params),
            'vega': bs_pricer.vega(params),
            'theta': bs_pricer.theta(params),
            'rho': bs_pricer.rho(params)
        })
        
    except Exception as e:
        return jsonify({'error': str(e)}), 400

# ── Real-time market data (LOB + Live Pricer) ──────────────────────────────────

import json
import time
import threading
import requests as _requests
from flask import Response, stream_with_context

# Module-level singleton keyed by symbol.
# Switching symbol stops the old engine and boots a new one.
_live_engine_lock = threading.Lock()
_live_client = None
_live_pricer  = None
_live_symbol  = None


def _fetch_rest_ticker(symbol: str) -> dict:
    """
    Python REST fallback — fetches best bid/ask directly from Binance using
    Python's requests library (separate TLS stack from C++ ixwebsocket).
    Used when the C++ LOB engine cannot populate (TLS cert issue, IP block, etc.)
    """
    url = f"https://api.binance.com/api/v3/ticker/bookTicker?symbol={symbol.upper()}"
    r = _requests.get(url, timeout=8)
    r.raise_for_status()
    d = r.json()
    bid = float(d['bidPrice'])
    ask = float(d['askPrice'])
    return {'bid': bid, 'ask': ask, 'mid': (bid + ask) / 2.0, 'spread': ask - bid}


@app.route('/api/health')
def health():
    """
    GET /api/health  — connectivity diagnostics.
    Returns status of Python requests → Binance and C++ LOB engine.
    Check this on Railway to diagnose market data failures.
    """
    result = {'python_requests': {}, 'cpp_engine': {}}

    # 1. Test Python → Binance REST
    try:
        t0 = time.time()
        ticker = _fetch_rest_ticker('btcusdt')
        result['python_requests'] = {
            'ok': True, 'latency_ms': round((time.time() - t0) * 1000, 1),
            'bid': ticker['bid'], 'ask': ticker['ask']
        }
    except Exception as e:
        result['python_requests'] = {'ok': False, 'error': str(e)}

    # 2. Test C++ engine
    try:
        c, p = _get_or_start_engine('btcusdt')
        spot = p.get_live_option_price(1.0, 1.0, 0.05, 0.3, True).spot
        result['cpp_engine'] = {'ok': spot > 0, 'spot': spot}
    except Exception as e:
        result['cpp_engine'] = {'ok': False, 'error': str(e)}

    result['overall'] = result['python_requests'].get('ok') or result['cpp_engine'].get('ok')
    return jsonify(result)


def _get_or_start_engine(symbol: str = "btcusdt"):
    """
    Return the running engine for `symbol`.
    If a different symbol is requested, the current engine is stopped and a
    new one is started — so changing the symbol in the browser takes effect
    on the next SSE connection.
    """
    global _live_client, _live_pricer, _live_symbol
    with _live_engine_lock:
        symbol_changed = (_live_symbol != symbol)
        engine_dead    = (_live_client is None or not _live_client.is_running)

        if symbol_changed or engine_dead:
            # Stop current engine if it exists
            if _live_client is not None:
                try:
                    _live_client.stop()
                except Exception:
                    pass
            _live_client, _live_pricer = quant_pricer.market_data.make_live_engine(symbol)
            _live_symbol = symbol
            _live_client.start()

            # Poll until the order book is populated (up to 15 s).
            # On Railway/cloud the Binance REST snapshot can take 3-8 s.
            deadline = time.time() + 15
            while time.time() < deadline:
                try:
                    r = _live_pricer.get_live_option_price(1.0, 1.0, 0.05, 0.3, True)
                    if r.spot > 0:
                        break
                except Exception:
                    pass  # still loading — keep waiting
                time.sleep(0.25)

    return _live_client, _live_pricer



@app.route('/api/market-data')
def market_data_snapshot():
    """
    GET /api/market-data?symbol=btcusdt&strike=95000&T=0.25&r=0.05&vol=0.60
    Returns a JSON snapshot of the current order-book state.
    """
    try:
        symbol = request.args.get('symbol', 'btcusdt').lower()
        client, pricer = _get_or_start_engine(symbol)
        result = pricer.get_live_option_price(
            strike=float(request.args.get('strike', 95000)),
            time_to_maturity=float(request.args.get('T', 0.25)),
            risk_free_rate=float(request.args.get('r', 0.05)),
            volatility=float(request.args.get('vol', 0.60)),
            is_call=True,
        )
        return jsonify({
            'spot':         result.spot,
            'option_price': result.option_price,
            'best_bid':     result.best_bid,
            'best_ask':     result.best_ask,
            'spread':       result.spread,
            'is_running':   client.is_running,
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 503


@app.route('/api/live-stream')
def live_stream():
    """
    GET /api/live-stream?strike=95000&T=0.25&r=0.05&vol=0.60
    Server-Sent Events stream. Browser connects once; JSON frames push every 100 ms.
    Compatible with EventSource API — no browser WebSocket needed.
    """
    strike = float(request.args.get('strike', 95000))
    T      = float(request.args.get('T', 0.25))
    r      = float(request.args.get('r', 0.05))
    vol    = float(request.args.get('vol', 0.60))
    symbol = request.args.get('symbol', 'btcusdt').lower()

    def generate():
        # Capture pricer locally — avoids reading the global mid-stream
        # if another thread resets it for a symbol change.
        _client, pricer = _get_or_start_engine(symbol)

        # SSE keepalive comment: forces Werkzeug to flush its write buffer
        # so the browser knows the connection is alive before the first data frame.
        yield ": keepalive\n\n"

        # Track consecutive zero-spot ticks to trigger REST fallback
        import math as _math
        _zero_streak = 0

        while True:
            try:
                t0 = time.perf_counter()
                result = pricer.get_live_option_price(strike, T, r, vol, True)
                bs_us  = (time.perf_counter() - t0) * 1_000_000

                if result.spot > 0:
                    # C++ LOB is working — happy path
                    _zero_streak = 0
                    payload = json.dumps({
                        'spot':         round(result.spot, 2),
                        'option_price': round(result.option_price, 4),
                        'best_bid':     round(result.best_bid, 2),
                        'best_ask':     round(result.best_ask, 2),
                        'spread':       round(result.spread, 4),
                        'bs_us':        round(bs_us, 2),
                        'server_ts':    time.time(),
                        'source':       'lob',
                    })
                    yield f"data: {payload}\n\n"
                    time.sleep(0.1)
                    continue

                # result.spot == 0: order book not populated yet
                _zero_streak += 1

            except GeneratorExit:
                return  # browser disconnected

            except Exception:
                _zero_streak += 1

            # After 3 consecutive zero/error ticks, try Python REST fallback
            if _zero_streak >= 3:
                try:
                    t0     = time.perf_counter()
                    ticker = _fetch_rest_ticker(symbol)
                    bs_us  = (time.perf_counter() - t0) * 1_000_000
                    spot   = ticker['mid']
                    bid    = ticker['bid']
                    ask    = ticker['ask']
                    spread = ticker['spread']

                    # Inline Black-Scholes (no scipy dependency)
                    def _ncdf(x):
                        return (1.0 + _math.erf(x / _math.sqrt(2.0))) / 2.0
                    if T > 0 and vol > 0 and spot > 0 and strike > 0:
                        d1  = (_math.log(spot / strike) + (r + 0.5 * vol**2) * T) / (vol * _math.sqrt(T))
                        d2  = d1 - vol * _math.sqrt(T)
                        opt = spot * _ncdf(d1) - strike * _math.exp(-r * T) * _ncdf(d2)
                    else:
                        opt = 0.0

                    payload = json.dumps({
                        'spot':         round(spot, 2),
                        'option_price': round(opt, 4),
                        'best_bid':     round(bid, 2),
                        'best_ask':     round(ask, 2),
                        'spread':       round(spread, 4),
                        'bs_us':        round(bs_us, 2),
                        'server_ts':    time.time(),
                        'source':       'rest',
                    })
                    yield f"data: {payload}\n\n"
                    time.sleep(1.0)  # REST fallback runs at 1 Hz
                    # Don't reset _zero_streak — keep using REST until C++ recovers
                    continue

                except GeneratorExit:
                    return

                except Exception:
                    yield 'data: {"status":"connecting","msg":"Waiting for market data..."}\n\n'
                    time.sleep(1.0)
                    continue

            # C++ returned zero but < 3 ticks — hold briefly before retry
            yield 'data: {"status":"connecting","msg":"Waiting for order book..."}\n\n'
            time.sleep(0.5)


    return Response(
        stream_with_context(generate()),
        mimetype='text/event-stream',
        headers={
            'Cache-Control': 'no-cache',
            'X-Accel-Buffering': 'no',   # disable Nginx/Railway proxy buffering
        }
    )


if __name__ == '__main__':
    port = int(os.environ.get('PORT', 5000))
    print("=" * 60)
    print("  Quant Pricing Engine — Web Interface")
    print("=" * 60)
    print(f"  Server starting at http://0.0.0.0:{port}")
    print("  Open your browser and navigate to the URL above")
    print("=" * 60)
    app.run(debug=False, host='0.0.0.0', port=port, threaded=True)

