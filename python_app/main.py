"""
Phase 5: Quant Pricing Engine — Analytics & Performance Showcase
Requires: quant_pricer.pyd in the same directory (built via CMake Release).
"""

import sys
import os
import time
import math

import numpy as np
import matplotlib
matplotlib.use("Agg")          # headless backend — saves PNGs without a display
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

sys.path.insert(0, os.path.dirname(__file__))
import quant_pricer

# ── Helpers ────────────────────────────────────────────────────────────────────

def make_params(S, K, T, r, v, opt_type):
    return quant_pricer.OptionParams(S=S, K=K, T=T, r=r, v=v, type=opt_type)

BS = quant_pricer.BlackScholesPricer()

# ── Plot 1: BS vs MC price across strikes (with MC error bars) ─────────────────

def plot_strike_sweep(out_path: str):
    strikes    = np.linspace(80, 120, 41)
    S, T, r, v = 100.0, 1.0, 0.05, 0.20
    mc         = quant_pricer.MonteCarloPricer(num_paths=500_000, seed=42)

    bs_calls, bs_puts   = [], []
    mc_calls, mc_puts   = [], []
    mc_call_err, mc_put_err = [], []

    for K in strikes:
        pc = make_params(S, K, T, r, v, quant_pricer.OptionType.Call)
        pp = make_params(S, K, T, r, v, quant_pricer.OptionType.Put)

        bs_calls.append(BS.price(pc).price)
        bs_puts.append(BS.price(pp).price)

        rc = mc.price(pc)
        rp = mc.price(pp)
        mc_calls.append(rc.price);    mc_call_err.append(rc.std_error)
        mc_puts.append(rp.price);     mc_put_err.append(rp.std_error)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle("Black-Scholes vs Monte Carlo — Strike Sweep\n"
                 f"S={S}, T={T}, r={r}, σ={v}, MC paths=500K", fontsize=12)

    for ax, bs_vals, mc_vals, errs, label in [
        (ax1, bs_calls, mc_calls, mc_call_err, "Call"),
        (ax2, bs_puts,  mc_puts,  mc_put_err,  "Put"),
    ]:
        ax.plot(strikes, bs_vals, "b-",  lw=2,   label="Black-Scholes")
        ax.errorbar(strikes, mc_vals, yerr=[2*e for e in errs],
                    fmt="r.", ms=4, elinewidth=0.8, capsize=2, label="MC ±2σ")
        ax.set_xlabel("Strike K")
        ax.set_ylabel("Option Price")
        ax.set_title(f"{label} Price")
        ax.legend()
        ax.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"[Plot 1] Saved: {out_path}")


# ── Plot 2: Delta heatmap over (S, σ) ─────────────────────────────────────────

def plot_delta_heatmap(out_path: str):
    spots = np.linspace(80, 120, 40)
    vols  = np.linspace(0.05, 0.50, 40)
    K, T, r = 100.0, 1.0, 0.05

    call_delta = np.zeros((len(vols), len(spots)))
    put_delta  = np.zeros((len(vols), len(spots)))

    for i, v in enumerate(vols):
        for j, S in enumerate(spots):
            pc = make_params(S, K, T, r, v, quant_pricer.OptionType.Call)
            pp = make_params(S, K, T, r, v, quant_pricer.OptionType.Put)
            call_delta[i, j] = BS.delta(pc)
            put_delta[i, j]  = BS.delta(pp)

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    fig.suptitle(f"BS Delta Surface — K={K}, T={T}, r={r}", fontsize=12)

    for ax, data, title, cmap in [
        (ax1, call_delta, "Call Delta", "Blues"),
        (ax2, put_delta,  "Put Delta",  "Reds_r"),
    ]:
        im = ax.imshow(data, origin="lower", aspect="auto", cmap=cmap,
                       extent=[spots[0], spots[-1], vols[0], vols[-1]])
        ax.set_xlabel("Spot S")
        ax.set_ylabel("Volatility σ")
        ax.set_title(title)
        plt.colorbar(im, ax=ax)

    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"[Plot 2] Saved: {out_path}")


# ── Plot 3: MC convergence vs BS true value ────────────────────────────────────

