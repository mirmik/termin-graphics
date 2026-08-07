#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"

namespace tgfx::tests {

    template <typename Draw> bool run_ordered_mrt_smoke(IRenderDevice& device, std::string_view backend, Draw&& draw) {
        constexpr uint32_t kWidth = 16;
        constexpr uint32_t kHeight = 16;

        TextureDesc color_desc;
        color_desc.width = kWidth;
        color_desc.height = kHeight;
        color_desc.format = PixelFormat::RGBA8_UNorm;
        color_desc.usage = TextureUsage::ColorAttachment | TextureUsage::CopySrc;
        std::array<TextureHandle, 3> targets = {
            device.create_texture(color_desc),
            device.create_texture(color_desc),
            device.create_texture(color_desc),
        };

        TextureDesc depth_desc;
        depth_desc.width = kWidth;
        depth_desc.height = kHeight;
        depth_desc.format = PixelFormat::D32F;
        depth_desc.usage = TextureUsage::DepthStencilAttachment | TextureUsage::CopySrc;
        const TextureHandle depth = device.create_texture(depth_desc);
        auto release_attachments = [&]() {
            if (depth) {
                device.destroy(depth);
            }
            for (const TextureHandle target : targets) {
                if (target) {
                    device.destroy(target);
                }
            }
        };
        if (!targets[0] || !targets[1] || !targets[2] || !depth) {
            std::fprintf(stderr,
                         "%.*s MRT smoke: attachment creation failed\n",
                         static_cast<int>(backend.size()),
                         backend.data());
            release_attachments();
            return false;
        }

        RenderPassDesc pass;
        pass.colors.resize(targets.size());
        const float clear_colors[3][4] = {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };
        for (size_t i = 0; i < targets.size(); ++i) {
            pass.colors[i].texture = targets[i];
            pass.colors[i].load = LoadOp::Clear;
            pass.colors[i].store = StoreOp::Store;
            std::memcpy(pass.colors[i].clear_color, clear_colors[i], sizeof(clear_colors[i]));
        }
        pass.has_depth = true;
        pass.depth.texture = depth;
        pass.depth.load = LoadOp::Clear;
        pass.depth.store = StoreOp::Store;
        pass.depth.clear_depth = 0.0f;

        PipelineCache cache(device);
        RenderContext2 context(device, cache);
        auto run_pass = [&](bool issue_draw, bool depth_test) {
            context.begin_frame();
            if (!context.begin_pass(pass)) {
                context.end_frame();
                return false;
            }
            if (issue_draw) {
                context.set_depth_test(depth_test);
                context.set_depth_write(false);
                context.set_depth_func(CompareOp::Less);
                context.set_blend(false);
                context.set_cull(CullMode::None);
                draw(context);
            }
            context.end_pass();
            context.end_frame();
            device.wait_idle();
            return true;
        };

        auto pixel_matches = [](const float pixel[4], float r, float g, float b) {
            constexpr float epsilon = 0.05f;
            return std::abs(pixel[0] - r) < epsilon && std::abs(pixel[1] - g) < epsilon &&
                   std::abs(pixel[2] - b) < epsilon && pixel[3] > 0.9f;
        };
        float pixels[3][4]{};
        auto read_expected = [&](const float expected[3][4]) {
            bool matches = true;
            for (size_t i = 0; i < targets.size(); ++i) {
                matches = device.read_pixel_rgba8(targets[i], kWidth / 2, kHeight / 2, pixels[i]) &&
                          pixel_matches(pixels[i], expected[i][0], expected[i][1], expected[i][2]) && matches;
            }
            return matches;
        };

        const bool clear_ok = run_pass(false, false) && read_expected(clear_colors);
        for (ColorAttachmentDesc& color : pass.colors) {
            color.load = LoadOp::Load;
        }
        pass.depth.load = LoadOp::Load;
        const bool preserved_loads = run_pass(true, true) && read_expected(clear_colors);

        const float draw_colors[3][4] = {
            {0.0f, 1.0f, 1.0f, 1.0f},
            {1.0f, 0.0f, 1.0f, 1.0f},
            {1.0f, 1.0f, 0.0f, 1.0f},
        };
        const bool draw_ok = run_pass(true, false) && read_expected(draw_colors);

        std::printf("%.*s ordered MRT clear: %s, shared-depth preserved load: %s, "
                    "SV_Target routing: %s\n",
                    static_cast<int>(backend.size()),
                    backend.data(),
                    clear_ok ? "ok" : "failed",
                    preserved_loads ? "ok" : "failed",
                    draw_ok ? "ok" : "failed");
        release_attachments();
        return clear_ok && preserved_loads && draw_ok;
    }

} // namespace tgfx::tests
