#include "tgfx2/opengl/gl_features.hpp"

#include <sstream>
#include <algorithm>
#include <cctype>

namespace tgfx {

    namespace {

        bool version_at_least(const GlRuntimeInfo& runtime, uint32_t major, uint32_t minor) {
            return runtime.major > major || (runtime.major == major && runtime.minor >= minor);
        }

        bool fail_version(GlFeatureTier requested,
                          const GlRuntimeInfo& runtime,
                          const char* required,
                          std::string& error) {
            std::ostringstream message;
            message << "GL feature tier '" << gl_feature_tier_name(requested) << "' requires " << required
                    << ", reported API=" << (runtime.api == GlApi::WebGL ? "webgl" : "desktop-gl") << " "
                    << runtime.major << "." << runtime.minor;
            error = message.str();
            return false;
        }

    } // namespace

    const char* gl_feature_tier_name(GlFeatureTier tier) {
        switch (tier) {
        case GlFeatureTier::Modern:
            return "modern";
        case GlFeatureTier::Constrained33:
            return "opengl33";
        case GlFeatureTier::WebGL2:
            return "webgl2";
        }
        return "unknown";
    }

    bool parse_desktop_gl_feature_tier(std::string_view value,
                                       GlFeatureTier& out,
                                       std::string& error) {
        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (normalized.empty() || normalized == "modern") {
            out = GlFeatureTier::Modern;
            error.clear();
            return true;
        }
        if (normalized == "3.3" || normalized == "33" || normalized == "opengl33" ||
            normalized == "constrained33") {
            out = GlFeatureTier::Constrained33;
            error.clear();
            return true;
        }
        error = "invalid desktop GL tier '" + std::string(value) +
                "' (expected modern or opengl33)";
        return false;
    }

    bool derive_gl_feature_set(GlFeatureTier requested,
                               const GlRuntimeInfo& runtime,
                               GlFeatureSet& out,
                               std::string& error) {
        error.clear();
        GlFeatureSet features;
        features.tier = requested;
        features.max_color_attachments = runtime.max_color_attachments;
        features.max_texture_dimension_2d = runtime.max_texture_dimension_2d;
        features.max_texture_units = runtime.max_texture_units;
        features.max_fragment_texture_units = runtime.max_fragment_texture_units;

        switch (requested) {
        case GlFeatureTier::Modern:
            // The historical backend accepts core 4.x contexts where
            // ARB_clip_control supplies the 4.5 operation. Keep that behavior:
            // the feature contract matters more than the nominal version.
            if (runtime.api != GlApi::Desktop || !version_at_least(runtime, 3, 3)) {
                return fail_version(requested, runtime, "desktop OpenGL 3.3 plus clip-control", error);
            }
            if (!runtime.has_clip_control) {
                error = "GL feature tier 'modern' requires clip-control";
                return false;
            }
            features.shader_target = ShaderArtifactTarget::OpenGL450;
            features.uses_clip_control = true;
            features.supports_polygon_mode = runtime.has_polygon_mode;
            features.supports_base_vertex_draws = runtime.has_base_vertex_draws;
            features.supports_multisample_textures = runtime.has_multisample_textures;
            features.supports_timestamp_queries = runtime.has_timestamp_queries;
            features.supports_compute = runtime.has_compute;
            features.supports_geometry_shaders = runtime.has_geometry_shaders;
            features.max_shadow_maps = 16;
            break;

        case GlFeatureTier::Constrained33:
            if (runtime.api != GlApi::Desktop || !version_at_least(runtime, 3, 3)) {
                return fail_version(requested, runtime, "desktop OpenGL 3.3", error);
            }
            features.shader_target = ShaderArtifactTarget::OpenGL330;
            features.supports_polygon_mode = runtime.has_polygon_mode;
            features.supports_base_vertex_draws = runtime.has_base_vertex_draws;
            features.supports_multisample_textures = runtime.has_multisample_textures;
            features.supports_timestamp_queries = runtime.has_timestamp_queries;
            features.supports_geometry_shaders = runtime.has_geometry_shaders;
            features.max_shadow_maps = 8;
            break;

        case GlFeatureTier::WebGL2:
            if (runtime.api != GlApi::WebGL || !version_at_least(runtime, 2, 0)) {
                return fail_version(requested, runtime, "WebGL 2.0", error);
            }
            features.shader_target = ShaderArtifactTarget::WebGL2;
            // Desktop-only operations remain false even if a host shim exposes
            // similarly named entry points. WebGL extensions need explicit,
            // separately modelled support before the engine may use them.
            features.max_shadow_maps = 8;
            break;
        }

        out = features;
        return true;
    }

} // namespace tgfx
