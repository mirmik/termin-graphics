#include "tgfx2/output_transform.hpp"

#include <algorithm>

#include <tcbase/tc_log.hpp>

#include "tgfx2/builtin_shader_sources.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace tgfx {

    namespace {

        constexpr const char* kOutputTransformShaderUuid = "termin-engine-output-transform";
        constexpr const char* kMultiviewOutputTransformShaderUuid = "termin-engine-multiview-output-transform";

        struct alignas(16) OutputTransformGpuParams {
            uint32_t sampled_input_is_srgb = 0;
            uint32_t target_is_srgb = 0;
            float dither_step = 0.0f;
            uint32_t dither_enabled = 0;
        };

        static_assert(sizeof(OutputTransformGpuParams) == 16);

    } // namespace

    OutputTransformRenderer::~OutputTransformRenderer() {
        close();
    }

    bool OutputTransformRenderer::record(RenderContext2& context,
                                         TextureHandle input,
                                         TextureHandle output,
                                         const OutputTransformParams& params) {
        if (!input || !output) {
            tc::Log::error("[OutputTransform] input and output textures are required");
            return false;
        }

        IRenderDevice& device = context.device();
        if (device_ && device_ != &device) {
            tc::Log::error("[OutputTransform] renderer cannot migrate between graphics devices");
            return false;
        }
        device_ = &device;

        const TextureDesc input_desc = device.texture_desc(input);
        const TextureDesc output_desc = device.texture_desc(output);
        if (input_desc.width == 0 || input_desc.height == 0 || output_desc.width == 0 || output_desc.height == 0) {
            tc::Log::error("[OutputTransform] source or target has zero extent");
            return false;
        }
        if (input_desc.sample_count != 1 || output_desc.sample_count != 1) {
            tc::Log::error("[OutputTransform] programmable transform requires single-sample textures");
            return false;
        }
        if (input_desc.array_layers != output_desc.array_layers) {
            tc::Log::error("[OutputTransform] source and target array layer counts differ");
            return false;
        }

        const bool multiview = output_desc.array_layers > 1;
        if (multiview && (!device.capabilities().supports_multiview ||
                          output_desc.array_layers > device.capabilities().max_multiview_views)) {
            tc::Log::error("[OutputTransform] target layers exceed backend multiview capabilities");
            return false;
        }

        tc_shader_handle& selected_shader_handle = multiview ? multiview_shader_handle_ : shader_handle_;
        const char* selected_shader_uuid =
            multiview ? kMultiviewOutputTransformShaderUuid : kOutputTransformShaderUuid;
        if (tc_shader_handle_is_invalid(selected_shader_handle)) {
            selected_shader_handle = register_builtin_shader_from_catalog(selected_shader_uuid);
            if (tc_shader_handle_is_invalid(selected_shader_handle)) {
                tc::Log::error("[OutputTransform] failed to register built-in shader");
                return false;
            }
        }

        ShaderHandle fragment_shader;
        tc_shader* raw = tc_shader_get(selected_shader_handle);
        if (!raw || !termin::tc_shader_ensure_tgfx2(raw, device_, nullptr, &fragment_shader)) {
            tc::Log::error("[OutputTransform] failed to prepare built-in shader");
            return false;
        }

        const bool dither_enabled = params.dither == OutputDitherMode::StableSpatial && params.target_rgb_bits > 0;
        const uint32_t quantization_levels = params.target_rgb_bits >= 31 ? 0u : ((1u << params.target_rgb_bits) - 1u);
        const OutputTransformGpuParams gpu_params{
            .sampled_input_is_srgb = params.sampled_input_encoding == TextureEncoding::SRGB ? 1u : 0u,
            .target_is_srgb = params.target_encoding == TextureEncoding::SRGB ? 1u : 0u,
            .dither_step = dither_enabled && quantization_levels > 0
                                ? 1.0f / static_cast<float>(quantization_levels)
                                : 0.0f,
            .dither_enabled = dither_enabled ? 1u : 0u,
        };

        if (multiview) {
            MultiviewRenderPassDesc pass;
            pass.view_count = output_desc.array_layers;
            pass.color_final_state = MultiviewColorFinalState::ShaderRead;
            ColorAttachmentDesc color;
            color.texture = output;
            color.load = LoadOp::DontCare;
            color.store = StoreOp::Store;
            pass.colors.push_back(color);
            if (!context.begin_multiview_pass(pass)) {
                tc::Log::error("[OutputTransform] failed to begin multiview output pass");
                return false;
            }
        } else {
            context.begin_pass(output);
        }
        context.set_viewport(0, 0, static_cast<int>(output_desc.width), static_cast<int>(output_desc.height));
        context.set_depth_test(false);
        context.set_depth_write(false);
        context.set_blend(false);
        context.set_cull(CullMode::None);
        context.bind_shader(context.fsq_vertex_shader(), fragment_shader);

        VertexLayoutDesc fsq_layout;
        fsq_layout.stride = 4 * sizeof(float);
        fsq_layout.attribute_count = 2;
        fsq_layout.attributes[0] = {0, VertexFormat::Float2, 0, intern_vertex_semantic("position")};
        fsq_layout.attributes[1] = {
            1, VertexFormat::Float2, 2 * sizeof(float), intern_vertex_semantic("uv")};
        context.set_vertex_layout(fsq_layout);

        context.use_shader_resource_layout(raw);
        context.bind_uniform_data("u_params", &gpu_params, sizeof(gpu_params));
        context.bind_texture("u_input", input);
        context.draw_fullscreen_quad();
        context.end_pass();
        return true;
    }

    void OutputTransformRenderer::close() {
        device_ = nullptr;
        shader_handle_ = tc_shader_handle_invalid();
        multiview_shader_handle_ = tc_shader_handle_invalid();
    }

} // namespace tgfx
