#pragma once

namespace termin {

struct ShadowSettings {
public:
    static constexpr int METHOD_HARD = 0;
    static constexpr int METHOD_PCF = 1;
    static constexpr int METHOD_POISSON = 2;

    int method = METHOD_PCF;
    double softness = 1.0;
    double bias = 0.005;

public:
    ShadowSettings() = default;
    ShadowSettings(int method_, double softness_, double bias_)
        : method(method_),
          softness(softness_),
          bias(bias_) {}
};

} // namespace termin