def plot_mc_convergence(out_path: str):
    path_counts = [int(x) for x in np.logspace(3, 6, 30)]
    S, K, T, r, v = 100.0, 100.0, 1.0, 0.05, 0.20
    p      = make_params(S, K, T, r, v, quant_pricer.OptionType.Call)
    bs_val = BS.price(p).price

    mc_prices, mc_errors = [], []
    for n in path_counts:
        mc  = quant_pricer.MonteCarloPricer(num_paths=n, seed=99)
        res = mc.price(p)
        mc_prices.append(res.price)
        mc_errors.append(res.std_error)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(9, 8), sharex=True)
    fig.suptitle(f"MC Convergence — ATM Call (S=K={S}, T={T}, r={r}, σ={v})", fontsize=12)

    ax1.semilogx(path_counts, mc_prices, "b.-", lw=1.2, ms=5, label="MC price")
    ax1.axhline(bs_val, color="r", lw=1.5, ls="--", label=f"BS true = {bs_val:.4f}")
    ax1.fill_between(path_counts,
                     [p - 2*e for p, e in zip(mc_prices, mc_errors)],
                     [p + 2*e for p, e in zip(mc_prices, mc_errors)],
                     alpha=0.2, color="blue", label="±2σ band")
    ax1.set_ylabel("Option Price")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    ax2.loglog(path_counts, mc_errors, "g.-", lw=1.2, ms=5, label="std_error")
    # Theoretical 1/sqrt(N) reference line
    ref = mc_errors[0] * math.sqrt(path_counts[0])
    ax2.loglog(path_counts, [ref / math.sqrt(n) for n in path_counts],
               "k--", lw=1, label="1/√N reference")
    ax2.set_xlabel("Number of Paths")
    ax2.set_ylabel("Standard Error")
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(out_path, dpi=120)
    plt.close()
    print(f"[Plot 3] Saved: {out_path}")


# ── Timing benchmark ───────────────────────────────────────────────────────────

def run_timing():
    S, K, T, r, v = 100.0, 100.0, 1.0, 0.05, 0.20
    p = make_params(S, K, T, r, v, quant_pricer.OptionType.Call)
    N_BS  = 10_000
    N_MC  = 100

    # Black-Scholes sweep
    t0 = time.perf_counter()
    for _ in range(N_BS):
        BS.price(p)
    bs_wall = time.perf_counter() - t0

    # MC sweep — 100K paths
    mc_100k = quant_pricer.MonteCarloPricer(num_paths=100_000, seed=1)
    t0 = time.perf_counter()
    for _ in range(N_MC):
        mc_100k.price(p)
    mc_100k_wall = time.perf_counter() - t0

    # MC sweep — 1M paths
    mc_1m = quant_pricer.MonteCarloPricer(num_paths=1_000_000, seed=1)
    t0 = time.perf_counter()
    for _ in range(N_MC):
        mc_1m.price(p)
    mc_1m_wall = time.perf_counter() - t0

    print()
    print("=" * 60)
    print("  Performance Timing Summary")
    print("=" * 60)
    print(f"  {'Pricer':<30} {'Calls':>6}  {'Total':>9}  {'Per call':>10}")
    print(f"  {'-'*30}  {'-'*6}  {'-'*9}  {'-'*10}")
    print(f"  {'BlackScholes (price only)':<30} {N_BS:>6}  "
          f"{bs_wall*1e3:>8.2f}ms  {bs_wall/N_BS*1e6:>8.2f}µs")
    print(f"  {'MonteCarlo (100K paths)':<30} {N_MC:>6}  "
          f"{mc_100k_wall*1e3:>8.2f}ms  {mc_100k_wall/N_MC*1e3:>8.2f}ms")
    print(f"  {'MonteCarlo (1M paths)':<30} {N_MC:>6}  "
          f"{mc_1m_wall*1e3:>8.2f}ms  {mc_1m_wall/N_MC*1e3:>8.2f}ms")
    print("=" * 60)
    print()

    # Greeks timing
    greeks = [("delta", BS.delta), ("gamma", BS.gamma),
              ("vega",  BS.vega),  ("theta", BS.theta), ("rho", BS.rho)]
    print("  BS Greeks timing (10K calls each):")
    for name, fn in greeks:
        t0 = time.perf_counter()
        for _ in range(N_BS):
            fn(p)
        elapsed = time.perf_counter() - t0
        print(f"    {name:<8}  {elapsed/N_BS*1e6:>7.2f} µs/call")
    print()


# ── Live dashboard ─────────────────────────────────────────────────────────────

