#pragma once

#include "OptionParams.h"

struct PricingResult {
    double price;      // Fair value of the option
    double std_error;  // Standard error of the estimate (0.0 for analytical pricers)
};

class IPricer {
public:
    virtual ~IPricer() = default;
    virtual PricingResult price(const OptionParams& params) const = 0;
};
