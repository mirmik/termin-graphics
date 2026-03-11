#pragma once

#include <algorithm>
#include <cmath>

namespace termin {

struct AttenuationCoefficients {
public:
    double constant = 1.0;
    double linear = 0.0;
    double quadratic = 0.0;

public:
    AttenuationCoefficients() = default;

    AttenuationCoefficients(double c, double l, double q)
        : constant(c), linear(l), quadratic(q) {}

    double evaluate(double distance) const {
        double d = std::max(distance, 0.0);
        double denom = constant + linear * d + quadratic * d * d;
        if (denom <= 0.0) return 0.0;
        return 1.0 / denom;
    }

    static AttenuationCoefficients match_range(double falloff_range, double cutoff = 0.01) {
        double r = std::max(falloff_range, 1e-6);
        double k_q = (1.0 / cutoff - 1.0) / (r * r);
        return AttenuationCoefficients(1.0, 0.0, k_q);
    }

    static AttenuationCoefficients inverse_square() {
        return AttenuationCoefficients(0.0, 0.0, 1.0);
    }
};

} // namespace termin
