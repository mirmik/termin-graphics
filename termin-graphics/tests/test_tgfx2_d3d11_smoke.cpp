#include "tgfx2/backend_binding_plan.hpp"
#include "tgfx2/canvas2d_renderer.hpp"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tgfx2/vertex_layout.hpp"

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
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_mesh.h"
#include "tgfx/resources/tc_mesh_registry.h"
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"
}

namespace {

struct RegistryGuard {
    RegistryGuard() {
        tc_shader_init();
        tc_texture_init();
        tc_mesh_init();
    }
    ~RegistryGuard() {
        tc_mesh_shutdown();
        tc_texture_shutdown();
        tc_shader_shutdown();
    }
};

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

bool read_binary_file(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "D3D11 smoke: failed to open %s\n", path.string().c_str());
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.empty()) {
        std::fprintf(stderr, "D3D11 smoke: empty file %s\n", path.string().c_str());
        return false;
    }
    return true;
}

std::filesystem::path find_termin_shaderc() {
    if (const char* env = std::getenv("TERMIN_SHADERC")) {
        std::filesystem::path configured(env);
        if (std::filesystem::exists(configured)) {
            return configured;
        }
    }

#ifdef _WIN32
    constexpr const char* kToolName = "termin_shaderc.exe";
#else
    constexpr const char* kToolName = "termin_shaderc";
#endif

    std::filesystem::path cursor = std::filesystem::current_path();
    for (int depth = 0; depth < 6; ++depth) {
        std::filesystem::path candidate = cursor / "sdk" / "bin" / kToolName;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
            break;
        }
        cursor = cursor.parent_path();
    }
    return {};
}

