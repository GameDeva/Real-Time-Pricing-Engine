#pragma once

#include <stdexcept>

enum class OptionType { Call, Put };

struct OptionParams {
    double S;       // Spot price of the underlying asset
    double K;       // Strike price
    double T;       // Time to maturity in years
    double r;       // Annualized risk-free interest rate
    double v;       // Annualized volatility (sigma)
    OptionType type;
};

inline void validate(const OptionParams& p) {
    if (p.S <= 0.0) throw std::invalid_argument("Spot price S must be positive.");
    if (p.K <= 0.0) throw std::invalid_argument("Strike price K must be positive.");
    if (p.T <  0.0) throw std::invalid_argument("Time to maturity T must be non-negative.");
    if (p.v <  0.0) throw std::invalid_argument("Volatility v must be non-negative.");
}
