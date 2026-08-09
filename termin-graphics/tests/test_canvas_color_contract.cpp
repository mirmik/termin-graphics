#include <tgfx2/canvas2d_renderer.hpp>

#include <cassert>
#include <cmath>

#include <termin/geom/color.hpp>

int main() {
    constexpr float encoded = 128.0f / 255.0f;
    const tgfx::CanvasSrgbColor authored{encoded, encoded, encoded, 0.5f};
    const termin::LinearColor linear = termin::srgb_to_linear(
        termin::SrgbColor{authored.r, authored.g, authored.b, authored.a});
    assert(std::fabs(linear.r - 0.2158605f) < 1.0e-5f);
    assert(std::fabs(linear.g - 0.2158605f) < 1.0e-5f);
    assert(std::fabs(linear.b - 0.2158605f) < 1.0e-5f);
    assert(linear.a == 0.5f);
    return 0;
}
