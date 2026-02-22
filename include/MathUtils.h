#pragma once

namespace MathUtils {

    // Cumulative standard normal distribution using std::erfc (C++20 <numbers>).
    double norm_cdf(double x);

    // Standard normal probability density function.
    double norm_pdf(double x);

}
