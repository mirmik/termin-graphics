#include "tgfx2/render_state.hpp"
#include "tgfx2/opengl/opengl_type_conversions.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#endif

#ifdef TGFX2_HAS_D3D11
#include "tgfx2/d3d11/d3d11_type_conversions.hpp"
#endif

#include <cstdio>

int main() {
    const tgfx::RasterState raster;
    if (raster.front_face != tgfx::FrontFace::CCW) {
        std::fprintf(stderr, "RasterState default front face is not logical CCW\n");
        return 1;
    }

    if (tgfx::gl::to_gl_front_face(tgfx::FrontFace::CCW) != GL_CW ||
        tgfx::gl::to_gl_front_face(tgfx::FrontFace::CW) != GL_CCW) {
        std::fprintf(stderr, "OpenGL logical front-face mapping regressed\n");
        return 1;
    }

#ifdef TGFX2_HAS_VULKAN
    if (tgfx::vk::to_vk_front_face(tgfx::FrontFace::CCW) != VK_FRONT_FACE_COUNTER_CLOCKWISE ||
        tgfx::vk::to_vk_front_face(tgfx::FrontFace::CW) != VK_FRONT_FACE_CLOCKWISE) {
        std::fprintf(stderr, "Vulkan logical front-face mapping regressed\n");
        return 1;
    }
#endif

#ifdef TGFX2_HAS_D3D11
    if (!tgfx::d3d11::to_d3d_front_counter_clockwise(tgfx::FrontFace::CCW) ||
        tgfx::d3d11::to_d3d_front_counter_clockwise(tgfx::FrontFace::CW)) {
        std::fprintf(stderr, "D3D11 logical front-face mapping regressed\n");
        return 1;
    }
#endif

    return 0;
}
