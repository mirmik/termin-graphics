#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
}

namespace {

bool compile_hlsl_to_file(
    const char* source,
    const char* profile,
    const std::filesystem::path& path)
{
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    HRESULT hr = D3DCompile(
        source,
        std::strlen(source),
        nullptr,
        nullptr,
        nullptr,
        "main",
        profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &blob,
        &errors);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "D3D11 smoke: D3DCompile(%s) failed: %s\n",
                     profile,
                     errors ? static_cast<const char*>(errors->GetBufferPointer()) : "<no diagnostics>");
        return false;
    }

    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        std::fprintf(stderr, "D3D11 smoke: failed to open %s\n", path.string().c_str());
        return false;
    }
    out.write(static_cast<const char*>(blob->GetBufferPointer()),
              static_cast<std::streamsize>(blob->GetBufferSize()));
    return static_cast<bool>(out);
}

} // namespace

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

        const char* shader_uuid = "d3d11-smoke-artifact";
        const auto artifact_root =
            std::filesystem::temp_directory_path() / "termin-tgfx2-d3d11-smoke-artifacts";
        const auto shader_dir = artifact_root / "shaders" / "d3d11";
        const auto vs_path = shader_dir / (std::string(shader_uuid) + ".vs.cso");
        const auto ps_path = shader_dir / (std::string(shader_uuid) + ".ps.cso");

        const char* vs_source =
            "struct VSOut { float4 pos : SV_Position; };\n"
            "VSOut main(uint vertex_id : SV_VertexID) {\n"
            "    float2 p[3] = { float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0) };\n"
            "    VSOut o;\n"
            "    o.pos = float4(p[vertex_id], 0.0, 1.0);\n"
            "    return o;\n"
            "}\n";
        const char* ps_source =
            "float4 main() : SV_Target0 {\n"
            "    return float4(0.90, 0.10, 0.20, 1.0);\n"
            "}\n";
        if (!compile_hlsl_to_file(vs_source, "vs_5_0", vs_path) ||
            !compile_hlsl_to_file(ps_source, "ps_5_0", ps_path)) {
            return 1;
        }

        termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(false);

        tc_shader_init();
        tc_shader_handle shader_handle = tc_shader_from_sources_with_entries_ex(
            vs_source,
            ps_source,
            nullptr,
            "D3D11 smoke artifact shader",
            nullptr,
            shader_uuid,
            TC_SHADER_LANGUAGE_HLSL,
            TC_SHADER_ARTIFACT_REQUIRED,
            "main",
            "main",
            nullptr);
        tc_shader* shader = tc_shader_get(shader_handle);
        if (!shader) {
            std::fprintf(stderr, "D3D11 smoke: failed to create tc_shader\n");
            tc_shader_shutdown();
            return 1;
        }

        tgfx::ShaderHandle vs;
        tgfx::ShaderHandle fs;
        if (!termin::tc_shader_ensure_tgfx2(shader, device.get(), &vs, &fs) || !vs || !fs) {
            std::fprintf(stderr, "D3D11 smoke: tc_shader_ensure_tgfx2 failed\n");
            tc_shader_shutdown();
            return 1;
        }

        tgfx::PipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = vs;
        pipeline_desc.fragment_shader = fs;
        pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
        pipeline_desc.color_formats.push_back(tgfx::PixelFormat::RGBA8_UNorm);
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        auto pipeline = device->create_pipeline(pipeline_desc);
        if (!pipeline) {
            std::fprintf(stderr, "D3D11 smoke: create_pipeline failed\n");
            tc_shader_shutdown();
            return 1;
        }

        attachment.clear_color[0] = 0.0f;
        attachment.clear_color[1] = 0.0f;
        attachment.clear_color[2] = 0.0f;
        attachment.clear_color[3] = 1.0f;
        pass.colors[0] = attachment;

        auto draw_cmd = device->create_command_list();
        draw_cmd->begin();
        draw_cmd->begin_render_pass(pass);
        draw_cmd->bind_pipeline(pipeline);
        draw_cmd->draw(3);
        draw_cmd->end_render_pass();
        draw_cmd->end();
        device->submit(*draw_cmd);

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: draw read_pixel_rgba8 failed\n");
            tc_shader_shutdown();
            return 1;
        }
        if (!close_enough(rgba[0], 0.90f) ||
            !close_enough(rgba[1], 0.10f) ||
            !close_enough(rgba[2], 0.20f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected drawn pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            tc_shader_shutdown();
            return 1;
        }

        device->destroy(pipeline);
        tc_shader_shutdown();
        device->destroy(color);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "D3D11 smoke: %s\n", e.what());
        return 77;
    }
}
