#include <tgfx2/immediate_renderer.hpp>

#include <cassert>
#include <cmath>

namespace {

    bool near(float left, float right, float epsilon = 1.0e-5f) {
        return std::fabs(left - right) <= epsilon;
    }

} // namespace

int main() {
    termin::ImmediateRenderer renderer;
    constexpr float authored = 128.0f / 255.0f;
    const termin::SrgbColor color{authored, authored, authored, 0.5f};

    renderer.line(termin::Vec3{1.0, 2.0, 3.0}, termin::Vec3{4.0, 5.0, 6.0}, color);

    assert(renderer.line_count() == 1);
    assert(renderer.line_vertices.size() == 14);
    assert(near(renderer.line_vertices[0], 1.0f));
    assert(near(renderer.line_vertices[1], 2.0f));
    assert(near(renderer.line_vertices[2], 3.0f));
    assert(near(renderer.line_vertices[3], 0.2158605f));
    assert(near(renderer.line_vertices[4], 0.2158605f));
    assert(near(renderer.line_vertices[5], 0.2158605f));
    assert(near(renderer.line_vertices[6], 0.5f));
    assert(near(renderer.line_vertices[7], 4.0f));
    assert(near(renderer.line_vertices[8], 5.0f));
    assert(near(renderer.line_vertices[9], 6.0f));
    assert(near(renderer.line_vertices[10], 0.2158605f));
    assert(near(renderer.line_vertices[13], 0.5f));
    return 0;
}
