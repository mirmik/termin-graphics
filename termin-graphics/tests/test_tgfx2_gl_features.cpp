#include "guard_main.h"

#include <string>
#include <cmath>

#include "tgfx2/opengl/gl_features.hpp"
#include "tgfx2/opengl/gl_coordinates.hpp"
#if defined(TGFX2_HAS_OPENGL)
#include "tgfx2/opengl/gl_platform_operations.hpp"
#endif

namespace {

    tgfx::GlRuntimeInfo desktop_runtime(uint32_t major, uint32_t minor) {
        tgfx::GlRuntimeInfo runtime;
        runtime.api = tgfx::GlApi::Desktop;
        runtime.major = major;
        runtime.minor = minor;
        runtime.has_clip_control = true;
        runtime.has_polygon_mode = true;
        runtime.has_base_vertex_draws = true;
        runtime.has_multisample_textures = true;
        runtime.has_timestamp_queries = true;
        runtime.has_compute = true;
        runtime.has_geometry_shaders = true;
        runtime.max_color_attachments = 8;
        runtime.max_texture_dimension_2d = 16384;
        runtime.max_texture_units = 32;
        runtime.max_fragment_texture_units = 16;
        return runtime;
    }

} // namespace

TEST_CASE("GL feature tiers select explicit shader targets") {
    std::string error;
    tgfx::GlFeatureSet features;

    const tgfx::GlRuntimeInfo modern = desktop_runtime(4, 6);
    REQUIRE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::Modern, modern, features, error));
    CHECK(features.shader_target == tgfx::ShaderArtifactTarget::OpenGL450);
    CHECK(features.uses_clip_control);
    CHECK(features.supports_compute);
    CHECK(features.max_texture_units == 32);
    CHECK(features.max_fragment_texture_units == 16);
    CHECK(features.max_shadow_maps == 16);

    tgfx::GlRuntimeInfo constrained = desktop_runtime(3, 3);
    constrained.has_clip_control = false;
    constrained.has_compute = false;
    REQUIRE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::Constrained33, constrained, features, error));
    CHECK(features.shader_target == tgfx::ShaderArtifactTarget::OpenGL330);
    CHECK_FALSE(features.uses_clip_control);
    CHECK_FALSE(features.supports_compute);
    CHECK(features.max_shadow_maps == 8);

    tgfx::GlRuntimeInfo web;
    web.api = tgfx::GlApi::WebGL;
    web.major = 2;
    web.has_polygon_mode = true;
    web.has_base_vertex_draws = true;
    web.has_multisample_textures = true;
    web.has_timestamp_queries = true;
    web.has_compute = true;
    web.has_geometry_shaders = true;
    REQUIRE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::WebGL2, web, features, error));
    CHECK(features.shader_target == tgfx::ShaderArtifactTarget::WebGL2);
    CHECK_FALSE(features.uses_clip_control);
    CHECK_FALSE(features.supports_polygon_mode);
    CHECK_FALSE(features.supports_base_vertex_draws);
    CHECK_FALSE(features.supports_multisample_textures);
    CHECK_FALSE(features.supports_timestamp_queries);
    CHECK_FALSE(features.supports_compute);
    CHECK_FALSE(features.supports_geometry_shaders);
    CHECK(features.max_shadow_maps == 8);
}

TEST_CASE("GL feature tiers reject incompatible APIs and missing contracts") {
    std::string error;
    tgfx::GlFeatureSet features;

    tgfx::GlRuntimeInfo old_desktop = desktop_runtime(3, 2);
    CHECK_FALSE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::Constrained33, old_desktop, features, error));
    CHECK(error.find("OpenGL 3.3") != std::string::npos);

    tgfx::GlRuntimeInfo modern = desktop_runtime(4, 6);
    modern.has_clip_control = false;
    CHECK_FALSE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::Modern, modern, features, error));
    CHECK(error.find("clip-control") != std::string::npos);

    tgfx::GlRuntimeInfo desktop = desktop_runtime(4, 6);
    CHECK_FALSE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::WebGL2, desktop, features, error));
    CHECK(error.find("WebGL 2.0") != std::string::npos);
}

