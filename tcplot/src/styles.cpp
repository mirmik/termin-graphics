// styles.cpp - Color constants and helpers for tcplot.

#include "tcplot/styles.hpp"

#include <algorithm>

namespace tcplot {
namespace styles {

namespace {

// Values lifted verbatim from tcplot/styles.py so existing Python
// screenshots stay reproducible.
constexpr Color4 kDefaultColors[] = {
    {0.12f, 0.47f, 0.71f, 1.0f},   // blue
    {1.00f, 0.50f, 0.05f, 1.0f},   // orange
    {0.17f, 0.63f, 0.17f, 1.0f},   // green
    {0.84f, 0.15f, 0.16f, 1.0f},   // red
    {0.58f, 0.40f, 0.74f, 1.0f},   // purple
    {0.55f, 0.34f, 0.29f, 1.0f},   // brown
    {0.89f, 0.47f, 0.76f, 1.0f},   // pink
    {0.50f, 0.50f, 0.50f, 1.0f},   // gray
    {0.74f, 0.74f, 0.13f, 1.0f},   // olive
    {0.09f, 0.75f, 0.81f, 1.0f},   // cyan
};

constexpr uint32_t kDefaultColorsCount = 10;

}  // namespace

const Color4* default_colors() { return kDefaultColors; }
uint32_t default_colors_count() { return kDefaultColorsCount; }

Color4 axis_color()    { return {0.7f, 0.7f, 0.7f, 1.0f}; }
Color4 grid_color()    { return {0.3f, 0.3f, 0.3f, 0.5f}; }
Color4 label_color()   { return {0.8f, 0.8f, 0.8f, 1.0f}; }
Color4 bg_color()      { return {0.10f, 0.10f, 0.12f, 1.0f}; }
Color4 plot_area_bg()  { return {0.13f, 0.13f, 0.15f, 1.0f}; }

Color4 cycle_color(uint32_t index) {
    return kDefaultColors[index % kDefaultColorsCount];
}

Color4 jet(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (t < 0.125f) {
        r = 0.0f;
        g = 0.0f;
        b = 0.5f + t * 4.0f;
    } else if (t < 0.375f) {
        r = 0.0f;
        g = (t - 0.125f) * 4.0f;
        b = 1.0f;
    } else if (t < 0.625f) {
        r = (t - 0.375f) * 4.0f;
        g = 1.0f;
        b = 1.0f - (t - 0.375f) * 4.0f;
    } else if (t < 0.875f) {
        r = 1.0f;
        g = 1.0f - (t - 0.625f) * 4.0f;
        b = 0.0f;
    } else {
        r = 1.0f - (t - 0.875f) * 4.0f;
        g = 0.0f;
        b = 0.0f;
    }
    return {r, g, b, 1.0f};
}

}  // namespace styles
}  // namespace tcplot
