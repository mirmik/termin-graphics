#pragma once

#include <cstdint>

#include "tgfx2/opengl/gl_features.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    struct GlCoordinateContract {
        bool shader_flips_clip_y = false;
        bool shader_remaps_zero_to_one_depth = false;
        bool framebuffer_api_origin_bottom_left = true;
        bool texture_origin_top_left = true;
        bool native_front_face_inverted = true;
    };

    struct GlClipPosition {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct GlFramebufferRect {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    TGFX2_API GlCoordinateContract gl_coordinate_contract(GlFeatureTier tier);
    TGFX2_API GlClipPosition gl_native_clip_position(const GlCoordinateContract& contract,
                                                     GlClipPosition engine_clip);
    TGFX2_API GlFramebufferRect gl_native_framebuffer_rect(const GlCoordinateContract& contract,
                                                           int framebuffer_height,
                                                           GlFramebufferRect top_left_rect);
    TGFX2_API int gl_native_readback_y(const GlCoordinateContract& contract,
                                       int framebuffer_height,
                                       int top_left_y);

} // namespace tgfx
