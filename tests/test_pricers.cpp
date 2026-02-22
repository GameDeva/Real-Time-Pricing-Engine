#include <gtest/gtest.h>
#include <cmath>
#include <stdexcept>

#include "MathUtils.h"
#include "OptionParams.h"
#include "BlackScholesPricer.h"
#include "MonteCarloPricer.h"

// ── MathUtils: norm_cdf ────────────────────────────────────────────────────────

TEST(NormCdfTest, AtZeroIsHalf) {
    EXPECT_NEAR(MathUtils::norm_cdf(0.0), 0.5, 1e-12);
}

TEST(NormCdfTest, LargePositiveApproachesOne) {
    EXPECT_NEAR(MathUtils::norm_cdf(8.0), 1.0, 1e-10);
}

TEST(NormCdfTest, LargeNegativeApproachesZero) {
    EXPECT_NEAR(MathUtils::norm_cdf(-8.0), 0.0, 1e-10);
}

TEST(NormCdfTest, Symmetry) {
    // Phi(x) + Phi(-x) == 1 for all x
    for (double x : {-3.0, -1.5, -0.5, 0.5, 1.5, 3.0}) {
        EXPECT_NEAR(MathUtils::norm_cdf(x) + MathUtils::norm_cdf(-x), 1.0, 1e-12)
            << "Symmetry failed at x = " << x;
    }
}

TEST(NormCdfTest, KnownValues) {
    // Phi(1.0) ≈ 0.841345, Phi(-1.0) ≈ 0.158655
    EXPECT_NEAR(MathUtils::norm_cdf(1.0),  0.8413447460685429, 1e-9);
    EXPECT_NEAR(MathUtils::norm_cdf(-1.0), 0.1586552539314571, 1e-9);
    // Phi(1.96) ≈ 0.9750021 (95% two-tail boundary)
    EXPECT_NEAR(MathUtils::norm_cdf(1.96), 0.9750021048517795, 1e-7);
}

// ── MathUtils: norm_pdf ────────────────────────────────────────────────────────

TEST(NormPdfTest, AtZeroIsMaximum) {
    // phi(0) = 1 / sqrt(2 * pi) ≈ 0.398942
    EXPECT_NEAR(MathUtils::norm_pdf(0.0), 0.3989422804014327, 1e-10);
}

TEST(NormPdfTest, IsSymmetric) {
    for (double x : {0.5, 1.0, 2.0, 3.0}) {
        EXPECT_NEAR(MathUtils::norm_pdf(x), MathUtils::norm_pdf(-x), 1e-14)
            << "PDF symmetry failed at x = " << x;
    }
}

TEST(NormPdfTest, IsNonNegative) {
    for (double x : {-5.0, -2.0, 0.0, 2.0, 5.0}) {
        EXPECT_GE(MathUtils::norm_pdf(x), 0.0);
    }
}

TEST(NormPdfTest, KnownValue) {
    // phi(1.0) = exp(-0.5) / sqrt(2*pi) ≈ 0.241971
    EXPECT_NEAR(MathUtils::norm_pdf(1.0), 0.24197072451914337, 1e-10);
}

// ── OptionParams: validate() ───────────────────────────────────────────────────

