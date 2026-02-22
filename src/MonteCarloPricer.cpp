#include "MonteCarloPricer.h"

#include <cmath>
#include <future>
#include <random>
#include <thread>
#include <vector>
#include <algorithm>

namespace {

struct WorkerResult {
    double   sum;     // sum of discounted antithetic-averaged payoffs
    double   sum_sq;  // sum of squared discounted antithetic-averaged payoffs
    uint64_t count;   // number of antithetic pairs processed
};

// Each worker owns its own mt19937_64 — no shared mutable state, no locks.
// Antithetic variates: for each draw Z we evaluate payoffs at +Z and -Z,
// then average them before accumulating. This halves variance for vanilla payoffs.
WorkerResult run_chunk(const OptionParams& p, uint64_t n_pairs, uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> dist(0.0, 1.0);

    const double drift    = (p.r - 0.5 * p.v * p.v) * p.T;
    const double vst      = p.v * std::sqrt(p.T);
    const double discount = std::exp(-p.r * p.T);
    const bool   is_call  = (p.type == OptionType::Call);

    double   sum    = 0.0;
    double   sum_sq = 0.0;

    for (uint64_t i = 0; i < n_pairs; ++i) {
        const double Z = dist(rng);

        const double ST_pos = p.S * std::exp(drift + vst * Z);
        const double ST_neg = p.S * std::exp(drift - vst * Z);

        const double pay_pos = is_call ? std::max(0.0, ST_pos - p.K)
                                       : std::max(0.0, p.K - ST_pos);
        const double pay_neg = is_call ? std::max(0.0, ST_neg - p.K)
                                       : std::max(0.0, p.K - ST_neg);

        const double avg = 0.5 * (pay_pos + pay_neg) * discount;
        sum    += avg;
        sum_sq += avg * avg;
    }

    return {sum, sum_sq, n_pairs};
}

} // namespace

// ── Constructor ────────────────────────────────────────────────────────────────

MonteCarloPricer::MonteCarloPricer(uint64_t num_paths,
                                   std::optional<uint64_t> seed)
    : num_paths_(num_paths)
    , base_seed_(seed.value_or(0))
    , fixed_seed_(seed.has_value())
{}

// ── price ─────────────────────────────────────────────────────────────────────

PricingResult MonteCarloPricer::price(const OptionParams& p) const {
    validate(p);

    const unsigned n_threads   = std::max(1u, std::thread::hardware_concurrency());
    // Divide num_paths into antithetic pairs, distributed evenly across threads.
    const uint64_t total_pairs  = std::max<uint64_t>(1, (num_paths_ + 1) / 2);
    const uint64_t pairs_per_th = (total_pairs + n_threads - 1) / n_threads;

    // Root seed: fixed if provided, otherwise from hardware entropy.
    // Each thread receives root_seed + thread_index — guaranteed distinct.
    const uint64_t root_seed = fixed_seed_
        ? base_seed_
        : static_cast<uint64_t>(std::random_device{}());

    std::vector<std::future<WorkerResult>> futures;
    futures.reserve(n_threads);

    for (unsigned t = 0; t < n_threads; ++t) {
        futures.push_back(
            std::async(std::launch::async,
                       run_chunk,
                       std::cref(p), pairs_per_th, root_seed + t));
    }

    // Aggregate partial results — single synchronization point.
    double   total_sum    = 0.0;
    double   total_sum_sq = 0.0;
    uint64_t total_count  = 0;

    for (auto& f : futures) {
        auto [s, ss, c] = f.get();
        total_sum    += s;
        total_sum_sq += ss;
        total_count  += c;
    }

    const double n    = static_cast<double>(total_count);
    const double mean = total_sum / n;

    // Bessel-corrected sample variance, guarded against floating-point negatives.
    const double sample_var = (total_sum_sq - (total_sum * total_sum) / n)
                              / (n - 1.0);
    const double std_error  = std::sqrt(std::max(0.0, sample_var) / n);

    return {mean, std_error};
}