std::filesystem::path find_on_path(const char* executable_name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env || !path_env[0]) {
        return {};
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
#else
    constexpr char kSeparator = ':';
#endif
    std::stringstream stream(path_env);
    std::string dir;
    while (std::getline(stream, dir, kSeparator)) {
        if (dir.empty()) {
            continue;
        }
        std::filesystem::path candidate = std::filesystem::path(dir) / executable_name;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::filesystem::path find_slangc() {
    if (const char* env = std::getenv("TERMIN_SLANGC")) {
        std::filesystem::path configured(env);
        if (std::filesystem::exists(configured)) {
            return configured;
        }
    }

#ifdef _WIN32
    if (auto from_path = find_on_path("slangc.exe"); !from_path.empty()) {
        return from_path;
    }
    const std::filesystem::path vulkan_root("C:/VulkanSDK");
    std::error_code ec;
    if (std::filesystem::exists(vulkan_root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(vulkan_root, ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::filesystem::path candidate = entry.path() / "Bin" / "slangc.exe";
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
    }
#else
    if (auto from_path = find_on_path("slangc"); !from_path.empty()) {
        return from_path;
    }
#endif
    return {};
}

std::filesystem::path find_text_smoke_font() {
    const std::filesystem::path recast_font =
        std::filesystem::current_path() /
        "termin-thirdparty" / "recastnavigation" / "RecastDemo" /
        "Bin" / "DroidSans.ttf";
    if (std::filesystem::exists(recast_font)) {
        return recast_font;
    }

#ifdef _WIN32
    const char* windir_env = std::getenv("WINDIR");
    const std::filesystem::path fonts_dir =
        (windir_env != nullptr && windir_env[0] != '\0')
            ? std::filesystem::path(windir_env) / "Fonts"
            : std::filesystem::path("C:/Windows/Fonts");
    for (const char* name : {"segoeui.ttf", "arial.ttf", "tahoma.ttf"}) {
        std::filesystem::path candidate = fonts_dir / name;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
#endif

    return {};
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

        device->clear_texture(color, 0.10f, 0.20f, 0.30f, 1.0f, 0, 0, 4, 4);
        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: clear_texture readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 0.10f) ||
            !close_enough(rgba[1], 0.20f) ||
            !close_enough(rgba[2], 0.30f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected clear_texture pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        tgfx::TextureDesc hdr_desc;
        hdr_desc.width = 4;
        hdr_desc.height = 4;
        hdr_desc.format = tgfx::PixelFormat::RGBA16F;
        hdr_desc.usage = tgfx::TextureUsage::ColorAttachment |
                         tgfx::TextureUsage::Sampled |
                         tgfx::TextureUsage::CopySrc;
        auto hdr_color = device->create_texture(hdr_desc);
        if (!hdr_color) {
            std::fprintf(stderr, "D3D11 smoke: RGBA16F target creation failed\n");
            return 1;
        }
        device->clear_texture(hdr_color, 1.25f, 0.50f, 0.125f, 1.0f, 0, 0, 4, 4);
        std::vector<float> hdr_readback(4 * 4 * 4, 0.0f);
        if (!device->read_texture_rgba_float(hdr_color, hdr_readback.data())) {
            std::fprintf(stderr, "D3D11 smoke: RGBA16F read_texture_rgba_float failed\n");
            device->destroy(hdr_color);
            return 1;
        }
        if (!close_enough(hdr_readback[0], 1.25f) ||
            !close_enough(hdr_readback[1], 0.50f) ||
            !close_enough(hdr_readback[2], 0.125f) ||
            !close_enough(hdr_readback[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected RGBA16F readback %.3f %.3f %.3f %.3f\n",
                         hdr_readback[0],
                         hdr_readback[1],
                         hdr_readback[2],
                         hdr_readback[3]);
            device->destroy(hdr_color);
            return 1;
        }
        device->destroy(hdr_color);

        tgfx::TextureDesc depth_desc;
        depth_desc.width = 4;
        depth_desc.height = 4;
        depth_desc.format = tgfx::PixelFormat::D32F;
        depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment |
                           tgfx::TextureUsage::Sampled |
                           tgfx::TextureUsage::CopySrc;
        auto depth_tex = device->create_texture(depth_desc);
        if (!depth_tex) {
            std::fprintf(stderr, "D3D11 smoke: D32F target creation failed\n");
            return 1;
        }
        tgfx::RenderPassDesc depth_pass;
        depth_pass.has_depth = true;
        depth_pass.depth.texture = depth_tex;
        depth_pass.depth.load = tgfx::LoadOp::Clear;
        depth_pass.depth.clear_depth = 0.42f;
        auto depth_cmd = device->create_command_list();
        depth_cmd->begin();
        depth_cmd->begin_render_pass(depth_pass);
        depth_cmd->end_render_pass();
        depth_cmd->end();
        device->submit(*depth_cmd);

        std::vector<float> depth_readback(4 * 4, 0.0f);
        if (!device->read_texture_depth_float(depth_tex, depth_readback.data())) {
            std::fprintf(stderr, "D3D11 smoke: D32F read_texture_depth_float failed\n");
            device->destroy(depth_tex);
            return 1;
        }
        if (!close_enough(depth_readback[0], 0.42f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected D32F readback %.3f\n",
                         depth_readback[0]);
            device->destroy(depth_tex);
            return 1;
        }
        device->destroy(depth_tex);

        tgfx::TextureDesc bgra_desc;
        bgra_desc.width = 4;
        bgra_desc.height = 4;
        bgra_desc.format = tgfx::PixelFormat::BGRA8_UNorm;
        bgra_desc.usage = tgfx::TextureUsage::ColorAttachment |
                          tgfx::TextureUsage::CopySrc |
                          tgfx::TextureUsage::CopyDst;
        auto bgra_target = device->create_texture(bgra_desc);
        if (!bgra_target) {
            std::fprintf(stderr, "D3D11 smoke: BGRA blit target creation failed\n");
            return 1;
        }
        device->blit_to_texture(bgra_target, color, 0, 0, 4, 4, 0, 0, 4, 4);
        if (!device->read_pixel_rgba8(bgra_target, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: RGBA->BGRA blit readback failed\n");
            device->destroy(bgra_target);
            return 1;
        }
        if (!close_enough(rgba[0], 0.10f) ||
            !close_enough(rgba[1], 0.20f) ||
            !close_enough(rgba[2], 0.30f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected RGBA->BGRA blit pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            device->destroy(bgra_target);
            return 1;
        }
        device->destroy(bgra_target);

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

        RegistryGuard registries;
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
            return 1;
        }

        tgfx::ShaderHandle vs;
        tgfx::ShaderHandle fs;
        if (!termin::tc_shader_ensure_tgfx2(shader, device.get(), &vs, &fs) || !vs || !fs) {
            std::fprintf(stderr, "D3D11 smoke: tc_shader_ensure_tgfx2 failed\n");
            return 1;
        }

        tgfx::PipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = vs;
        pipeline_desc.fragment_shader = fs;
        pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
        pipeline_desc.color_formats.push_back(tgfx::PixelFormat::RGBA8_UNorm);
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.raster.cull = tgfx::CullMode::None;
        auto pipeline = device->create_pipeline(pipeline_desc);
        if (!pipeline) {
            std::fprintf(stderr, "D3D11 smoke: create_pipeline failed\n");
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
            return 1;
        }
        if (!close_enough(rgba[0], 0.90f) ||
            !close_enough(rgba[1], 0.10f) ||
            !close_enough(rgba[2], 0.20f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected drawn pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        const char* textured_shader_uuid = "d3d11-smoke-tc-resources";
        const auto textured_vs_path = shader_dir / (std::string(textured_shader_uuid) + ".vs.cso");
        const auto textured_ps_path = shader_dir / (std::string(textured_shader_uuid) + ".ps.cso");
        const char* textured_vs_source =
            "struct VSIn { float3 position : POSITION; };\n"
            "struct VSOut { float4 pos : SV_Position; };\n"
            "VSOut main(VSIn input) {\n"
            "    VSOut o;\n"
            "    o.pos = float4(input.position, 1.0);\n"
            "    return o;\n"
            "}\n";
        const char* textured_ps_source =
            "Texture2D albedo_texture : register(t0);\n"
            "SamplerState albedo_sampler : register(s0);\n"
            "float4 main() : SV_Target0 {\n"
            "    return albedo_texture.Sample(albedo_sampler, float2(0.5, 0.5));\n"
            "}\n";
        if (!compile_hlsl_to_file(textured_vs_source, "vs_5_0", textured_vs_path) ||
            !compile_hlsl_to_file(textured_ps_source, "ps_5_0", textured_ps_path)) {
            return 1;
        }

        tc_shader_handle textured_shader_handle = tc_shader_from_sources_with_entries_ex(
            textured_vs_source,
            textured_ps_source,
            nullptr,
            "D3D11 smoke tc resource shader",
            nullptr,
            textured_shader_uuid,
            TC_SHADER_LANGUAGE_HLSL,
            TC_SHADER_ARTIFACT_REQUIRED,
            "main",
            "main",
            nullptr);
        tc_shader* textured_shader = tc_shader_get(textured_shader_handle);
        if (!textured_shader) {
            std::fprintf(stderr, "D3D11 smoke: failed to create textured tc_shader\n");
            return 1;
        }

        tgfx::ShaderHandle textured_vs;
        tgfx::ShaderHandle textured_fs;
        if (!termin::tc_shader_ensure_tgfx2(
                textured_shader,
                device.get(),
                &textured_vs,
                &textured_fs) ||
            !textured_vs || !textured_fs) {
            std::fprintf(stderr, "D3D11 smoke: textured tc_shader_ensure_tgfx2 failed\n");
            return 1;
        }

        tc_texture_handle texture_handle = tc_texture_create("d3d11-smoke-texture");
        tc_texture* texture = tc_texture_get(texture_handle);
        const uint8_t texture_pixels[] = {
            13, 191, 64, 255, 13, 191, 64, 255,
            13, 191, 64, 255, 13, 191, 64, 255,
        };
        if (!texture ||
            !tc_texture_set_data(
                texture,
                texture_pixels,
                2,
                2,
                4,
                "D3D11 smoke texture",
                nullptr)) {
            std::fprintf(stderr, "D3D11 smoke: failed to create tc_texture\n");
            return 1;
        }
        auto texture_gpu = device->ensure_tc_texture(texture);
        auto texture_gpu_again = device->ensure_tc_texture(texture);
        if (!texture_gpu || texture_gpu != texture_gpu_again) {
            std::fprintf(stderr, "D3D11 smoke: ensure_tc_texture failed or did not cache\n");
            return 1;
        }

        struct SmokeVertex {
            float position[3];
        };
        const SmokeVertex vertices[] = {
            {{-1.0f, -1.0f, 0.0f}},
            {{-1.0f,  3.0f, 0.0f}},
            {{ 3.0f, -1.0f, 0.0f}},
        };
        const uint32_t indices[] = {0, 1, 2};
        tc_vertex_layout tc_layout;
        tc_vertex_layout_init(&tc_layout);
        if (!tc_vertex_layout_add(&tc_layout, "position", 3, TC_ATTRIB_FLOAT32, 0)) {
            std::fprintf(stderr, "D3D11 smoke: failed to build tc vertex layout\n");
            return 1;
        }

        tc_mesh_handle mesh_handle = tc_mesh_create("d3d11-smoke-mesh");
        tc_mesh* mesh = tc_mesh_get(mesh_handle);
        if (!mesh ||
            !tc_mesh_set_data(
                mesh,
                vertices,
                3,
                &tc_layout,
                indices,
                3,
                "D3D11 smoke mesh")) {
            std::fprintf(stderr, "D3D11 smoke: failed to create tc_mesh\n");
            return 1;
        }
        auto mesh_gpu = device->ensure_tc_mesh(mesh);
        auto mesh_gpu_again = device->ensure_tc_mesh(mesh);
        if (!mesh_gpu.first || !mesh_gpu.second ||
            mesh_gpu.first != mesh_gpu_again.first ||
            mesh_gpu.second != mesh_gpu_again.second) {
            std::fprintf(stderr, "D3D11 smoke: ensure_tc_mesh failed or did not cache\n");
            return 1;
        }

        tgfx::PipelineDesc textured_pipeline_desc;
        textured_pipeline_desc.vertex_shader = textured_vs;
        textured_pipeline_desc.fragment_shader = textured_fs;
        textured_pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
        textured_pipeline_desc.color_formats.push_back(tgfx::PixelFormat::RGBA8_UNorm);
        textured_pipeline_desc.depth_stencil.depth_test = false;
        textured_pipeline_desc.depth_stencil.depth_write = false;
        textured_pipeline_desc.raster.cull = tgfx::CullMode::None;
        tgfx::VertexBufferLayout vertex_layout;
        vertex_layout.stride = sizeof(SmokeVertex);
        vertex_layout.attributes.emplace_back(
            0,
            tgfx::VertexFormat::Float3,
            0,
            "POSITION");
        textured_pipeline_desc.vertex_layouts.push_back(vertex_layout);
        auto textured_pipeline = device->create_pipeline(textured_pipeline_desc);
        if (!textured_pipeline) {
            std::fprintf(stderr, "D3D11 smoke: textured create_pipeline failed\n");
            return 1;
        }

        tgfx::PipelineDesc standard_location_desc = textured_pipeline_desc;
        standard_location_desc.vertex_layouts.clear();
        tgfx::VertexBufferLayout standard_location_layout;
        standard_location_layout.stride = sizeof(SmokeVertex);
        standard_location_layout.attributes.emplace_back(
            0,
            tgfx::VertexFormat::Float3,
            0);
        standard_location_desc.vertex_layouts.push_back(standard_location_layout);
        if (!device->create_pipeline(standard_location_desc)) {
            std::fprintf(stderr, "D3D11 smoke: standard location input layout failed\n");
            return 1;
        }

        auto sampler = device->create_sampler(tgfx::SamplerDesc{});
        tgfx::ResourceSetDesc resource_set_desc;
        tgfx::ResourceBinding sampled_texture;
        sampled_texture.kind = tgfx::ResourceBinding::Kind::SampledTexture;
        sampled_texture.binding = 0;
        sampled_texture.texture = texture_gpu;
        sampled_texture.sampler = sampler;
        resource_set_desc.bindings.push_back(sampled_texture);
        auto resource_set = device->create_resource_set(resource_set_desc);
        if (!sampler || !resource_set) {
            std::fprintf(stderr, "D3D11 smoke: failed to create sampler/resource set\n");
            return 1;
        }

        attachment.clear_color[0] = 0.0f;
        attachment.clear_color[1] = 0.0f;
        attachment.clear_color[2] = 0.0f;
        attachment.clear_color[3] = 1.0f;
        pass.colors[0] = attachment;
        auto tc_draw_cmd = device->create_command_list();
        tc_draw_cmd->begin();
        tc_draw_cmd->begin_render_pass(pass);
        tc_draw_cmd->bind_pipeline(textured_pipeline);
        tc_draw_cmd->bind_resource_set(resource_set);
        tc_draw_cmd->bind_vertex_buffer(0, mesh_gpu.first);
        tc_draw_cmd->bind_index_buffer(mesh_gpu.second, tgfx::IndexType::Uint32);
        tc_draw_cmd->draw_indexed(3);
        tc_draw_cmd->end_render_pass();
        tc_draw_cmd->end();
        device->submit(*tc_draw_cmd);

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: tc resource draw readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 13.0f / 255.0f) ||
            !close_enough(rgba[1], 191.0f / 255.0f) ||
            !close_enough(rgba[2], 64.0f / 255.0f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected tc resource pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        const auto normal_vs_path = shader_dir / "d3d11-smoke-normal-material.vs.cso";
        const auto normal_ps_path = shader_dir / "d3d11-smoke-normal-material.ps.cso";
        const char* normal_vs_source =
            "cbuffer PerFrame : register(b0) { float4x4 view_proj; };\n"
            "cbuffer DrawData : register(b1) { float4x4 model; };\n"
            "struct VSIn { float3 position : POSITION; float3 normal : NORMAL; };\n"
            "struct VSOut { float4 pos : SV_Position; float3 normal_world : NORMAL; };\n"
            "VSOut main(VSIn input) {\n"
            "    VSOut o;\n"
            "    float4 world = mul(model, float4(input.position, 1.0));\n"
            "    o.pos = mul(view_proj, world);\n"
            "    o.normal_world = input.normal;\n"
            "    return o;\n"
            "}\n";
        const char* normal_ps_source =
            "struct VSOut { float4 pos : SV_Position; float3 normal_world : NORMAL; };\n"
            "float4 main(VSOut input) : SV_Target0 {\n"
            "    float3 n = normalize(input.normal_world);\n"
            "    return float4(n * 0.5 + 0.5, 1.0);\n"
            "}\n";
        if (!compile_hlsl_to_file(normal_vs_source, "vs_5_0", normal_vs_path) ||
            !compile_hlsl_to_file(normal_ps_source, "ps_5_0", normal_ps_path)) {
            return 1;
        }

        std::vector<uint8_t> normal_vs_bytecode;
        std::vector<uint8_t> normal_ps_bytecode;
        if (!read_binary_file(normal_vs_path, normal_vs_bytecode) ||
            !read_binary_file(normal_ps_path, normal_ps_bytecode)) {
            return 1;
        }
        tgfx::ShaderDesc normal_vs_desc;
        normal_vs_desc.stage = tgfx::ShaderStage::Vertex;
        normal_vs_desc.debug_name = "D3D11 smoke normal material VS";
        normal_vs_desc.bytecode = std::move(normal_vs_bytecode);
        auto normal_vs = device->create_shader(normal_vs_desc);
        tgfx::ShaderDesc normal_ps_desc;
        normal_ps_desc.stage = tgfx::ShaderStage::Fragment;
        normal_ps_desc.debug_name = "D3D11 smoke normal material PS";
        normal_ps_desc.bytecode = std::move(normal_ps_bytecode);
        auto normal_fs = device->create_shader(normal_ps_desc);
        if (!normal_vs || !normal_fs) {
            std::fprintf(stderr, "D3D11 smoke: normal material shader creation failed\n");
            return 1;
        }

        struct NormalVertex {
            float position[3];
            float normal[3];
        };
        const NormalVertex normal_vertices[] = {
            {{-1.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-1.0f,  3.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 3.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        };
        tgfx::BufferDesc normal_vbo_desc;
        normal_vbo_desc.size = sizeof(normal_vertices);
        normal_vbo_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
        auto normal_vbo = device->create_buffer(normal_vbo_desc);
        device->upload_buffer(
            normal_vbo,
            std::span<const uint8_t>(
                reinterpret_cast<const uint8_t*>(normal_vertices),
                sizeof(normal_vertices)));

        float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        tgfx::BufferDesc normal_cb_desc;
        normal_cb_desc.size = sizeof(identity);
        normal_cb_desc.usage = tgfx::BufferUsage::Uniform | tgfx::BufferUsage::CopyDst;
        auto per_frame_cb = device->create_buffer(normal_cb_desc);
        auto draw_data_cb = device->create_buffer(normal_cb_desc);
        device->upload_buffer(
            per_frame_cb,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(identity), sizeof(identity)));
        device->upload_buffer(
            draw_data_cb,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(identity), sizeof(identity)));

        tgfx::PipelineDesc normal_pipeline_desc;
        normal_pipeline_desc.vertex_shader = normal_vs;
        normal_pipeline_desc.fragment_shader = normal_fs;
        normal_pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
        normal_pipeline_desc.color_formats.push_back(tgfx::PixelFormat::RGBA8_UNorm);
        normal_pipeline_desc.depth_stencil.depth_test = false;
        normal_pipeline_desc.depth_stencil.depth_write = false;
        normal_pipeline_desc.raster.cull = tgfx::CullMode::None;
        tgfx::VertexBufferLayout normal_layout;
        normal_layout.stride = sizeof(NormalVertex);
        normal_layout.attributes.emplace_back(
            0,
            tgfx::VertexFormat::Float3,
            static_cast<uint32_t>(offsetof(NormalVertex, position)),
            "position");
        normal_layout.attributes.emplace_back(
            1,
            tgfx::VertexFormat::Float3,
            static_cast<uint32_t>(offsetof(NormalVertex, normal)),
            "normal");
        normal_pipeline_desc.vertex_layouts.push_back(normal_layout);
        auto normal_pipeline = device->create_pipeline(normal_pipeline_desc);
        if (!normal_pipeline) {
            std::fprintf(stderr, "D3D11 smoke: normal material pipeline creation failed\n");
            return 1;
        }

        tgfx::BackendBindingPlanEntry per_frame_plan;
        per_frame_plan.resource.name = "PerFrame";
        per_frame_plan.resource.kind = tgfx::ShaderResourceKind::ConstantBuffer;
        per_frame_plan.resource.scope = tgfx::ShaderResourceScope::Frame;
        per_frame_plan.stage_mask = TC_SHADER_STAGE_VERTEX;
        per_frame_plan.size = sizeof(identity);
        per_frame_plan.placement.kind = tgfx::BackendPlacementKind::D3D11Register;
        per_frame_plan.placement.d3d11.register_class = tgfx::D3D11RegisterClass::B;
        per_frame_plan.placement.d3d11.register_index = 0;

        tgfx::BoundResourceValue per_frame_value;
        per_frame_value.kind = tgfx::BoundResourceKind::UniformBuffer;
        per_frame_value.buffer = per_frame_cb;
        per_frame_value.range = sizeof(identity);

        tgfx::BackendBindingPlanEntry draw_data_plan;
        draw_data_plan.resource.name = "DrawData";
        draw_data_plan.resource.kind = tgfx::ShaderResourceKind::ConstantBuffer;
        draw_data_plan.resource.scope = tgfx::ShaderResourceScope::Draw;
        draw_data_plan.stage_mask = TC_SHADER_STAGE_VERTEX;
        draw_data_plan.size = sizeof(identity);
        draw_data_plan.placement.kind = tgfx::BackendPlacementKind::D3D11Register;
        draw_data_plan.placement.d3d11.register_class = tgfx::D3D11RegisterClass::B;
        draw_data_plan.placement.d3d11.register_index = 1;

        tgfx::BoundResourceValue draw_data_value;
        draw_data_value.kind = tgfx::BoundResourceKind::UniformBuffer;
        draw_data_value.buffer = draw_data_cb;
        draw_data_value.range = sizeof(identity);

        tgfx::BoundResourceSetDesc normal_resource_set_desc;
        normal_resource_set_desc.resource_layout_token =
            device->pipeline_resource_layout_token(normal_pipeline);
        tgfx::BoundResourceGroup frame_group;
        frame_group.scope = tgfx::ShaderResourceScope::Frame;
        frame_group.bindings.push_back({per_frame_plan, per_frame_value});
        normal_resource_set_desc.groups.push_back(std::move(frame_group));
        tgfx::BoundResourceGroup draw_group;
        draw_group.scope = tgfx::ShaderResourceScope::Draw;
        draw_group.bindings.push_back({draw_data_plan, draw_data_value});
        normal_resource_set_desc.groups.push_back(std::move(draw_group));
        auto normal_resource_set = device->create_bound_resource_set(normal_resource_set_desc);
        if (!normal_vbo || !per_frame_cb || !draw_data_cb || !normal_resource_set) {
            std::fprintf(stderr, "D3D11 smoke: normal material resources failed\n");
            return 1;
        }

        auto normal_cmd = device->create_command_list();
        normal_cmd->begin();
        normal_cmd->begin_render_pass(pass);
        normal_cmd->bind_pipeline(normal_pipeline);
        normal_cmd->bind_resource_set(normal_resource_set);
        normal_cmd->bind_vertex_buffer(0, normal_vbo);
        normal_cmd->draw(3);
        normal_cmd->end_render_pass();
        normal_cmd->end();
        device->submit(*normal_cmd);

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: normal material readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 0.5f) ||
            !close_enough(rgba[1], 1.0f) ||
            !close_enough(rgba[2], 0.5f) ||
            !close_enough(rgba[3], 1.0f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected normal material pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        tgfx::TextureDesc normal_msaa_desc;
        normal_msaa_desc.width = 4;
        normal_msaa_desc.height = 4;
        normal_msaa_desc.format = tgfx::PixelFormat::RGBA16F;
        normal_msaa_desc.sample_count = 4;
        normal_msaa_desc.usage = tgfx::TextureUsage::ColorAttachment |
                                 tgfx::TextureUsage::Sampled |
                                 tgfx::TextureUsage::CopySrc;
        auto normal_msaa_color = device->create_texture(normal_msaa_desc);
        if (!normal_msaa_color) {
            std::fprintf(stderr, "D3D11 smoke: normal material MSAA target creation failed\n");
            return 1;
        }

        tgfx::PipelineDesc normal_msaa_pipeline_desc = normal_pipeline_desc;
        normal_msaa_pipeline_desc.color_formats.clear();
        normal_msaa_pipeline_desc.color_formats.push_back(tgfx::PixelFormat::RGBA16F);
        normal_msaa_pipeline_desc.sample_count = 4;
        auto normal_msaa_pipeline = device->create_pipeline(normal_msaa_pipeline_desc);
        if (!normal_msaa_pipeline) {
            std::fprintf(stderr, "D3D11 smoke: normal material MSAA pipeline creation failed\n");
            return 1;
        }

        tgfx::RenderPassDesc normal_msaa_pass;
        tgfx::ColorAttachmentDesc normal_msaa_attachment;
        normal_msaa_attachment.texture = normal_msaa_color;
        normal_msaa_attachment.load = tgfx::LoadOp::Clear;
        normal_msaa_attachment.clear_color[0] = 0.0f;
        normal_msaa_attachment.clear_color[1] = 0.0f;
        normal_msaa_attachment.clear_color[2] = 0.0f;
        normal_msaa_attachment.clear_color[3] = 1.0f;
        normal_msaa_pass.colors.push_back(normal_msaa_attachment);

        auto normal_msaa_cmd = device->create_command_list();
        normal_msaa_cmd->begin();
        normal_msaa_cmd->begin_render_pass(normal_msaa_pass);
        normal_msaa_cmd->bind_pipeline(normal_msaa_pipeline);
        normal_msaa_cmd->bind_resource_set(normal_resource_set);
        normal_msaa_cmd->bind_vertex_buffer(0, normal_vbo);
        normal_msaa_cmd->draw(3);
        normal_msaa_cmd->end_render_pass();
        normal_msaa_cmd->end();
        device->submit(*normal_msaa_cmd);

        std::vector<float> normal_msaa_readback(4 * 4 * 4, 0.0f);
        if (!device->read_texture_rgba_float(
                normal_msaa_color,
                normal_msaa_readback.data())) {
            std::fprintf(stderr, "D3D11 smoke: normal material MSAA readback failed\n");
            return 1;
        }
        if (!close_enough(normal_msaa_readback[0], 0.5f) ||
            !close_enough(normal_msaa_readback[1], 1.0f) ||
            !close_enough(normal_msaa_readback[2], 0.5f) ||
            !close_enough(normal_msaa_readback[3], 1.0f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected normal material MSAA pixel %.3f %.3f %.3f %.3f\n",
                         normal_msaa_readback[0],
                         normal_msaa_readback[1],
                         normal_msaa_readback[2],
                         normal_msaa_readback[3]);
            return 1;
        }

        const auto& fsq_shader = tgfx::engine_fullscreen_quad_vertex_shader();
        const auto fsq_vs_path = shader_dir / (std::string(fsq_shader.uuid) + ".vs.cso");
        const auto render_context_ps_path = shader_dir / "d3d11-smoke-render-context.ps.cso";
        const char* fsq_vs_source =
            "struct VSIn { float2 position : POSITION; float2 uv : TEXCOORD0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut main(VSIn input) {\n"
            "    VSOut o;\n"
            "    o.pos = float4(input.position, 0.0, 1.0);\n"
            "    o.uv = input.uv;\n"
            "    return o;\n"
            "}\n";
        const char* render_context_ps_source =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "float4 main(VSOut input) : SV_Target0 {\n"
            "    return float4(0.20, 0.70, 0.35, 1.0);\n"
            "}\n";
        if (!compile_hlsl_to_file(fsq_vs_source, "vs_5_0", fsq_vs_path) ||
            !compile_hlsl_to_file(render_context_ps_source, "ps_5_0", render_context_ps_path)) {
            return 1;
        }

        std::vector<uint8_t> render_context_ps_bytecode;
        if (!read_binary_file(render_context_ps_path, render_context_ps_bytecode)) {
            return 1;
        }
        tgfx::ShaderDesc render_context_ps_desc;
        render_context_ps_desc.stage = tgfx::ShaderStage::Fragment;
        render_context_ps_desc.debug_name = "D3D11 smoke RenderContext2 FS";
        render_context_ps_desc.bytecode = std::move(render_context_ps_bytecode);
        auto render_context_fs = device->create_shader(render_context_ps_desc);
        if (!render_context_fs) {
            std::fprintf(stderr, "D3D11 smoke: RenderContext2 fragment shader creation failed\n");
            return 1;
        }

        tgfx::PipelineCache pipeline_cache(*device);
        tgfx::RenderContext2 ctx(*device, pipeline_cache);
        const float context_clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
        auto fsq_vs = ctx.fsq_vertex_shader();
        if (!fsq_vs) {
            std::fprintf(stderr, "D3D11 smoke: RenderContext2 fullscreen quad VS failed\n");
            return 1;
        }
        ctx.begin_frame();
        ctx.begin_pass(color, {}, context_clear, 1.0f, false);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.set_blend(false);
        ctx.bind_shader(fsq_vs, render_context_fs);
        ctx.draw_fullscreen_quad_with_bound_shader();
        ctx.end_pass();
        ctx.end_frame();

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: RenderContext2 draw readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 0.20f) ||
            !close_enough(rgba[1], 0.70f) ||
            !close_enough(rgba[2], 0.35f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected RenderContext2 pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        ctx.begin_frame();
        ctx.begin_pass(color, {}, context_clear, 1.0f, false);
        ctx.set_depth_test(true);
        ctx.set_depth_write(true);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.set_blend(false);
        ctx.bind_shader(fsq_vs, render_context_fs);
        ctx.draw_fullscreen_quad_with_bound_shader();
        ctx.end_pass();
        ctx.end_frame();

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: RenderContext2 color-only depth-state readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 0.20f) ||
            !close_enough(rgba[1], 0.70f) ||
            !close_enough(rgba[2], 0.35f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: color-only pass with stale depth state did not draw %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        const auto fsq_uv_ps_path = shader_dir / "d3d11-smoke-fsq-uv.ps.cso";
        const char* fsq_uv_ps_source =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "float4 main(VSOut input) : SV_Target0 {\n"
            "    return float4(input.uv.x, input.uv.y, 0.0, 1.0);\n"
            "}\n";
        if (!compile_hlsl_to_file(fsq_uv_ps_source, "ps_5_0", fsq_uv_ps_path)) {
            return 1;
        }
        std::vector<uint8_t> fsq_uv_ps_bytecode;
        if (!read_binary_file(fsq_uv_ps_path, fsq_uv_ps_bytecode)) {
            return 1;
        }
        tgfx::ShaderDesc fsq_uv_ps_desc;
        fsq_uv_ps_desc.stage = tgfx::ShaderStage::Fragment;
        fsq_uv_ps_desc.debug_name = "D3D11 smoke FSQ UV FS";
        fsq_uv_ps_desc.bytecode = std::move(fsq_uv_ps_bytecode);
        auto fsq_uv_fs = device->create_shader(fsq_uv_ps_desc);
        if (!fsq_uv_fs) {
            std::fprintf(stderr, "D3D11 smoke: FSQ UV fragment shader creation failed\n");
            return 1;
        }

        ctx.begin_frame();
        ctx.begin_pass(color, {}, context_clear, 1.0f, false);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.set_blend(false);
        ctx.bind_shader(fsq_vs, fsq_uv_fs);
        ctx.draw_fullscreen_quad_with_bound_shader();
        ctx.end_pass();
        ctx.end_frame();

        float top_rgba[4] = {};
        float bottom_rgba[4] = {};
        if (!device->read_pixel_rgba8(color, 2, 0, top_rgba) ||
            !device->read_pixel_rgba8(color, 2, 3, bottom_rgba)) {
            std::fprintf(stderr, "D3D11 smoke: FSQ UV readback failed\n");
            return 1;
        }
        if (!(top_rgba[1] < 0.25f && bottom_rgba[1] > 0.75f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: fullscreen quad UV Y is inverted "
                         "(top=%.3f bottom=%.3f)\n",
                         top_rgba[1],
                         bottom_rgba[1]);
            return 1;
        }

        const auto push_constants_ps_path = shader_dir / "d3d11-smoke-push-constants.ps.cso";
        const char* push_constants_ps_source =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "cbuffer PushConstants : register(b13) {\n"
            "    float4 u_color;\n"
            "};\n"
            "float4 main(VSOut input) : SV_Target0 {\n"
            "    return u_color;\n"
            "}\n";
        if (!compile_hlsl_to_file(push_constants_ps_source, "ps_5_0", push_constants_ps_path)) {
            return 1;
        }
        std::vector<uint8_t> push_constants_ps_bytecode;
        if (!read_binary_file(push_constants_ps_path, push_constants_ps_bytecode)) {
            return 1;
        }
        tgfx::ShaderDesc push_constants_ps_desc;
        push_constants_ps_desc.stage = tgfx::ShaderStage::Fragment;
        push_constants_ps_desc.debug_name = "D3D11 smoke push constants FS";
        push_constants_ps_desc.bytecode = std::move(push_constants_ps_bytecode);
        auto push_constants_fs = device->create_shader(push_constants_ps_desc);
        if (!push_constants_fs) {
            std::fprintf(stderr, "D3D11 smoke: push constants fragment shader creation failed\n");
            return 1;
        }

        const std::array<float, 4> push_color = {0.62f, 0.18f, 0.73f, 1.0f};
        ctx.begin_frame();
        ctx.begin_pass(color, {}, context_clear, 1.0f, false);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.set_blend(false);
        ctx.bind_shader(fsq_vs, push_constants_fs);
        ctx.set_push_constants(push_color.data(), static_cast<uint32_t>(push_color.size() * sizeof(float)));
        ctx.draw_fullscreen_quad_with_bound_shader();
        ctx.end_pass();
        ctx.end_frame();

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: push constants draw readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], push_color[0]) ||
            !close_enough(rgba[1], push_color[1]) ||
            !close_enough(rgba[2], push_color[2]) ||
            !close_enough(rgba[3], push_color[3])) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected push constants pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        const auto reflected_input_vs_path = shader_dir / "d3d11-smoke-reflected-input.vs.cso";
        const auto reflected_input_ps_path = shader_dir / "d3d11-smoke-reflected-input.ps.cso";
        const char* reflected_input_vs_source =
            "struct VSIn { float3 pos : POSITION; float4 uv_pad : TEXCOORD0; };\n"
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "VSOut main(VSIn input) {\n"
            "    VSOut o;\n"
            "    o.pos = float4(input.pos, 1.0);\n"
            "    o.uv = input.uv_pad.xy;\n"
            "    return o;\n"
            "}\n";
        const char* reflected_input_ps_source =
            "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
            "float4 main(VSOut input) : SV_Target0 {\n"
            "    return float4(0.31, 0.19, 0.25, 1.0);\n"
            "}\n";
        if (!compile_hlsl_to_file(reflected_input_vs_source, "vs_5_0", reflected_input_vs_path) ||
            !compile_hlsl_to_file(reflected_input_ps_source, "ps_5_0", reflected_input_ps_path)) {
            return 1;
        }

        std::vector<uint8_t> reflected_input_vs_bytecode;
        std::vector<uint8_t> reflected_input_ps_bytecode;
        if (!read_binary_file(reflected_input_vs_path, reflected_input_vs_bytecode) ||
            !read_binary_file(reflected_input_ps_path, reflected_input_ps_bytecode)) {
            return 1;
        }
        tgfx::ShaderDesc reflected_input_vs_desc;
        reflected_input_vs_desc.stage = tgfx::ShaderStage::Vertex;
        reflected_input_vs_desc.debug_name = "D3D11 smoke reflected input VS";
        reflected_input_vs_desc.bytecode = std::move(reflected_input_vs_bytecode);
        auto reflected_input_vs = device->create_shader(reflected_input_vs_desc);
        tgfx::ShaderDesc reflected_input_ps_desc;
        reflected_input_ps_desc.stage = tgfx::ShaderStage::Fragment;
        reflected_input_ps_desc.debug_name = "D3D11 smoke reflected input PS";
        reflected_input_ps_desc.bytecode = std::move(reflected_input_ps_bytecode);
        auto reflected_input_fs = device->create_shader(reflected_input_ps_desc);
        if (!reflected_input_vs || !reflected_input_fs) {
            std::fprintf(stderr, "D3D11 smoke: reflected input shader creation failed\n");
            return 1;
        }

        tgfx::PipelineDesc logical_semantic_desc;
        logical_semantic_desc.vertex_shader = reflected_input_vs;
        logical_semantic_desc.fragment_shader = reflected_input_fs;
        logical_semantic_desc.depth_format = tgfx::PixelFormat::Undefined;
        logical_semantic_desc.color_formats.push_back(tgfx::PixelFormat::RGBA8_UNorm);
        logical_semantic_desc.depth_stencil.depth_test = false;
        logical_semantic_desc.depth_stencil.depth_write = false;
        logical_semantic_desc.raster.cull = tgfx::CullMode::None;
        tgfx::VertexBufferLayout logical_semantic_layout;
        logical_semantic_layout.stride = 7 * sizeof(float);
        logical_semantic_layout.attributes = {
            {0, tgfx::VertexFormat::Float3, 0, "position"},
            {1, tgfx::VertexFormat::Float4, 3 * sizeof(float), "uv"},
        };
        logical_semantic_desc.vertex_layouts.push_back(logical_semantic_layout);
        if (!device->create_pipeline(logical_semantic_desc)) {
            std::fprintf(stderr, "D3D11 smoke: logical semantic input layout failed\n");
            return 1;
        }

        tgfx::PipelineDesc reflected_semantic_desc = logical_semantic_desc;
        reflected_semantic_desc.vertex_layouts.clear();
        tgfx::VertexBufferLayout reflected_semantic_layout;
        reflected_semantic_layout.stride = 7 * sizeof(float);
        reflected_semantic_layout.use_shader_input_locations = true;
        reflected_semantic_layout.attributes = {
            {0, tgfx::VertexFormat::Float3, 0, "position"},
            {4, tgfx::VertexFormat::Float4, 3 * sizeof(float), "joints"},
        };
        reflected_semantic_desc.vertex_layouts.push_back(reflected_semantic_layout);
        if (!device->create_pipeline(reflected_semantic_desc)) {
            std::fprintf(stderr, "D3D11 smoke: reflected semantic input layout failed\n");
            return 1;
        }

        const float reflected_clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float reflected_vertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
             3.0f, -1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
            -1.0f,  3.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        };
        ctx.begin_frame();
        ctx.begin_pass(color, {}, reflected_clear, 1.0f, false);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.set_blend(false);
        ctx.bind_shader(reflected_input_vs, reflected_input_fs);
        ctx.draw_immediate_triangles(reflected_vertices, 3);
        ctx.end_pass();
        ctx.end_frame();

        if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
            std::fprintf(stderr, "D3D11 smoke: reflected input draw readback failed\n");
            return 1;
        }
        if (!close_enough(rgba[0], 0.31f) ||
            !close_enough(rgba[1], 0.19f) ||
            !close_enough(rgba[2], 0.25f) ||
            !close_enough(rgba[3], 1.00f)) {
            std::fprintf(stderr,
                         "D3D11 smoke: unexpected reflected input pixel %.3f %.3f %.3f %.3f\n",
                         rgba[0], rgba[1], rgba[2], rgba[3]);
            return 1;
        }

        const std::filesystem::path shaderc = find_termin_shaderc();
        const std::filesystem::path slangc = find_slangc();
        if (!shaderc.empty() && !slangc.empty()) {
#ifdef _WIN32
            _putenv_s("TERMIN_SLANGC", slangc.string().c_str());
#else
            setenv("TERMIN_SLANGC", slangc.string().c_str(), 1);
#endif
            termin::tgfx2_set_shader_compiler_path(shaderc.string().c_str());
            termin::tgfx2_set_shader_cache_root((artifact_root / "cache").string().c_str());
            termin::tgfx2_set_shader_dev_compile_enabled(true);

            tgfx::TextureDesc canvas_texture_desc;
            canvas_texture_desc.width = 2;
            canvas_texture_desc.height = 2;
            canvas_texture_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
            canvas_texture_desc.usage = tgfx::TextureUsage::Sampled |
                                        tgfx::TextureUsage::CopyDst |
                                        tgfx::TextureUsage::CopySrc;
            auto canvas_texture = device->create_texture(canvas_texture_desc);
            if (!canvas_texture) {
                std::fprintf(stderr, "D3D11 smoke: Canvas2D texture creation failed\n");
                return 1;
            }
            const uint8_t canvas_pixels[] = {
                51, 153, 204, 255, 51, 153, 204, 255,
                51, 153, 204, 255, 51, 153, 204, 255,
            };
            device->upload_texture(
                canvas_texture,
                std::span<const uint8_t>(canvas_pixels, sizeof(canvas_pixels)));

            const float canvas_clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
            ctx.begin_frame();
            ctx.begin_pass(color, {}, canvas_clear, 1.0f, false);
            tgfx::Canvas2DRenderer canvas;
            canvas.begin(ctx, 4, 4);
            canvas.draw_texture(canvas_texture, 0.0f, 0.0f, 4.0f, 4.0f);
            canvas.end();
            ctx.end_pass();
            ctx.end_frame();

            if (!device->read_pixel_rgba8(color, 2, 2, rgba)) {
                std::fprintf(stderr, "D3D11 smoke: Canvas2D readback failed\n");
                return 1;
            }
            if (!close_enough(rgba[0], 51.0f / 255.0f) ||
                !close_enough(rgba[1], 153.0f / 255.0f) ||
                !close_enough(rgba[2], 204.0f / 255.0f) ||
                !close_enough(rgba[3], 1.00f)) {
                std::fprintf(stderr,
                             "D3D11 smoke: unexpected Canvas2D pixel %.3f %.3f %.3f %.3f\n",
                             rgba[0], rgba[1], rgba[2], rgba[3]);
                return 1;
            }
            device->destroy(canvas_texture);

            const std::filesystem::path font_path = find_text_smoke_font();
            if (!font_path.empty()) {
                tgfx::TextureDesc text_desc;
                text_desc.width = 128;
                text_desc.height = 64;
                text_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
                text_desc.usage = tgfx::TextureUsage::Sampled |
                                  tgfx::TextureUsage::ColorAttachment |
                                  tgfx::TextureUsage::CopySrc |
                                  tgfx::TextureUsage::CopyDst;
                auto text_target = device->create_texture(text_desc);
                if (!text_target) {
                    std::fprintf(stderr, "D3D11 smoke: Text2D target creation failed\n");
                    return 1;
                }

                auto assert_text_has_signal = [&](const char* label) -> bool {
                    std::vector<float> pixels(static_cast<size_t>(text_desc.width) *
                                              static_cast<size_t>(text_desc.height) * 4u);
                    if (!device->read_texture_rgba_float(text_target, pixels.data())) {
                        std::fprintf(stderr, "D3D11 smoke: %s text readback failed\n", label);
                        return false;
                    }
                    uint32_t lit_pixels = 0;
                    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
                        if (pixels[i] > 0.05f ||
                            pixels[i + 1] > 0.05f ||
                            pixels[i + 2] > 0.05f) {
                            ++lit_pixels;
                        }
                    }
                    if (lit_pixels < 8) {
                        std::fprintf(stderr,
                                     "D3D11 smoke: %s text produced too few lit pixels (%u)\n",
                                     label,
                                     lit_pixels);
                        return false;
                    }
                    return true;
                };

                tgfx::FontAtlas bitmap_font(font_path.string(), 14, 512, 512);
                bitmap_font.set_sdf_enabled(false);
                ctx.begin_frame();
                ctx.begin_pass(text_target, {}, canvas_clear, 1.0f, false);
                tgfx::Canvas2DRenderer bitmap_canvas(&bitmap_font);
                bitmap_canvas.begin(ctx, static_cast<int>(text_desc.width), static_cast<int>(text_desc.height));
                bitmap_canvas.draw_text(
                    "Text",
                    8.0f,
                    8.0f,
                    18.0f,
                    tgfx::CanvasColor{1.0f, 1.0f, 1.0f, 1.0f},
                    &bitmap_font,
                    tgfx::Text2DRenderer::Anchor::Left);
                bitmap_canvas.end();
                ctx.end_pass();
                ctx.end_frame();
                if (!assert_text_has_signal("bitmap")) {
                    device->destroy(text_target);
                    return 1;
                }

                tgfx::FontAtlas sdf_font(font_path.string(), 14, 512, 512);
                ctx.begin_frame();
                ctx.begin_pass(text_target, {}, canvas_clear, 1.0f, false);
                tgfx::Canvas2DRenderer sdf_canvas(&sdf_font);
                sdf_canvas.begin(ctx, static_cast<int>(text_desc.width), static_cast<int>(text_desc.height));
                sdf_canvas.draw_text(
                    "Text",
                    8.0f,
                    8.0f,
                    28.0f,
                    tgfx::CanvasColor{1.0f, 1.0f, 1.0f, 1.0f},
                    &sdf_font,
                    tgfx::Text2DRenderer::Anchor::Left);
                sdf_canvas.end();
                ctx.end_pass();
                ctx.end_frame();
                if (!assert_text_has_signal("SDF")) {
                    device->destroy(text_target);
                    return 1;
                }

                tgfx::TextureDesc mixed_texture_desc;
                mixed_texture_desc.width = 2;
                mixed_texture_desc.height = 2;
                mixed_texture_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
                mixed_texture_desc.usage = tgfx::TextureUsage::Sampled |
                                           tgfx::TextureUsage::CopyDst |
                                           tgfx::TextureUsage::CopySrc;
                auto mixed_texture = device->create_texture(mixed_texture_desc);
                if (!mixed_texture) {
                    std::fprintf(stderr, "D3D11 smoke: mixed Canvas2D texture creation failed\n");
                    device->destroy(text_target);
                    return 1;
                }
                const uint8_t mixed_pixels[] = {
                    51, 153, 204, 255, 51, 153, 204, 255,
                    51, 153, 204, 255, 51, 153, 204, 255,
                };
                device->upload_texture(
                    mixed_texture,
                    std::span<const uint8_t>(mixed_pixels, sizeof(mixed_pixels)));

                ctx.begin_frame();
                ctx.begin_pass(text_target, {}, canvas_clear, 1.0f, false);
                tgfx::Canvas2DRenderer mixed_canvas(&bitmap_font);
                mixed_canvas.begin(ctx, static_cast<int>(text_desc.width), static_cast<int>(text_desc.height));
                mixed_canvas.draw_rect(
                    0.0f, 0.0f, static_cast<float>(text_desc.width), static_cast<float>(text_desc.height),
                    tgfx::CanvasColor{0.02f, 0.02f, 0.03f, 1.0f});
                mixed_canvas.draw_texture(mixed_texture, 4.0f, 4.0f, 24.0f, 24.0f);
                mixed_canvas.draw_rect(
                    34.0f, 4.0f, 80.0f, 32.0f,
                    tgfx::CanvasColor{0.12f, 0.12f, 0.16f, 1.0f});
                mixed_canvas.draw_text(
                    "Text",
                    38.0f,
                    8.0f,
                    18.0f,
                    tgfx::CanvasColor{1.0f, 1.0f, 1.0f, 1.0f},
                    &bitmap_font,
                    tgfx::Text2DRenderer::Anchor::Left);
                mixed_canvas.end();
                ctx.end_pass();
                ctx.end_frame();

                std::vector<float> mixed_readback(static_cast<size_t>(text_desc.width) *
                                                   static_cast<size_t>(text_desc.height) * 4u);
                if (!device->read_texture_rgba_float(text_target, mixed_readback.data())) {
                    std::fprintf(stderr, "D3D11 smoke: mixed Canvas2D readback failed\n");
                    device->destroy(mixed_texture);
                    device->destroy(text_target);
                    return 1;
                }
                const size_t mixed_texture_px = (12u * text_desc.width + 12u) * 4u;
                if (!close_enough(mixed_readback[mixed_texture_px + 0], 51.0f / 255.0f) ||
                    !close_enough(mixed_readback[mixed_texture_px + 1], 153.0f / 255.0f) ||
                    !close_enough(mixed_readback[mixed_texture_px + 2], 204.0f / 255.0f) ||
                    !close_enough(mixed_readback[mixed_texture_px + 3], 1.0f)) {
                    std::fprintf(stderr,
                                 "D3D11 smoke: unexpected mixed Canvas2D texture pixel %.3f %.3f %.3f %.3f\n",
                                 mixed_readback[mixed_texture_px + 0],
                                 mixed_readback[mixed_texture_px + 1],
                                 mixed_readback[mixed_texture_px + 2],
                                 mixed_readback[mixed_texture_px + 3]);
                    device->destroy(mixed_texture);
                    device->destroy(text_target);
                    return 1;
                }
                uint32_t mixed_text_pixels = 0;
                for (uint32_t y = 0; y < text_desc.height; ++y) {
                    for (uint32_t x = 34; x < text_desc.width; ++x) {
                        const size_t i = (static_cast<size_t>(y) * text_desc.width + x) * 4u;
                        if (mixed_readback[i + 0] > 0.2f &&
                            mixed_readback[i + 1] > 0.2f &&
                            mixed_readback[i + 2] > 0.2f) {
                            ++mixed_text_pixels;
                        }
                    }
                }
                if (mixed_text_pixels < 8) {
                    std::fprintf(stderr,
                                 "D3D11 smoke: mixed Canvas2D text produced too few lit pixels (%u)\n",
                                 mixed_text_pixels);
                    device->destroy(mixed_texture);
                    device->destroy(text_target);
                    return 1;
                }
                device->destroy(mixed_texture);
                device->destroy(text_target);
            } else {
                std::printf(
                    "D3D11 smoke: Text2D smoke skipped (font missing: %s)\n",
                    font_path.string().c_str());
            }
        } else {
            std::printf(
                "D3D11 smoke: Canvas2D builtin shader smoke skipped "
                "(termin_shaderc=%s slangc=%s)\n",
                shaderc.empty() ? "missing" : "ok",
                slangc.empty() ? "missing" : "ok");
        }

        device->destroy(reflected_input_fs);
        device->destroy(reflected_input_vs);
        device->destroy(render_context_fs);
        device->destroy(normal_msaa_pipeline);
        device->destroy(normal_msaa_color);
        device->destroy(normal_resource_set);
        device->destroy(draw_data_cb);
        device->destroy(per_frame_cb);
        device->destroy(normal_vbo);
        device->destroy(normal_pipeline);
        device->destroy(normal_fs);
        device->destroy(normal_vs);
        device->destroy(resource_set);
        device->destroy(sampler);
        device->destroy(textured_pipeline);
        device->destroy(pipeline);
        device->destroy(color);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "D3D11 smoke: %s\n", e.what());
        return 77;
    }
}
