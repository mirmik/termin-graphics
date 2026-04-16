// axes.cpp - Tick placement / formatting.

#include "tcplot/axes.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tcplot {
namespace axes {

std::vector<double> nice_ticks(double lo, double hi, int max_ticks) {
    std::vector<double> out;
    if (hi <= lo) {
        out.push_back(lo);
        return out;
    }

    const double span = hi - lo;
    const double rough_step = span / std::max(max_ticks - 1, 1);

    // Round step to a nice multiple of a power of ten.
    const double exp = std::floor(std::log10(rough_step));
    const double base = std::pow(10.0, exp);
    const double normalized = rough_step / base;

    double nice_step;
    if (normalized <= 1.0) {
        nice_step = base;
    } else if (normalized <= 2.0) {
        nice_step = 2.0 * base;
    } else if (normalized <= 5.0) {
        nice_step = 5.0 * base;
    } else {
        nice_step = 10.0 * base;
    }

    const double start = std::floor(lo / nice_step) * nice_step;
    const double eps = nice_step * 0.001;

    double v = start;
    // The Python version iterates until `v <= hi + eps`; mirror that.
    // Use a bounded loop to protect against accidental infinite
    // iteration on pathological inputs (NaN, huge span).
    int safety = 0;
    while (v <= hi + eps && safety < 100000) {
        if (v >= lo - eps) {
            // Trim floating-point dust: round to 12 decimal places.
            const double factor = 1e12;
            out.push_back(std::round(v * factor) / factor);
        }
        v += nice_step;
        safety++;
    }
    return out;
}

std::string format_tick(double value) {
    if (value == 0.0) {
        return "0";
    }
    const double abs_val = std::abs(value);
    char buf[64];
    if (abs_val >= 1e6 || abs_val < 1e-3) {
        std::snprintf(buf, sizeof(buf), "%.2e", value);
        return buf;
    }
    // "%.6g" matches Python's f"{value:.6g}" — includes trimming of
    // insignificant trailing zeros in the mantissa.
    std::snprintf(buf, sizeof(buf), "%.6g", value);
    return buf;
}

}  // namespace axes
}  // namespace tcplot
