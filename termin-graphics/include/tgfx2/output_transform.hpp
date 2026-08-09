#pragma once

#include <cstdint>
#include <type_traits>

#include "tgfx/resources/tc_shader.h"
#include "tgfx/texture_encoding.h"
#include "tgfx2/handles.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    class IRenderDevice;
    class RenderContext2;

    enum class OutputDitherMode : uint8_t {
        Disabled = 0,
        StableSpatial = 1,
    };

    // Low-level parameters for one display-referred color conversion. The
    // caller owns semantic policy; this descriptor only tells the GPU program
    // how sampled RGB values and the destination attachment are encoded.
    struct OutputTransformParams {
        TextureEncoding sampled_input_encoding = TextureEncoding::Linear;
        TextureEncoding target_encoding = TextureEncoding::Linear;
        OutputDitherMode dither = OutputDitherMode::Disabled;
        uint8_t target_rgb_bits = 0;
    };

    static_assert(std::is_standard_layout_v<OutputTransformParams>);

    // Records the programmable color-output operation shared by framegraph
    // exports and physical presentation sinks. It owns no target textures and
    // may be reused across frames on one graphics device.
    class TGFX2_TYPE_API OutputTransformRenderer {
    public:
        OutputTransformRenderer() = default;
        ~OutputTransformRenderer();

        OutputTransformRenderer(const OutputTransformRenderer&) = delete;
        OutputTransformRenderer& operator=(const OutputTransformRenderer&) = delete;

        bool record(RenderContext2& context,
                    TextureHandle input,
                    TextureHandle output,
                    const OutputTransformParams& params);
        void close();

    private:
        IRenderDevice* device_ = nullptr;
        tc_shader_handle shader_handle_ = tc_shader_handle_invalid();
        tc_shader_handle multiview_shader_handle_ = tc_shader_handle_invalid();
    };

} // namespace tgfx
