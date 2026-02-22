import sys
import os
import math

sys.path.insert(0, os.path.dirname(__file__))

import quant_pricer

# ── Canonical fixtures ─────────────────────────────────────────────────────────

CALL = quant_pricer.OptionParams(S=100.0, K=100.0, T=1.0, r=0.05, v=0.2,
                                  type=quant_pricer.OptionType.Call)
PUT  = quant_pricer.OptionParams(S=100.0, K=100.0, T=1.0, r=0.05, v=0.2,
                                  type=quant_pricer.OptionType.Put)

# ── OptionParams ───────────────────────────────────────────────────────────────

def test_option_params_fields():
    p = quant_pricer.OptionParams(S=100.0, K=105.0, T=0.5, r=0.03, v=0.25,
                                   type=quant_pricer.OptionType.Put)
    assert p.S    == 100.0
    assert p.K    == 105.0
    assert p.T    == 0.5
    assert p.r    == 0.03
    assert p.v    == 0.25
    assert p.type == quant_pricer.OptionType.Put

def test_option_params_mutable():
    p = quant_pricer.OptionParams(S=100.0, K=100.0, T=1.0, r=0.05, v=0.2,
                                   type=quant_pricer.OptionType.Call)
    p.S = 110.0
    assert p.S == 110.0

def test_option_params_repr():
    assert "OptionParams" in repr(CALL)

# ── PricingResult ──────────────────────────────────────────────────────────────

def test_pricing_result_fields_exist():
    bs = quant_pricer.BlackScholesPricer()
    r  = bs.price(CALL)
    assert hasattr(r, "price")
    assert hasattr(r, "std_error")
    assert isinstance(r.price,     float)
    assert isinstance(r.std_error, float)

def test_pricing_result_repr():
    bs = quant_pricer.BlackScholesPricer()
    assert "PricingResult" in repr(bs.price(CALL))

def test_pricing_result_is_readonly():
    bs = quant_pricer.BlackScholesPricer()
    r  = bs.price(CALL)
    try:
        r.price = 0.0
        assert False, "Should have raised AttributeError"
    except AttributeError:
        pass

# ── BlackScholesPricer: prices ─────────────────────────────────────────────────

def test_bs_call_canonical():
    assert abs(quant_pricer.BlackScholesPricer().price(CALL).price - 10.4506) < 1e-3

def test_bs_put_canonical():
    assert abs(quant_pricer.BlackScholesPricer().price(PUT).price - 5.5735) < 1e-3

def test_bs_std_error_is_zero():
    bs = quant_pricer.BlackScholesPricer()
    assert bs.price(CALL).std_error == 0.0
    assert bs.price(PUT).std_error  == 0.0

def test_bs_put_call_parity():
    bs  = quant_pricer.BlackScholesPricer()
    lhs = bs.price(CALL).price - bs.price(PUT).price
    rhs = CALL.S - CALL.K * math.exp(-CALL.r * CALL.T)
    assert abs(lhs - rhs) < 1e-9

def test_bs_price_nonnegative():
    bs = quant_pricer.BlackScholesPricer()
    for S in (80.0, 100.0, 120.0):
        for t in (quant_pricer.OptionType.Call, quant_pricer.OptionType.Put):
            p = quant_pricer.OptionParams(S=S, K=100.0, T=1.0, r=0.05, v=0.2, type=t)
            assert bs.price(p).price >= 0.0

# ── BlackScholesPricer: Greeks ─────────────────────────────────────────────────

def test_bs_greeks_return_floats():
    bs = quant_pricer.BlackScholesPricer()
    for fn in (bs.delta, bs.gamma, bs.vega, bs.theta, bs.rho):
        assert isinstance(fn(CALL), float)

def test_bs_call_delta_in_bounds():
    d = quant_pricer.BlackScholesPricer().delta(CALL)
    assert 0.0 < d < 1.0

def test_bs_put_delta_in_bounds():
    d = quant_pricer.BlackScholesPricer().delta(PUT)
    assert -1.0 < d < 0.0

def test_bs_put_call_delta_parity():
    bs = quant_pricer.BlackScholesPricer()
    assert abs(bs.delta(CALL) - bs.delta(PUT) - 1.0) < 1e-12

def test_bs_gamma_positive():
    assert quant_pricer.BlackScholesPricer().gamma(CALL) > 0.0

def test_bs_vega_positive():
    assert quant_pricer.BlackScholesPricer().vega(CALL) > 0.0

def test_bs_theta_call_negative():
    assert quant_pricer.BlackScholesPricer().theta(CALL) < 0.0

def test_bs_rho_call_positive():
    assert quant_pricer.BlackScholesPricer().rho(CALL) > 0.0

def test_bs_rho_put_negative():
    assert quant_pricer.BlackScholesPricer().rho(PUT) < 0.0

# ── MonteCarloPricer ───────────────────────────────────────────────────────────

def test_mc_deterministic_seed():
    mc1 = quant_pricer.MonteCarloPricer(num_paths=100000, seed=42)
    mc2 = quant_pricer.MonteCarloPricer(num_paths=100000, seed=42)
    assert mc1.price(CALL).price == mc2.price(CALL).price

def test_mc_no_seed_runs():
    r = quant_pricer.MonteCarloPricer(num_paths=10000).price(CALL)
    assert r.price     >= 0.0
    assert r.std_error >= 0.0

def test_mc_std_error_positive():
    r = quant_pricer.MonteCarloPricer(num_paths=10000, seed=1).price(CALL)
    assert r.std_error > 0.0

def test_mc_converges_to_bs():
    bs = quant_pricer.BlackScholesPricer()
    mc = quant_pricer.MonteCarloPricer(num_paths=1000000, seed=7)
    assert abs(mc.price(CALL).price - bs.price(CALL).price) < 0.10

def test_mc_num_paths_accessor():
    mc = quant_pricer.MonteCarloPricer(num_paths=50000, seed=0)
    assert mc.num_paths() == 50000
