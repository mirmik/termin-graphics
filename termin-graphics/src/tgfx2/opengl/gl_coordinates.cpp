#include "tgfx2/opengl/gl_coordinates.hpp"

namespace tgfx {

    GlCoordinateContract gl_coordinate_contract(GlFeatureTier tier) {
        GlCoordinateContract contract;
        if (tier == GlFeatureTier::Constrained33 || tier == GlFeatureTier::WebGL2) {
            contract.shader_flips_clip_y = true;
            contract.shader_remaps_zero_to_one_depth = true;
            contract.native_front_face_inverted = false;
        }
        return contract;
    }

    GlClipPosition gl_native_clip_position(const GlCoordinateContract& contract,
                                           GlClipPosition engine_clip) {
        if (contract.shader_flips_clip_y)
            engine_clip.y = -engine_clip.y;
        if (contract.shader_remaps_zero_to_one_depth)
            engine_clip.z = 2.0f * engine_clip.z - engine_clip.w;
        return engine_clip;
    }

    GlFramebufferRect gl_native_framebuffer_rect(const GlCoordinateContract& contract,
                                                  int framebuffer_height,
                                                  GlFramebufferRect top_left_rect) {
        if (contract.framebuffer_api_origin_bottom_left && framebuffer_height > 0) {
            top_left_rect.y = framebuffer_height - (top_left_rect.y + top_left_rect.height);
        }
        return top_left_rect;
    }

    int gl_native_readback_y(const GlCoordinateContract& contract,
                             int framebuffer_height,
                             int top_left_y) {
        if (contract.framebuffer_api_origin_bottom_left && framebuffer_height > 0)
            return framebuffer_height - top_left_y - 1;
        return top_left_y;
    }

} // namespace tgfx