TEST_CASE("desktop GL tier parser keeps constrained selection explicit") {
    tgfx::GlFeatureTier tier = tgfx::GlFeatureTier::WebGL2;
    std::string error;

    CHECK(tgfx::parse_desktop_gl_feature_tier("", tier, error));
    CHECK(tier == tgfx::GlFeatureTier::Modern);
    CHECK(tgfx::parse_desktop_gl_feature_tier("modern", tier, error));
    CHECK(tier == tgfx::GlFeatureTier::Modern);
    CHECK(tgfx::parse_desktop_gl_feature_tier("OpenGL33", tier, error));
    CHECK(tier == tgfx::GlFeatureTier::Constrained33);
    CHECK(tgfx::parse_desktop_gl_feature_tier("3.3", tier, error));
    CHECK(tier == tgfx::GlFeatureTier::Constrained33);
    CHECK_FALSE(tgfx::parse_desktop_gl_feature_tier("webgl2", tier, error));
    CHECK(error.find("modern or opengl33") != std::string::npos);
}

TEST_CASE("GL coordinate tiers preserve engine orientation and zero-to-one depth") {
    const auto modern = tgfx::gl_coordinate_contract(tgfx::GlFeatureTier::Modern);
    const auto constrained = tgfx::gl_coordinate_contract(tgfx::GlFeatureTier::Constrained33);
    const auto webgl = tgfx::gl_coordinate_contract(tgfx::GlFeatureTier::WebGL2);

    CHECK_FALSE(modern.shader_flips_clip_y);
    CHECK_FALSE(modern.shader_remaps_zero_to_one_depth);
    CHECK(constrained.shader_flips_clip_y);
    CHECK(constrained.shader_remaps_zero_to_one_depth);
    CHECK(webgl.shader_flips_clip_y);
    CHECK(webgl.shader_remaps_zero_to_one_depth);
    CHECK(modern.native_front_face_inverted);
    CHECK_FALSE(constrained.native_front_face_inverted);
    CHECK_FALSE(webgl.native_front_face_inverted);

    const tgfx::GlClipPosition engine_clip{0.25f, -0.5f, 0.2f, 1.0f};
    const auto modern_clip = tgfx::gl_native_clip_position(modern, engine_clip);
    const auto constrained_clip = tgfx::gl_native_clip_position(constrained, engine_clip);

    CHECK(modern_clip.y == -0.5f);
    CHECK(modern_clip.z == 0.2f);
    CHECK(constrained_clip.y == 0.5f);
    CHECK(constrained_clip.z == -0.6f);

    // Upper-left clip-control consumes engine Y directly. Default GL maps
    // from the lower-left, so the shader-side sign flip produces the same
    // normalized top-down window coordinate.
    const float modern_top_down_y = modern_clip.y * 0.5f + 0.5f;
    const float constrained_top_down_y = 1.0f - (constrained_clip.y * 0.5f + 0.5f);
    CHECK(modern_top_down_y == constrained_top_down_y);

    const float modern_depth = modern_clip.z;
    const float constrained_depth = constrained_clip.z * 0.5f + 0.5f;
    CHECK(std::abs(modern_depth - constrained_depth) < 1.0e-6f);

    const tgfx::GlFramebufferRect top_left{3, 7, 11, 13};
    const auto native_rect = tgfx::gl_native_framebuffer_rect(constrained, 64, top_left);
    CHECK(native_rect.x == 3);
    CHECK(native_rect.y == 44);
    CHECK(native_rect.width == 11);
    CHECK(native_rect.height == 13);
    CHECK(tgfx::gl_native_readback_y(webgl, 64, 7) == 56);
}

#if defined(TGFX2_HAS_OPENGL)
TEST_CASE("GL platform boundary rejects unsupported operations before calling GL") {
    tgfx::GlRuntimeInfo runtime;
    runtime.api = tgfx::GlApi::WebGL;
    runtime.major = 2;

    tgfx::GlFeatureSet features;
    std::string error;
    REQUIRE(tgfx::derive_gl_feature_set(tgfx::GlFeatureTier::WebGL2, runtime, features, error));

    const tgfx::GlPlatformOperations operations(features);
    CHECK(operations.apply_polygon_mode(GL_FILL));
    CHECK_FALSE(operations.apply_polygon_mode(GL_LINE));
    CHECK_FALSE(operations.draw_elements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr, 4));
    CHECK_FALSE(operations.draw_elements_instanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, nullptr, 2, 4));
    CHECK_FALSE(operations.allocate_multisample_texture_2d(
        GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 64, 64, true));
    CHECK_FALSE(operations.issue_timestamp(1));

    uint64_t timestamp = 42;
    bool available = true;
    CHECK_FALSE(operations.read_timestamp(1, false, timestamp, available));
    CHECK(timestamp == 0);
    CHECK_FALSE(available);
}
#endif
