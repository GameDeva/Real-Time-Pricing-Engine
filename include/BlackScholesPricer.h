#pragma once

#include "IPricer.h"

class BlackScholesPricer : public IPricer {
public:
    PricingResult price(const OptionParams& params) const override;

    double delta(const OptionParams& params) const;
    double gamma(const OptionParams& params) const;
    double vega (const OptionParams& params) const;
    double theta(const OptionParams& params) const;
    double rho  (const OptionParams& params) const;
};
