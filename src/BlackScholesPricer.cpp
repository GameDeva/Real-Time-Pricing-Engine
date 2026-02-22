#include "BlackScholesPricer.h"
#include "MathUtils.h"

#include <cmath>
#include <algorithm>

namespace {

// Threshold below which vol*sqrt(T) is treated as zero (degenerate case).
constexpr double kVstEps = 1e-10;

struct D1D2 { double d1, d2; };

D1D2 compute_d1_d2(const OptionParams& p, double vst) {
    double d1 = (std::log(p.S / p.K) + (p.r + 0.5 * p.v * p.v) * p.T) / vst;
    return {d1, d1 - vst};
}

// Discounted intrinsic value for the v=0 / T=0 degenerate case.
PricingResult degenerate_price(const OptionParams& p) {
    const double discount  = std::exp(-p.r * p.T);
    const double forward   = p.S * std::exp(p.r * p.T);
    if (p.type == OptionType::Call)
        return {std::max(0.0, (forward - p.K) * discount), 0.0};
    else
        return {std::max(0.0, (p.K - forward) * discount), 0.0};
}

} // namespace

// ── price ─────────────────────────────────────────────────────────────────────

PricingResult BlackScholesPricer::price(const OptionParams& p) const {
    validate(p);
    const double vst = p.v * std::sqrt(p.T);
    if (vst < kVstEps) return degenerate_price(p);

    auto [d1, d2]      = compute_d1_d2(p, vst);
    const double disc  = std::exp(-p.r * p.T);
    const double Kdisc = p.K * disc;

    if (p.type == OptionType::Call)
        return {p.S * MathUtils::norm_cdf(d1) - Kdisc * MathUtils::norm_cdf(d2), 0.0};
    else
        return {Kdisc * MathUtils::norm_cdf(-d2) - p.S * MathUtils::norm_cdf(-d1), 0.0};
}

// ── delta ─────────────────────────────────────────────────────────────────────

double BlackScholesPricer::delta(const OptionParams& p) const {
    validate(p);
    const double vst = p.v * std::sqrt(p.T);
    if (vst < kVstEps) {
        const double fwd = p.S * std::exp(p.r * p.T);
        if (p.type == OptionType::Call) return fwd > p.K ? 1.0 : 0.0;
        else                            return fwd < p.K ? -1.0 : 0.0;
    }
    const double d1 = compute_d1_d2(p, vst).d1;
    return p.type == OptionType::Call ? MathUtils::norm_cdf(d1)
                                      : MathUtils::norm_cdf(d1) - 1.0;
}

// ── gamma ─────────────────────────────────────────────────────────────────────

double BlackScholesPricer::gamma(const OptionParams& p) const {
    validate(p);
    const double vst = p.v * std::sqrt(p.T);
    if (vst < kVstEps) return 0.0;
    const double d1 = compute_d1_d2(p, vst).d1;
    return MathUtils::norm_pdf(d1) / (p.S * vst);
}

// ── vega ──────────────────────────────────────────────────────────────────────

double BlackScholesPricer::vega(const OptionParams& p) const {
    validate(p);
    const double vst = p.v * std::sqrt(p.T);
    if (vst < kVstEps) return 0.0;
    const double d1 = compute_d1_d2(p, vst).d1;
    return p.S * MathUtils::norm_pdf(d1) * std::sqrt(p.T);
}

// ── theta (per year) ──────────────────────────────────────────────────────────

double BlackScholesPricer::theta(const OptionParams& p) const {
    validate(p);
    const double vst = p.v * std::sqrt(p.T);
    if (vst < kVstEps) return 0.0;

    auto [d1, d2]     = compute_d1_d2(p, vst);
    const double disc = std::exp(-p.r * p.T);
    const double decay = -(p.S * MathUtils::norm_pdf(d1) * p.v) / (2.0 * std::sqrt(p.T));

    if (p.type == OptionType::Call)
        return decay - p.r * p.K * disc * MathUtils::norm_cdf(d2);
    else
        return decay + p.r * p.K * disc * MathUtils::norm_cdf(-d2);
}

// ── rho ───────────────────────────────────────────────────────────────────────

double BlackScholesPricer::rho(const OptionParams& p) const {
    validate(p);
    if (p.T < kVstEps) return 0.0;

    const double vst  = p.v * std::sqrt(p.T);
    const double disc = std::exp(-p.r * p.T);
    const double KTd  = p.K * p.T * disc;

    if (vst < kVstEps) {
        const double fwd = p.S * std::exp(p.r * p.T);
        if (p.type == OptionType::Call) return fwd > p.K ?  KTd : 0.0;
        else                            return fwd < p.K ? -KTd : 0.0;
    }
    auto [d1, d2] = compute_d1_d2(p, vst);
    return p.type == OptionType::Call ?  KTd * MathUtils::norm_cdf(d2)
                                      : -KTd * MathUtils::norm_cdf(-d2);
}
