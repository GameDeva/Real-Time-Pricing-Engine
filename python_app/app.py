"""
Flask Web Server for Quant Pricing Engine
Exposes REST API endpoints for option pricing and Greeks calculation.
"""

import sys
import os
sys.path.insert(0, os.path.dirname(__file__))

from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
import quant_pricer

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

if __name__ == '__main__':
    import os
    port = int(os.environ.get('PORT', 5000))
    print("=" * 60)
    print("  Quant Pricing Engine — Web Interface")
    print("=" * 60)
    print(f"  Server starting at http://0.0.0.0:{port}")
    print("  Open your browser and navigate to the URL above")
    print("=" * 60)
    app.run(debug=False, host='0.0.0.0', port=port)
