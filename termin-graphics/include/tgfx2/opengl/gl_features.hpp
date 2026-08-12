#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "tgfx2/shader_artifact_target.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    enum class GlApi : uint8_t {
        Desktop,
        WebGL,
    };

    enum class GlFeatureTier : uint8_t {
        Modern,
        Constrained33,
        WebGL2,
    };

    struct GlRuntimeInfo {
        GlApi api = GlApi::Desktop;
        uint32_t major = 0;
        uint32_t minor = 0;
        bool has_clip_control = false;
        bool has_polygon_mode = false;
        bool has_base_vertex_draws = false;
        bool has_multisample_textures = false;
        bool has_timestamp_queries = false;
        bool has_compute = false;
        bool has_geometry_shaders = false;
        uint32_t max_color_attachments = 0;
        uint32_t max_texture_dimension_2d = 0;
        uint32_t max_texture_units = 0;
        uint32_t max_fragment_texture_units = 0;
    };

    struct GlFeatureSet {
        GlFeatureTier tier = GlFeatureTier::Modern;
        ShaderArtifactTarget shader_target = ShaderArtifactTarget::None;
        bool uses_clip_control = false;
        bool supports_polygon_mode = false;
        bool supports_base_vertex_draws = false;
        bool supports_multisample_textures = false;
        bool supports_timestamp_queries = false;
        bool supports_compute = false;
        bool supports_geometry_shaders = false;
        uint32_t max_color_attachments = 0;
        uint32_t max_texture_dimension_2d = 0;
        uint32_t max_texture_units = 0;
        uint32_t max_fragment_texture_units = 0;
        uint32_t max_shadow_maps = 0;
    };

    TGFX2_API const char* gl_feature_tier_name(GlFeatureTier tier);
    TGFX2_API bool parse_desktop_gl_feature_tier(std::string_view value,
                                                 GlFeatureTier& out,
                                                 std::string& error);
    TGFX2_API bool derive_gl_feature_set(GlFeatureTier requested,
                                         const GlRuntimeInfo& runtime,
                                         GlFeatureSet& out,
                                         std::string& error);

} // namespace tgfx
