#include "MathUtils.h"

#include <cmath>
#include <numbers>

namespace MathUtils {

double norm_cdf(double x) {
    // Phi(x) = 0.5 * erfc(-x / sqrt(2))
    return 0.5 * std::erfc(-x * std::numbers::sqrt2 / 2.0);
}

double norm_pdf(double x) {
    // phi(x) = exp(-0.5 * x^2) / sqrt(2 * pi)
    return std::exp(-0.5 * x * x) / std::sqrt(2.0 * std::numbers::pi);
}

}
