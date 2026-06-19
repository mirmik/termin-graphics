#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"

#include <cmath>
#include <cstdio>
#include <exception>

int main() {
    try {
        auto device = tgfx::create_device(tgfx::BackendType::D3D11);

        tgfx::TextureDesc color_desc;
        color_desc.width = 4;
        color_desc.height = 4;
        color_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        color_desc.usage = tgfx::TextureUsage::ColorAttachment |
                           tgfx::TextureUsage::Sampled |
                           tgfx::TextureUsage::CopySrc;
        auto color = device->create_texture(color_desc);
        if (!color) {
            std::fprintf(stderr, "D3D11 smoke: create_texture returned empty handle\n");
            return 1;
        }

        tgfx::RenderPassDesc pass;
        tgfx::ColorAttachmentDesc attachment;
        attachment.texture = color;
        attachment.load = tgfx::LoadOp::Clear;
        attachment.clear_color[0] = 0.25f;
        attachment.clear_color[1] = 0.50f;
        attachment.clear_color[2] = 0.75f;
        attachment.clear_color[3] = 1.00f;
        pass.colors.push_back(attachment);

        auto cmd = device->create_command_list();
        cmd->begin();
        cmd->begin_render_pass(pass);
        cmd->end_render_pass();
        cmd->end();
        device->submit(*cmd);

        float rgba[4] = {};
        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: read_pixel_rgba8 failed\n");
            return 1;
        }

        auto close_enough = [](float a, float b) {
            return std::fabs(a - b) < 0.02f;
        };
        if (!close_enough(rgba[0], 0.25f) ||
            !close_enough(rgba[1], 0.50f) ||
            !close_enough(rgba[2], 0.75f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        device->destroy(color);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "D3D11 smoke: %s\n", e.what());
        return 77;
    }
}