def live_dashboard(strike: float = 95000.0, T: float = 0.25,
                   r: float = 0.05, vol: float = 0.60,
                   symbol: str = "btcusdt"):
    """
    Real-time animated chart: BTC Mid-Price and live BS Call Option price.
    Connects to Binance via C++ WebSocket thread; updates every 100 ms.
    Press Ctrl-C or close the window to stop.
    """
    import sys as _sys
    if _sys.platform == "win32":
        import os as _os
        _ssl = r"C:\Program Files\OpenSSL-Win64\bin"
        if _os.path.isdir(_ssl):
            _os.add_dll_directory(_ssl)

    import matplotlib
    matplotlib.use("TkAgg")          # interactive backend for local use
    import matplotlib.pyplot as _plt
    import matplotlib.animation as _anim
    import collections

    md = quant_pricer.market_data
    client, pricer = md.make_live_engine(symbol)

    print(f"[LiveDashboard] Connecting to Binance ({symbol.upper()})...")
    client.start()

    # Allow the REST snapshot to populate before first render
    import time as _time
    _time.sleep(2)

    MAX_POINTS = 300         # rolling window (~30 s at 100 ms tick)
    spots   = collections.deque(maxlen=MAX_POINTS)
    options = collections.deque(maxlen=MAX_POINTS)

    fig, (ax1, ax2) = _plt.subplots(2, 1, figsize=(12, 7), sharex=False)
    fig.suptitle(
        f"Real-Time BTC Options Pricer  |  K={strike:,.0f}  T={T}y  r={r}  σ={vol}",
        fontsize=13, fontweight="bold")

    line_spot,   = ax1.plot([], [], color="#00c8ff", lw=1.5, label="BTC Mid-Price")
    line_option, = ax2.plot([], [], color="#ff9f43", lw=1.5, label="BS Call Price")

    for ax, ylabel, label in [
        (ax1, "USD", "BTC Spot"),
        (ax2, "USD", f"Call K={strike:,.0f}"),
    ]:
        ax.set_ylabel(ylabel)
        ax.set_title(label)
        ax.legend(loc="upper left", fontsize=9)
        ax.grid(True, alpha=0.2, color="white")
        ax.set_facecolor("#1a1a2e")

    fig.patch.set_facecolor("#0f0f1a")
    for ax in (ax1, ax2):
        ax.tick_params(colors="white")
        ax.yaxis.label.set_color("white")
        ax.title.set_color("white")
        for spine in ax.spines.values():
            spine.set_edgecolor("#444")

    def _animate(_frame):
        try:
            res = pricer.get_live_option_price(strike, T, r, vol, True)
            spots.append(res.spot)
            options.append(res.option_price)
        except Exception:
            return line_spot, line_option

        xs = list(range(len(spots)))
        line_spot.set_data(xs, list(spots))
        line_option.set_data(xs, list(options))
        ax1.relim(); ax1.autoscale_view()
        ax2.relim(); ax2.autoscale_view()
        return line_spot, line_option

    ani = _anim.FuncAnimation(fig, _animate, interval=100,
                              blit=True, cache_frame_data=False)

    try:
        _plt.tight_layout(rect=[0, 0, 1, 0.95])
        _plt.show()
    except KeyboardInterrupt:
        pass
    finally:
        print("[LiveDashboard] Stopping stream...")
        client.stop()
        print("[LiveDashboard] Done.")


# ── Entry point ────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Quant Pricing Engine")
    parser.add_argument("--static", action="store_true",
                        help="Run static analytics plots (original mode)")
    parser.add_argument("--strike", type=float, default=95000.0)
    parser.add_argument("--T",      type=float, default=0.25,
                        help="Time to maturity (years)")
    parser.add_argument("--r",      type=float, default=0.05,
                        help="Risk-free rate")
    parser.add_argument("--vol",    type=float, default=0.60,
                        help="Implied volatility")
    parser.add_argument("--symbol", type=str,   default="btcusdt",
                        help="Binance symbol (lowercase, e.g. ethusdt)")
    args = parser.parse_args()

    out_dir = os.path.dirname(__file__)

    if args.static:
        print("Quant Pricing Engine — Static Analytics")
        print(f"Python {sys.version.split()[0]}, quant_pricer loaded from {out_dir}")
        print()
        print("[1/4] Strike sweep plot...")
        plot_strike_sweep(os.path.join(out_dir, "plot1_strike_sweep.png"))
        print("[2/4] Delta heatmap...")
        plot_delta_heatmap(os.path.join(out_dir, "plot2_delta_heatmap.png"))
        print("[3/4] MC convergence plot...")
        plot_mc_convergence(os.path.join(out_dir, "plot3_mc_convergence.png"))
        print("[4/4] Timing benchmark...")
        run_timing()
        print("Done. PNG files written to:", out_dir)
    else:
        live_dashboard(strike=args.strike, T=args.T,
                       r=args.r, vol=args.vol, symbol=args.symbol)

