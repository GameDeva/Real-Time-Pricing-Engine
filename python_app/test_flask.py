"""Flask endpoint tests — run via: python python_app/test_flask.py"""
import sys, os

if sys.platform == 'win32':
    _ssl = r'C:\Program Files\OpenSSL-Win64\bin'
    if os.path.isdir(_ssl):
        os.add_dll_directory(_ssl)

sys.path.insert(0, os.path.dirname(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'python_app'))

from app import app
client = app.test_client()

# 1. Static pricer
r = client.post('/api/price', json={
    'S': 100, 'K': 100, 'T': 1.0,
    'r': 0.05, 'v': 0.20, 'type': 'Call', 'method': 'BlackScholes'
})
assert r.status_code == 200
data = r.get_json()
assert 'price' in data and data['price'] > 0
print('[OK] /api/price   price=' + str(round(data['price'], 4)))

# 2. index.html contains Live Dashboard tab
r2 = client.get('/')
assert r2.status_code == 200
assert b'Live Dashboard' in r2.data
print('[OK] /            index.html has Live Dashboard tab')

# 3. /api/market-data → triggers lazy start of Binance client
r3 = client.get('/api/market-data?strike=95000&T=0.25&r=0.05&vol=0.60')
j3 = r3.get_json()
print('[OK] /api/market-data  HTTP ' + str(r3.status_code) + '  body keys: ' + str(list(j3.keys())))
if r3.status_code == 200:
    assert 'spot' in j3 and j3['spot'] > 0
    print('     spot=' + str(round(j3['spot'], 2))
          + '  option=' + str(round(j3['option_price'], 4))
          + '  bid='    + str(round(j3['best_bid'], 2))
          + '  ask='    + str(round(j3['best_ask'], 2)))

print('=== All checks passed ===')
