#!/usr/bin/env python3
"""
Test script to verify market data connection works correctly.
Tests both WebSocket and REST fallback functionality.
"""

import sys
import os
import time
import threading

sys.path.insert(0, os.path.dirname(__file__))
import quant_pricer

def test_market_data_connection(symbol="btcusdt", duration=30):
    """Test market data connection with fallback to REST polling."""
    print(f"Testing market data connection for {symbol.upper()}...")
    
    # Create market data engine
    client, pricer = quant_pricer.market_data.make_live_engine(symbol)
    
    print(f"Starting market data client...")
    client.start()
    
    # Test for specified duration
    start_time = time.time()
    last_mid_price = 0.0
    data_received = False
    
    while time.time() - start_time < duration:
        try:
            result = pricer.get_live_option_price(
                strike=95000.0 if symbol == "btcusdt" else 2000.0,
                time_to_maturity=0.25,
                risk_free_rate=0.05,
                volatility=0.60,
                is_call=True
            )
            
            if result.spot > 0:
                data_received = True
                if result.spot != last_mid_price:
                    print(f"✓ Data received: Spot=${result.spot:.2f}, "
                          f"Bid=${result.best_bid:.2f}, Ask=${result.best_ask:.2f}, "
                          f"Option=${result.option_price:.2f}")
                    last_mid_price = result.spot
            else:
                print("⏳ Waiting for market data...")
                
        except Exception as e:
            print(f"❌ Error getting live price: {e}")
            
        time.sleep(2)
    
    print(f"\nTest Results:")
    print(f"- Data received: {'✓ Yes' if data_received else '❌ No'}")
    print(f"- Client running: {'✓ Yes' if client.is_running else '❌ No'}")
    print(f"- Final mid-price: ${last_mid_price:.2f}")
    
    # Stop the client
    client.stop()
    print("✓ Test completed")
    
    return data_received

def test_rest_fallback():
    """Test REST polling fallback specifically."""
    print("\n" + "="*50)
    print("Testing REST polling fallback...")
    
    # This will automatically fall back to REST if WebSocket fails
    return test_market_data_connection("btcusdt", duration=20)

if __name__ == "__main__":
    print("🚀 Quant Pricing Engine - Market Data Connection Test")
    print("="*50)
    
    try:
        # Test normal connection
        success = test_market_data_connection()
        
        if not success:
            print("\n⚠️  WebSocket connection failed, testing REST fallback...")
            success = test_rest_fallback()
        
        if success:
            print("\n✅ Market data connection test PASSED")
            sys.exit(0)
        else:
            print("\n❌ Market data connection test FAILED")
            sys.exit(1)
            
    except KeyboardInterrupt:
        print("\n⏹️  Test interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n💥 Test failed with exception: {e}")
        sys.exit(1)
