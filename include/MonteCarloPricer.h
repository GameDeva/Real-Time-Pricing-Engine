#pragma once

#include "IPricer.h"

#include <cstdint>
#include <optional>

class MonteCarloPricer : public IPricer {
public:
    // num_paths: total GBM paths (actual paths = num_paths; antithetic uses num_paths/2 RNG draws).
    // seed: fixed seed for deterministic results; omit for non-deterministic.
    explicit MonteCarloPricer(uint64_t num_paths,
                               std::optional<uint64_t> seed = std::nullopt);

    PricingResult price(const OptionParams& params) const override;

    uint64_t num_paths() const noexcept { return num_paths_; }

private:
    uint64_t num_paths_;
    uint64_t base_seed_;
    bool     fixed_seed_;
};