TEST(ValidateTest, ValidParamsNoThrow) {
    OptionParams p{100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    EXPECT_NO_THROW(validate(p));
}

TEST(ValidateTest, ZeroSpotThrows) {
    OptionParams p{0.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    EXPECT_THROW(validate(p), std::invalid_argument);
}

TEST(ValidateTest, NegativeSpotThrows) {
    OptionParams p{-50.0, 100.0, 1.0, 0.05, 0.2, OptionType::Put};
    EXPECT_THROW(validate(p), std::invalid_argument);
}

TEST(ValidateTest, ZeroStrikeThrows) {
    OptionParams p{100.0, 0.0, 1.0, 0.05, 0.2, OptionType::Call};
    EXPECT_THROW(validate(p), std::invalid_argument);
}

TEST(ValidateTest, NegativeMaturityThrows) {
    OptionParams p{100.0, 100.0, -0.1, 0.05, 0.2, OptionType::Call};
    EXPECT_THROW(validate(p), std::invalid_argument);
}

TEST(ValidateTest, ZeroMaturityIsValid) {
    // T=0 is a degenerate but valid edge case (payoff equals intrinsic value)
    OptionParams p{100.0, 100.0, 0.0, 0.05, 0.2, OptionType::Call};
    EXPECT_NO_THROW(validate(p));
}

TEST(ValidateTest, NegativeVolatilityThrows) {
    OptionParams p{100.0, 100.0, 1.0, 0.05, -0.1, OptionType::Call};
    EXPECT_THROW(validate(p), std::invalid_argument);
}

TEST(ValidateTest, ZeroVolatilityIsValid) {
    // sigma=0 is valid: option price collapses to discounted intrinsic value
    OptionParams p{100.0, 100.0, 1.0, 0.05, 0.0, OptionType::Call};
    EXPECT_NO_THROW(validate(p));
}

// ── MonteCarloPricer ───────────────────────────────────────────────────────────

TEST(MCPriceTest, PriceIsNonNegative) {
    MonteCarloPricer mc(10000, 42u);
    OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    EXPECT_GE(mc.price(p).price, 0.0);
}

TEST(MCPriceTest, StdErrorIsPositive) {
    MonteCarloPricer mc(10000, 42u);
    OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    EXPECT_GT(mc.price(p).std_error, 0.0);
}

TEST(MCPriceTest, DeterministicSeed) {
    OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    MonteCarloPricer mc1(100000, 12345u);
    MonteCarloPricer mc2(100000, 12345u);
    EXPECT_DOUBLE_EQ(mc1.price(p).price,     mc2.price(p).price);
    EXPECT_DOUBLE_EQ(mc1.price(p).std_error, mc2.price(p).std_error);
}

TEST(MCPriceTest, StdErrorDecreasesWithMorePaths) {
    OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    MonteCarloPricer mc_small(1000,    99u);
    MonteCarloPricer mc_large(1000000, 99u);
    EXPECT_LT(mc_large.price(p).std_error, mc_small.price(p).std_error);
}

TEST(MCPriceTest, ConvergesCallToBlackScholes) {
    // 1M paths with antithetic variates should be well within 0.10 of BS
    const OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    const BlackScholesPricer bs;
    const MonteCarloPricer   mc(1000000, 7u);
    EXPECT_NEAR(mc.price(p).price, bs.price(p).price, 0.10);
}

TEST(MCPriceTest, ConvergesPutToBlackScholes) {
    OptionParams p = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Put};
    const BlackScholesPricer bs;
    const MonteCarloPricer   mc(1000000, 7u);
    EXPECT_NEAR(mc.price(p).price, bs.price(p).price, 0.10);
}

TEST(MCPriceTest, PutCallParityHolds) {
    // C - P ≈ S - K*exp(-rT) within a generous MC tolerance
    const OptionParams pc = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
    const OptionParams pp = {100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Put};
    const MonteCarloPricer mc(1000000, 7u);
    double lhs = mc.price(pc).price - mc.price(pp).price;
    double rhs = pc.S - pc.K * std::exp(-pc.r * pc.T);
    EXPECT_NEAR(lhs, rhs, 0.05);
}

TEST(MCPriceTest, StressNoNaN) {
    // Run 20 sequential pricings on different params — must never produce NaN/inf
    MonteCarloPricer mc(50000, 0u);
    for (double S : {80.0, 100.0, 120.0}) {
        for (double K : {90.0, 100.0, 110.0}) {
            for (OptionType t : {OptionType::Call, OptionType::Put}) {
                OptionParams p{S, K, 1.0, 0.05, 0.2, t};
                auto r = mc.price(p);
                EXPECT_FALSE(std::isnan(r.price))     << "NaN price at S=" << S << " K=" << K;
                EXPECT_FALSE(std::isinf(r.price))     << "Inf price at S=" << S << " K=" << K;
                EXPECT_FALSE(std::isnan(r.std_error)) << "NaN std_error at S=" << S << " K=" << K;
                EXPECT_GE(r.price,     0.0);
                EXPECT_GE(r.std_error, 0.0);
            }
        }
    }
}

// ── BlackScholesPricer helpers ─────────────────────────────────────────────────

namespace {
const BlackScholesPricer bs;
// Canonical ATM params: S=100, K=100, T=1, r=0.05, v=0.2
const OptionParams kAtm{100.0, 100.0, 1.0, 0.05, 0.2, OptionType::Call};
} // namespace

// ── Price: known values ────────────────────────────────────────────────────────

TEST(BSPriceTest, CanonicalCallValue) {
    OptionParams p = kAtm;
    EXPECT_NEAR(bs.price(p).price, 10.4506, 1e-3);
    EXPECT_DOUBLE_EQ(bs.price(p).std_error, 0.0);
}

TEST(BSPriceTest, CanonicalPutValue) {
    OptionParams p = kAtm;
    p.type = OptionType::Put;
    EXPECT_NEAR(bs.price(p).price, 5.5735, 1e-3);
}

TEST(BSPriceTest, PutCallParity) {
    // C - P = S - K * exp(-r*T)
    for (double K : {80.0, 90.0, 100.0, 110.0, 120.0}) {
        OptionParams pc = kAtm; pc.K = K; pc.type = OptionType::Call;
        OptionParams pp = kAtm; pp.K = K; pp.type = OptionType::Put;
        double lhs = bs.price(pc).price - bs.price(pp).price;
        double rhs = pc.S - K * std::exp(-pc.r * pc.T);
        EXPECT_NEAR(lhs, rhs, 1e-9) << "Put-call parity failed at K=" << K;
    }
}

TEST(BSPriceTest, ZeroMaturityIntrinsicCall) {
    OptionParams p{110.0, 100.0, 0.0, 0.05, 0.2, OptionType::Call};
    EXPECT_NEAR(bs.price(p).price, 10.0, 1e-10);
}

TEST(BSPriceTest, ZeroMaturityOTMCallIsZero) {
    OptionParams p{90.0, 100.0, 0.0, 0.05, 0.2, OptionType::Call};
    EXPECT_NEAR(bs.price(p).price, 0.0, 1e-10);
}

TEST(BSPriceTest, ZeroVolCallIsDiscountedIntrinsic) {
    // v=0, T=1: forward = 100*exp(0.05), call = max(fwd-K,0)*exp(-rT) = S - K*exp(-rT)
    OptionParams p{100.0, 90.0, 1.0, 0.05, 0.0, OptionType::Call};
    double expected = p.S - p.K * std::exp(-p.r * p.T);
    EXPECT_NEAR(bs.price(p).price, expected, 1e-10);
}

TEST(BSPriceTest, ZeroVolOTMCallIsZero) {
    OptionParams p{100.0, 110.0, 1.0, 0.05, 0.0, OptionType::Call};
    EXPECT_NEAR(bs.price(p).price, 0.0, 1e-10);
}

TEST(BSPriceTest, PriceIsNonNegative) {
    for (double S : {80.0, 100.0, 120.0}) {
        for (OptionType t : {OptionType::Call, OptionType::Put}) {
            OptionParams p{S, 100.0, 1.0, 0.05, 0.2, t};
            EXPECT_GE(bs.price(p).price, 0.0);
        }
    }
}

// ── Greeks: sanity bounds ──────────────────────────────────────────────────────

TEST(BSGreeksTest, CallDeltaBounds) {
    for (double K : {80.0, 100.0, 120.0}) {
        OptionParams p = kAtm; p.K = K;
        double d = bs.delta(p);
        EXPECT_GT(d, 0.0);
        EXPECT_LT(d, 1.0);
    }
}

TEST(BSGreeksTest, PutDeltaBounds) {
    for (double K : {80.0, 100.0, 120.0}) {
        OptionParams p = kAtm; p.K = K; p.type = OptionType::Put;
        double d = bs.delta(p);
        EXPECT_GT(d, -1.0);
        EXPECT_LT(d,  0.0);
    }
}

TEST(BSGreeksTest, PutCallDeltaParityIsOne) {
    // delta(call) - delta(put) = 1 for same params
    OptionParams pc = kAtm;
    OptionParams pp = kAtm; pp.type = OptionType::Put;
    EXPECT_NEAR(bs.delta(pc) - bs.delta(pp), 1.0, 1e-12);
}

TEST(BSGreeksTest, GammaIsPositive) {
    EXPECT_GT(bs.gamma(kAtm), 0.0);
}

TEST(BSGreeksTest, GammaCallPutEqual) {
    OptionParams pp = kAtm; pp.type = OptionType::Put;
    EXPECT_NEAR(bs.gamma(kAtm), bs.gamma(pp), 1e-12);
}

TEST(BSGreeksTest, VegaIsPositive) {
    EXPECT_GT(bs.vega(kAtm), 0.0);
}

TEST(BSGreeksTest, VegaCallPutEqual) {
    OptionParams pp = kAtm; pp.type = OptionType::Put;
    EXPECT_NEAR(bs.vega(kAtm), bs.vega(pp), 1e-12);
}

TEST(BSGreeksTest, ThetaCallIsNegative) {
    EXPECT_LT(bs.theta(kAtm), 0.0);
}

TEST(BSGreeksTest, RhoCallIsPositive) {
    EXPECT_GT(bs.rho(kAtm), 0.0);
}

TEST(BSGreeksTest, RhoPutIsNegative) {
    OptionParams pp = kAtm; pp.type = OptionType::Put;
    EXPECT_LT(bs.rho(pp), 0.0);
}

// ── Greeks: finite-difference numerical validation ─────────────────────────────

TEST(BSGreeksNumericalTest, DeltaMatchesFiniteDiff) {
    constexpr double dS = 0.01;
    OptionParams p = kAtm;
    OptionParams pu = p; pu.S += dS;
    OptionParams pd = p; pd.S -= dS;
    double fd = (bs.price(pu).price - bs.price(pd).price) / (2.0 * dS);
    EXPECT_NEAR(bs.delta(p), fd, 1e-6);
}

TEST(BSGreeksNumericalTest, GammaMatchesFiniteDiff) {
    constexpr double dS = 0.01;
    OptionParams p  = kAtm;
    OptionParams pu = p; pu.S += dS;
    OptionParams pd = p; pd.S -= dS;
    double fd = (bs.price(pu).price - 2.0 * bs.price(p).price + bs.price(pd).price)
                / (dS * dS);
    EXPECT_NEAR(bs.gamma(p), fd, 1e-5);
}

TEST(BSGreeksNumericalTest, VegaMatchesFiniteDiff) {
    constexpr double dv = 1e-4;
    OptionParams p  = kAtm;
    OptionParams pu = p; pu.v += dv;
    OptionParams pd = p; pd.v -= dv;
    double fd = (bs.price(pu).price - bs.price(pd).price) / (2.0 * dv);
    EXPECT_NEAR(bs.vega(p), fd, 1e-5);
}

TEST(BSGreeksNumericalTest, ThetaMatchesFiniteDiff) {
    constexpr double dT = 1e-4;
    OptionParams p  = kAtm;
    OptionParams pu = p; pu.T += dT;
    OptionParams pd = p; pd.T -= dT;
    // Theta = -dV/dT (value decreases as time to maturity shrinks)
    double fd = -(bs.price(pu).price - bs.price(pd).price) / (2.0 * dT);
    EXPECT_NEAR(bs.theta(p), fd, 1e-5);
}

TEST(BSGreeksNumericalTest, RhoMatchesFiniteDiff) {
    constexpr double dr = 1e-4;
    OptionParams p  = kAtm;
    OptionParams pu = p; pu.r += dr;
    OptionParams pd = p; pd.r -= dr;
    double fd = (bs.price(pu).price - bs.price(pd).price) / (2.0 * dr);
    EXPECT_NEAR(bs.rho(p), fd, 1e-5);
}
