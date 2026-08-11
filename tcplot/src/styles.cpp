// styles.cpp - Color constants and helpers for tcplot.

#include "tcplot/styles.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace tcplot {
    namespace styles {

        namespace {

            // Values lifted verbatim from tcplot/styles.py so existing Python
            // screenshots stay reproducible.
            constexpr SrgbColor kDefaultColors[] = {
                {0.12f, 0.47f, 0.71f, 1.0f}, // blue
                {1.00f, 0.50f, 0.05f, 1.0f}, // orange
                {0.17f, 0.63f, 0.17f, 1.0f}, // green
                {0.84f, 0.15f, 0.16f, 1.0f}, // red
                {0.58f, 0.40f, 0.74f, 1.0f}, // purple
                {0.55f, 0.34f, 0.29f, 1.0f}, // brown
                {0.89f, 0.47f, 0.76f, 1.0f}, // pink
                {0.50f, 0.50f, 0.50f, 1.0f}, // gray
                {0.74f, 0.74f, 0.13f, 1.0f}, // olive
                {0.09f, 0.75f, 0.81f, 1.0f}, // cyan
            };

            constexpr uint32_t kDefaultColorsCount = 10;

        } // namespace

        const SrgbColor* default_colors() {
            return kDefaultColors;
        }
        uint32_t default_colors_count() {
            return kDefaultColorsCount;
        }

        SrgbColor axis_color() {
            return {0.7f, 0.7f, 0.7f, 1.0f};
        }
        SrgbColor grid_color() {
            return {0.3f, 0.3f, 0.3f, 0.5f};
        }
        SrgbColor label_color() {
            return {0.8f, 0.8f, 0.8f, 1.0f};
        }
        // Neutral greys — R = G = B so hosts on a pure-grey dark theme
        // (#1A1A1A / #242424) don't see a subtle blue cast. If a host wants a
        // tinted look they override via set_bg_color / set_plot_bg_color.
        SrgbColor bg_color() {
            return {0.14f, 0.14f, 0.14f, 1.0f};
        }
        SrgbColor plot_area_bg() {
            return {0.10f, 0.10f, 0.10f, 1.0f};
        }

        SrgbColor cycle_color(uint32_t index) {
            return kDefaultColors[index % kDefaultColorsCount];
        }

        SrgbColor jet(float t) {
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

        SrgbColor colormap(SurfaceColorMap map, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            const auto piecewise = [t](const auto& colors) {
                const float x = t * static_cast<float>(colors.size() - 1);
                const std::size_t index = std::min(static_cast<std::size_t>(std::floor(x)), colors.size() - 2);
                const float f = x - static_cast<float>(index);
                const SrgbColor& left = colors[index];
                const SrgbColor& right = colors[index + 1];
                return SrgbColor{
                    left.r + (right.r - left.r) * f,
                    left.g + (right.g - left.g) * f,
                    left.b + (right.b - left.b) * f,
                    1.0f,
                };
            };
            switch (map) {
            case SurfaceColorMap::Viridis: {
                static constexpr std::array<SrgbColor, 10> colors{{
                    {0.267f, 0.005f, 0.329f, 1.0f},
                    {0.283f, 0.141f, 0.458f, 1.0f},
                    {0.254f, 0.265f, 0.530f, 1.0f},
                    {0.207f, 0.372f, 0.553f, 1.0f},
                    {0.164f, 0.471f, 0.558f, 1.0f},
                    {0.128f, 0.567f, 0.551f, 1.0f},
                    {0.135f, 0.659f, 0.518f, 1.0f},
                    {0.267f, 0.749f, 0.441f, 1.0f},
                    {0.478f, 0.821f, 0.318f, 1.0f},
                    {0.741f, 0.873f, 0.150f, 1.0f},
                }};
                return piecewise(colors);
            }
            case SurfaceColorMap::Plasma: {
                static constexpr std::array<SrgbColor, 7> colors{{
                    {0.050f, 0.030f, 0.528f, 1.0f},
                    {0.362f, 0.004f, 0.649f, 1.0f},
                    {0.610f, 0.090f, 0.620f, 1.0f},
                    {0.798f, 0.280f, 0.470f, 1.0f},
                    {0.928f, 0.473f, 0.326f, 1.0f},
                    {0.994f, 0.704f, 0.184f, 1.0f},
                    {0.940f, 0.975f, 0.131f, 1.0f},
                }};
                return piecewise(colors);
            }
            case SurfaceColorMap::Grayscale:
                return {t, t, t, 1.0f};
            case SurfaceColorMap::CoolWarm: {
                static constexpr std::array<SrgbColor, 3> colors{{
                    {0.230f, 0.299f, 0.754f, 1.0f},
                    {0.865f, 0.865f, 0.865f, 1.0f},
                    {0.706f, 0.016f, 0.150f, 1.0f},
                }};
                return piecewise(colors);
            }
            case SurfaceColorMap::Solid:
                return {1.0f, 1.0f, 1.0f, 1.0f};
            case SurfaceColorMap::Jet:
            default:
                return jet(t);
            }
        }

    } // namespace styles
} // namespace tcplot
