// Smoke test for tgfx2 Vulkan backend: offscreen render to texture.
// No swapchain — we render to a texture and read back pixels.
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "tgfx2/backend_binding_plan.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
}

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_type_conversions.hpp"
#endif

static const char* vertex_src = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* fragment_src = R"(
#version 450 core
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

static const char* bound_resource_vertex_src = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* bound_resource_fragment_src = R"(
#version 450 core
layout(binding = 0) uniform ColorBlock {
    vec4 color;
};
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = color;
}
)";

static const char* slang_matrix_vertex_src = R"(
import termin_prelude;

struct TransformUBO
{
    float4x4 mvp;
};

[[TerminScope("draw")]]
ConstantBuffer<TransformUBO> u_transform;

struct VertexInput
{
    float2 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input)
{
    VertexOutput output;
    output.position = mul(u_transform.mvp, float4(input.position, 0.0, 1.0));
    return output;
}
)";

static const char* slang_matrix_fragment_src = R"(
struct FragmentOutput
{
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main()
{
    FragmentOutput output;
    output.color = float4(0.90, 0.05, 0.75, 1.0);
    return output;
}
)";

static const char* fsq_uv_fragment_src = R"(
#version 450 core
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(v_uv, 0.0, 1.0);
}
)";

static const char* invalid_fallback_src = R"(
#version 450 core
#error Vulkan Slang artifact smoke must not compile fallback source
void main() {}
)";

static bool write_text_file(const std::filesystem::path& path, const char* text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        fprintf(stderr, "Failed to open file for writing: %s\n", path.string().c_str());
        return false;
    }
    out << text;
    if (!out) {
        fprintf(stderr, "Failed to write file: %s\n", path.string().c_str());
        return false;
    }
    return true;
}

static std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        fprintf(stderr, "Failed to open text file: %s\n", path.string().c_str());
        return std::nullopt;
    }
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

static bool is_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !std::filesystem::is_directory(path, ec);
}

static std::vector<std::string> split_paths(const char* value) {
    std::vector<std::string> paths;
    if (!value || value[0] == '\0') return paths;
    std::string text(value);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(':', start);
        std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) paths.push_back(part);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return paths;
}

static std::optional<std::filesystem::path> find_on_path(const char* exe_name) {
    for (const std::string& dir : split_paths(std::getenv("PATH"))) {
        std::filesystem::path candidate = std::filesystem::path(dir) / exe_name;
        if (is_existing_file(candidate)) return candidate;
    }
    return std::nullopt;
}

static std::optional<std::filesystem::path> resolve_slangc() {
    if (const char* env = std::getenv("TERMIN_SLANGC")) {
        if (env[0] != '\0' && is_existing_file(env)) {
            return std::filesystem::path(env);
        }
    }
    return find_on_path("slangc");
}

static std::optional<std::filesystem::path> resolve_termin_shaderc(const char* argv0) {
    if (const char* env = std::getenv("TERMIN_SHADERC")) {
        if (env[0] != '\0' && is_existing_file(env)) {
            return std::filesystem::path(env);
        }
    }

    std::vector<std::filesystem::path> candidates;
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        std::filesystem::path exe_dir = std::filesystem::absolute(argv0, ec).parent_path();
        if (!ec) {
            candidates.push_back(exe_dir / "termin_shaderc");
        }
    }
    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            candidates.push_back(std::filesystem::path(sdk) / "bin" / "termin_shaderc");
        }
    }
    candidates.push_back(std::filesystem::current_path() / "sdk" / "bin" / "termin_shaderc");

    for (const auto& candidate : candidates) {
        if (is_existing_file(candidate)) return candidate;
    }
    return find_on_path("termin_shaderc");
}

static std::string quote_arg(const std::filesystem::path& value) {
    std::string text = value.string();
    std::string out = "'";
    for (char ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

static bool run_shaderc(
    const std::filesystem::path& shaderc,
    const std::filesystem::path& slangc,
    const char* stage,
    const char* entry,
    const std::filesystem::path& input,
    const std::filesystem::path& output)
{
    std::string cmd =
        quote_arg(shaderc) +
        " compile --language slang --target vulkan --stage " + stage +
        " --entry " + entry + " --input " + quote_arg(input) +
        " --output " + quote_arg(output) +
        " --slangc " + quote_arg(slangc);
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "termin_shaderc failed for %s with status %d\n", input.string().c_str(), rc);
        return false;
    }
    return true;
}

static bool render_fsq_artifact_smoke(tgfx::IRenderDevice& device) {
    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kHeight = 64;

    tgfx::TextureDesc rt_desc;
    rt_desc.width = kWidth;
    rt_desc.height = kHeight;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    tgfx::TextureHandle rt = device.create_texture(rt_desc);
    if (!rt) {
        fprintf(stderr, "Failed to create FSQ Slang artifact render target\n");
        return false;
    }

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = fsq_uv_fragment_src;
    fs_desc.debug_name = "termin-engine-fsq-artifact-smoke:fragment";
    tgfx::ShaderHandle fs = device.create_shader(fs_desc);
    if (!fs) {
        fprintf(stderr, "Failed to create FSQ Slang artifact fragment shader\n");
        device.destroy(rt);
        return false;
    }

    {
        tgfx::PipelineCache cache(device);
        tgfx::RenderContext2 ctx(device, cache);
        ctx.begin_frame();

        float clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
        ctx.begin_pass(rt, {}, clear);
        ctx.set_viewport(0, 0, kWidth, kHeight);
        ctx.set_depth_test(false);
        ctx.set_depth_write(false);
        ctx.set_blend(false);
        ctx.set_cull(tgfx::CullMode::None);
        ctx.bind_shader({}, fs);
        ctx.draw_fullscreen_quad();
        ctx.end_pass();
        ctx.end_frame();
    }

    device.wait_idle();

    float center_pixel[4] = {};
    bool read_ok = device.read_pixel_rgba8(rt, kWidth / 2, kHeight / 2, center_pixel);
    printf("FSQ Slang artifact center pixel: (%.2f %.2f %.2f %.2f)\n",
           center_pixel[0], center_pixel[1], center_pixel[2], center_pixel[3]);

    const bool matches_canonical_uv =
        read_ok &&
        center_pixel[0] > 0.40f && center_pixel[0] < 0.60f &&
        center_pixel[1] > 0.40f && center_pixel[1] < 0.60f &&
        center_pixel[2] < 0.10f;

    device.destroy(fs);
    device.destroy(rt);
    return matches_canonical_uv;
}

static bool render_bound_resource_set_smoke(tgfx::IRenderDevice& device) {
    constexpr uint32_t kWidth = 32;
    constexpr uint32_t kHeight = 32;

    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = bound_resource_vertex_src;
    vs_desc.debug_name = "vulkan-bound-resource-set:vertex";
    tgfx::ShaderHandle vs = device.create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = bound_resource_fragment_src;
    fs_desc.debug_name = "vulkan-bound-resource-set:fragment";
    tgfx::ShaderHandle fs = device.create_shader(fs_desc);

    tgfx::PipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vs;
    pipeline_desc.fragment_shader = fs;
    pipeline_desc.topology = tgfx::PrimitiveTopology::TriangleList;
    pipeline_desc.depth_stencil.depth_test = false;
    pipeline_desc.depth_stencil.depth_write = false;
    pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
    pipeline_desc.raster.cull = tgfx::CullMode::None;
    pipeline_desc.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};

    tgfx::VertexBufferLayout layout;
    layout.stride = 2 * sizeof(float);
    layout.attributes = {{0, tgfx::VertexFormat::Float2, 0}};
    pipeline_desc.vertex_layouts.push_back(layout);

    tgfx::PipelineHandle pipeline = device.create_pipeline(pipeline_desc);
    const uintptr_t resource_layout_token =
        device.pipeline_resource_layout_token(pipeline);
    if (resource_layout_token == 0) {
        fprintf(stderr, "Vulkan bound smoke: pipeline resource layout token is null\n");
        return false;
    }

    const float vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle vb = device.create_buffer(vb_desc);
    device.upload_buffer(
        vb,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)));

    const float color_block[] = {0.15f, 0.65f, 0.25f, 1.0f};
    tgfx::BufferDesc ubo_desc;
    ubo_desc.size = sizeof(color_block);
    ubo_desc.usage = tgfx::BufferUsage::Uniform | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle ubo = device.create_buffer(ubo_desc);
    device.upload_buffer(
        ubo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(color_block), sizeof(color_block)));

    tgfx::BackendBindingPlanEntry plan_entry;
    plan_entry.resource.name = "ColorBlock";
    plan_entry.resource.kind = tgfx::ShaderResourceKind::ConstantBuffer;
    plan_entry.resource.scope = tgfx::ShaderResourceScope::Material;
    plan_entry.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    plan_entry.size = sizeof(color_block);
    plan_entry.placement.kind = tgfx::BackendPlacementKind::VulkanDescriptor;
    plan_entry.placement.vulkan.set = 0;
    plan_entry.placement.vulkan.binding = 0;
    plan_entry.placement.vulkan.descriptor_kind = tgfx::BackendDescriptorKind::UniformBuffer;

    tgfx::BoundResourceValue value;
    value.kind = tgfx::BoundResourceKind::UniformBuffer;
    value.buffer = ubo;
    value.range = sizeof(color_block);

    tgfx::BoundResourceSetDesc bound_desc;
    bound_desc.resource_layout_token = resource_layout_token;
    tgfx::BoundResourceGroup material_group;
    material_group.scope = tgfx::ShaderResourceScope::Material;
    material_group.bindings.push_back({plan_entry, value});
    bound_desc.groups.push_back(std::move(material_group));
    tgfx::ResourceSetHandle resource_set =
        device.create_bound_resource_set(bound_desc);
    if (!resource_set) {
        fprintf(stderr, "Vulkan bound smoke: create_bound_resource_set failed\n");
        return false;
    }

    tgfx::BoundResourceSetDesc wrong_backend_desc = bound_desc;
    wrong_backend_desc.groups[0].bindings[0].plan_entry.placement.kind =
        tgfx::BackendPlacementKind::OpenGLBinding;
    if (device.create_bound_resource_set(wrong_backend_desc)) {
        fprintf(stderr, "Vulkan bound smoke: accepted non-Vulkan placement\n");
        return false;
    }

    tgfx::BoundResourceSetDesc wrong_descriptor_desc = bound_desc;
    wrong_descriptor_desc.groups[0].bindings[0].plan_entry.placement.vulkan.descriptor_kind =
        tgfx::BackendDescriptorKind::Sampler;
    if (device.create_bound_resource_set(wrong_descriptor_desc)) {
        fprintf(stderr, "Vulkan bound smoke: accepted wrong descriptor kind\n");
        return false;
    }

    tgfx::TextureDesc rt_desc;
    rt_desc.width = kWidth;
    rt_desc.height = kHeight;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    tgfx::TextureHandle rt = device.create_texture(rt_desc);

    auto cmd = device.create_command_list();
    cmd->begin();

    tgfx::RenderPassDesc pass;
    tgfx::ColorAttachmentDesc color_attachment;
    color_attachment.texture = rt;
    color_attachment.load = tgfx::LoadOp::Clear;
    color_attachment.clear_color[0] = 0.0f;
    color_attachment.clear_color[1] = 0.0f;
    color_attachment.clear_color[2] = 0.0f;
    color_attachment.clear_color[3] = 1.0f;
    pass.colors.push_back(color_attachment);

    cmd->begin_render_pass(pass);
    cmd->set_viewport(0, 0, kWidth, kHeight);
    cmd->bind_pipeline(pipeline);
    cmd->bind_resource_set(resource_set);
    cmd->bind_vertex_buffer(0, vb);
    cmd->draw(3);
    cmd->end_render_pass();
    cmd->end();
    device.submit(*cmd);
    device.wait_idle();

    float pixel[4] = {};
    const bool read_ok = device.read_pixel_rgba8(rt, kWidth / 2, kHeight / 2, pixel);
    printf("Vulkan bound resource set center pixel: %s (%.2f %.2f %.2f %.2f)\n",
           read_ok ? "ok" : "failed",
           pixel[0], pixel[1], pixel[2], pixel[3]);

    const bool pass_ok =
        read_ok &&
        pixel[0] > 0.08f && pixel[0] < 0.30f &&
        pixel[1] > 0.50f && pixel[1] < 0.80f &&
        pixel[2] > 0.15f && pixel[2] < 0.40f &&
        pixel[3] > 0.90f;

    device.destroy(resource_set);
    device.destroy(ubo);
    device.destroy(vb);
    device.destroy(rt);
    device.destroy(pipeline);
    device.destroy(vs);
    device.destroy(fs);
    return pass_ok;
}

#ifdef TGFX2_HAS_VULKAN
static bool verify_vertex_format_conversions() {
    struct Case {
        tgfx::VertexFormat source;
        VkFormat expected;
    };

    const Case cases[] = {
        {tgfx::VertexFormat::Float, VK_FORMAT_R32_SFLOAT},
        {tgfx::VertexFormat::Float2, VK_FORMAT_R32G32_SFLOAT},
        {tgfx::VertexFormat::Float3, VK_FORMAT_R32G32B32_SFLOAT},
        {tgfx::VertexFormat::Float4, VK_FORMAT_R32G32B32A32_SFLOAT},
        {tgfx::VertexFormat::Int, VK_FORMAT_R32_SINT},
        {tgfx::VertexFormat::Int2, VK_FORMAT_R32G32_SINT},
        {tgfx::VertexFormat::Int3, VK_FORMAT_R32G32B32_SINT},
        {tgfx::VertexFormat::Int4, VK_FORMAT_R32G32B32A32_SINT},
        {tgfx::VertexFormat::UInt, VK_FORMAT_R32_UINT},
        {tgfx::VertexFormat::UInt2, VK_FORMAT_R32G32_UINT},
        {tgfx::VertexFormat::UInt3, VK_FORMAT_R32G32B32_UINT},
        {tgfx::VertexFormat::UInt4, VK_FORMAT_R32G32B32A32_UINT},
        {tgfx::VertexFormat::Short, VK_FORMAT_R16_SINT},
        {tgfx::VertexFormat::Short2, VK_FORMAT_R16G16_SINT},
        {tgfx::VertexFormat::Short3, VK_FORMAT_R16G16B16_SINT},
        {tgfx::VertexFormat::Short4, VK_FORMAT_R16G16B16A16_SINT},
        {tgfx::VertexFormat::UShort, VK_FORMAT_R16_UINT},
        {tgfx::VertexFormat::UShort2, VK_FORMAT_R16G16_UINT},
        {tgfx::VertexFormat::UShort3, VK_FORMAT_R16G16B16_UINT},
        {tgfx::VertexFormat::UShort4, VK_FORMAT_R16G16B16A16_UINT},
        {tgfx::VertexFormat::Byte4, VK_FORMAT_R8G8B8A8_SINT},
        {tgfx::VertexFormat::UByte4, VK_FORMAT_R8G8B8A8_UINT},
        {tgfx::VertexFormat::UByte4N, VK_FORMAT_R8G8B8A8_UNORM},
    };

    for (const auto& c : cases) {
        const VkFormat actual = tgfx::vk::to_vk_vertex_format(c.source);
        if (actual != c.expected) {
            fprintf(stderr,
                    "Vulkan vertex format conversion mismatch: got %d, expected %d\n",
                    static_cast<int>(actual),
                    static_cast<int>(c.expected));
            return false;
        }
    }
    return true;
}
#endif

int main(int argc, char** argv) {
#ifndef TGFX2_HAS_VULKAN
    printf("Vulkan backend not compiled, skipping test\n");
    return 0;
#else
    printf("--- tgfx2 Vulkan smoke test (offscreen) ---\n");
    if (!verify_vertex_format_conversions()) {
        return 1;
    }

    // Create Vulkan device (no surface — offscreen only). Validation is
    // opt-in so CI runners without VK_LAYER_KHRONOS_validation still exercise
    // the runtime path when a Vulkan ICD is available.
    tgfx::VulkanDeviceCreateInfo info;
    const char* validation_env = std::getenv("TGFX2_VULKAN_VALIDATION");
    info.enable_validation = validation_env && validation_env[0] == '1';

    std::unique_ptr<tgfx::IRenderDevice> device;
    try {
        device = std::make_unique<tgfx::VulkanRenderDevice>(info);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create Vulkan device: %s\n", e.what());
        return 1;
    }

    auto caps = device->capabilities();
    printf("Backend: Vulkan, max_tex: %u, compute: %s, geometry: %s\n",
           caps.max_texture_dimension_2d,
           caps.supports_compute ? "yes" : "no",
           caps.supports_geometry_shaders ? "yes" : "no");

    // --- Texture region upload parity check ---
    tgfx::TextureDesc upload_desc;
    upload_desc.width = 4;
    upload_desc.height = 4;
    upload_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    upload_desc.usage = tgfx::TextureUsage::Sampled |
                        tgfx::TextureUsage::CopySrc |
                        tgfx::TextureUsage::CopyDst;
    auto upload_tex = device->create_texture(upload_desc);

    std::vector<uint8_t> zeros(4 * 4 * 4, 0);
    device->upload_texture(upload_tex, zeros);

    const uint8_t region_pixels[] = {
        255, 0, 0, 255,  255, 0, 0, 255,
        255, 0, 0, 255,  255, 0, 0, 255,
    };
    device->upload_texture_region(
        upload_tex, 1, 2, 2, 2,
        std::span<const uint8_t>(region_pixels, sizeof(region_pixels)));

    auto upload_cmd = device->create_command_list();
    upload_cmd->begin();
    upload_cmd->end();
    device->submit(*upload_cmd);
    upload_cmd.reset();

    float red_pixel[4] = {};
    float untouched_pixel[4] = {};
    bool region_read_ok = device->read_pixel_rgba8(upload_tex, 1, 2, red_pixel) &&
                          device->read_pixel_rgba8(upload_tex, 0, 0, untouched_pixel);
    bool region_upload_ok =
        region_read_ok &&
        red_pixel[0] > 0.9f && red_pixel[1] < 0.1f &&
        red_pixel[2] < 0.1f && red_pixel[3] > 0.9f &&
        untouched_pixel[0] < 0.1f && untouched_pixel[1] < 0.1f &&
        untouched_pixel[2] < 0.1f && untouched_pixel[3] < 0.1f;

    std::vector<float> upload_pixels(4 * 4 * 4, 0.0f);
    bool full_color_read_ok = device->read_texture_rgba_float(upload_tex, upload_pixels.data());
    const size_t full_red = (2 * 4 + 1) * 4;
    const size_t full_untouched = 0;
    bool full_color_matches =
        full_color_read_ok &&
        upload_pixels[full_red + 0] > 0.9f &&
        upload_pixels[full_red + 1] < 0.1f &&
        upload_pixels[full_red + 2] < 0.1f &&
        upload_pixels[full_red + 3] > 0.9f &&
        upload_pixels[full_untouched + 0] < 0.1f &&
        upload_pixels[full_untouched + 1] < 0.1f &&
        upload_pixels[full_untouched + 2] < 0.1f &&
        upload_pixels[full_untouched + 3] < 0.1f;

    printf("Texture region upload: %s, full color readback: %s\n",
           region_upload_ok ? "ok" : "failed",
           full_color_matches ? "ok" : "failed");
    if (!region_upload_ok || !full_color_matches) {
        fprintf(stderr,
                "Texture readback mismatch: red=(%.3f %.3f %.3f %.3f), untouched=(%.3f %.3f %.3f %.3f), full_red=(%.3f %.3f %.3f %.3f)\n",
                red_pixel[0], red_pixel[1], red_pixel[2], red_pixel[3],
                untouched_pixel[0], untouched_pixel[1], untouched_pixel[2], untouched_pixel[3],
                upload_pixels[full_red + 0], upload_pixels[full_red + 1],
                upload_pixels[full_red + 2], upload_pixels[full_red + 3]);
        return 1;
    }
    device->destroy(upload_tex);

    tgfx::TextureDesc depth_desc;
    depth_desc.width = 2;
    depth_desc.height = 2;
    depth_desc.format = tgfx::PixelFormat::D32F;
    depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment |
                       tgfx::TextureUsage::CopySrc |
                       tgfx::TextureUsage::CopyDst;
    auto depth_tex = device->create_texture(depth_desc);
    const float depth_upload[] = {0.25f, 0.5f, 0.75f, 1.0f};
    device->upload_texture(depth_tex, std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(depth_upload), sizeof(depth_upload)));

    auto depth_cmd = device->create_command_list();
    depth_cmd->begin();
    depth_cmd->end();
    device->submit(*depth_cmd);
    depth_cmd.reset();

    float depth_pixels[4] = {};
    bool depth_read_ok = device->read_texture_depth_float(depth_tex, depth_pixels);
    bool depth_matches =
        depth_read_ok &&
        depth_pixels[0] > 0.24f && depth_pixels[0] < 0.26f &&
        depth_pixels[1] > 0.49f && depth_pixels[1] < 0.51f &&
        depth_pixels[2] > 0.74f && depth_pixels[2] < 0.76f &&
        depth_pixels[3] > 0.99f && depth_pixels[3] < 1.01f;
    printf("Full depth readback: %s\n", depth_matches ? "ok" : "failed");
    if (!depth_matches) {
        fprintf(stderr,
                "Depth readback mismatch: (%.3f %.3f %.3f %.3f)\n",
                depth_pixels[0], depth_pixels[1], depth_pixels[2], depth_pixels[3]);
        return 1;
    }
    device->destroy(depth_tex);

    // --- Create shaders (GLSL source — compiled to SPIR-V by shaderc) ---
    tgfx::ShaderHandle vs, fs;
    try {
        tgfx::ShaderDesc vs_desc;
        vs_desc.stage = tgfx::ShaderStage::Vertex;
        vs_desc.source = vertex_src;
        vs = device->create_shader(vs_desc);

        tgfx::ShaderDesc fs_desc;
        fs_desc.stage = tgfx::ShaderStage::Fragment;
        fs_desc.source = fragment_src;
        fs = device->create_shader(fs_desc);
    } catch (const std::exception& e) {
        fprintf(stderr, "Shader creation failed: %s\n", e.what());
        return 1;
    }

    printf("Shaders created: vs=%u, fs=%u\n", vs.id, fs.id);

    // --- Create render target texture (256x256) ---
    tgfx::TextureDesc rt_desc;
    rt_desc.width = 256;
    rt_desc.height = 256;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    auto rt_tex = device->create_texture(rt_desc);
    printf("Render target: id=%u\n", rt_tex.id);

    // --- Create pipeline ---
    tgfx::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.topology = tgfx::PrimitiveTopology::TriangleList;
    pipe_desc.depth_stencil.depth_test = false;
    pipe_desc.depth_stencil.depth_write = false;
    pipe_desc.depth_format = tgfx::PixelFormat::Undefined;
    pipe_desc.raster.cull = tgfx::CullMode::None;
    pipe_desc.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};

    tgfx::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.attributes = {
        {0, tgfx::VertexFormat::Float2, 0},
        {1, tgfx::VertexFormat::Float3, 2 * sizeof(float)},
    };
    pipe_desc.vertex_layouts.push_back(layout);

    auto pipeline = device->create_pipeline(pipe_desc);
    printf("Pipeline: id=%u\n", pipeline.id);

    // --- Create vertex data through the transient vertex ring ---
    float vertices[] = {
         0.0f,  0.5f,  1.f, 0.f, 0.f,
        -0.5f, -0.5f,  0.f, 1.f, 0.f,
         0.5f, -0.5f,  0.f, 0.f, 1.f,
    };
    uint64_t vertex_offset = device->transient_vertex_write(
        vertices, static_cast<uint32_t>(sizeof(vertices)));
    if (vertex_offset == UINT64_MAX) {
        fprintf(stderr, "Transient vertex ring write failed\n");
        return 1;
    }
    auto vb = device->transient_vertex_buffer();
    if (!vb) {
        fprintf(stderr, "Transient vertex ring returned empty buffer\n");
        return 1;
    }

    uint32_t indices[] = {0, 1, 2};
    tgfx::BufferDesc ib_desc;
    ib_desc.size = sizeof(indices);
    ib_desc.usage = tgfx::BufferUsage::Index;
    ib_desc.cpu_visible = true;
    auto ib = device->create_buffer(ib_desc);
    device->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

    printf("Buffers created\n");

    // --- Draw ---
    auto cmd = device->create_command_list();
    cmd->begin();

    tgfx::RenderPassDesc pass;
    tgfx::ColorAttachmentDesc color_att;
    color_att.texture = rt_tex;
    color_att.load = tgfx::LoadOp::Clear;
    color_att.clear_color[0] = 0.0f;
    color_att.clear_color[1] = 0.0f;
    color_att.clear_color[2] = 0.2f;
    color_att.clear_color[3] = 1.0f;
    pass.colors.push_back(color_att);

    cmd->begin_render_pass(pass);
    cmd->bind_pipeline(pipeline);
    cmd->bind_vertex_buffer(0, vb, vertex_offset);
    cmd->bind_index_buffer(ib, tgfx::IndexType::Uint32);
    cmd->draw_indexed(3);
    cmd->end_render_pass();

    cmd->end();
    device->submit(*cmd);

    printf("Draw submitted\n");

    // --- Read back via staging buffer ---
    const uint32_t pixel_count = 256 * 256;
    const uint32_t byte_size = pixel_count * 4; // RGBA8

    tgfx::BufferDesc readback_desc;
    readback_desc.size = byte_size;
    readback_desc.usage = tgfx::BufferUsage::CopyDst;
    readback_desc.cpu_visible = true;
    auto readback_buf = device->create_buffer(readback_desc);

    // Copy texture to buffer
    auto* vk_device = static_cast<tgfx::VulkanRenderDevice*>(device.get());
    auto* rt = vk_device->get_texture(rt_tex);
    auto* rb = vk_device->get_buffer(readback_buf);

    vk_device->execute_immediate([&](VkCommandBuffer cb) {
        // Transition to transfer src
        vk_device->transition_image_layout(cb, rt->image,
            rt->current_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {256, 256, 1};

        vkCmdCopyImageToBuffer(cb, rt->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                rb->buffer, 1, &region);
    });
    rt->current_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    auto readback_cmd = device->create_command_list();
    readback_cmd->begin();
    readback_cmd->end();
    device->submit(*readback_cmd);
    device->wait_idle();
    readback_cmd.reset();

    // Read pixels
    std::vector<uint8_t> pixels(byte_size);
    device->read_buffer(readback_buf, pixels);

    // Check center pixel (128, 128)
    size_t center = (128 * 256 + 128) * 4;
    printf("Center pixel: (%u, %u, %u, %u)\n",
           pixels[center], pixels[center+1], pixels[center+2], pixels[center+3]);

    // Check corner pixel (0, 0)
    printf("Corner pixel: (%u, %u, %u, %u)\n",
           pixels[0], pixels[1], pixels[2], pixels[3]);

    bool center_drawn = (pixels[center] > 30 || pixels[center+1] > 30 || pixels[center+2] > 60);
    bool corner_is_blue = (pixels[0] < 10 && pixels[1] < 10 && pixels[2] > 40);

    bool bound_resource_ok = render_bound_resource_set_smoke(*device);
    if (!bound_resource_ok) {
        fprintf(stderr, "Vulkan bound resource set smoke failed\n");
    }

    // --- Generated Slang artifact smoke ---
    bool slang_artifact_ok = true;
    auto slangc = resolve_slangc();
    auto shaderc = resolve_termin_shaderc(argc > 0 ? argv[0] : nullptr);
    if (!slangc || !shaderc) {
        printf("Slang Vulkan artifact smoke: skipped (termin_shaderc=%s, slangc=%s)\n",
               shaderc ? shaderc->string().c_str() : "<missing>",
               slangc ? slangc->string().c_str() : "<missing>");
    } else {
        printf("Slang Vulkan artifact smoke: termin_shaderc=%s, slangc=%s\n",
               shaderc->string().c_str(), slangc->string().c_str());

        const char* artifact_uuid = "termin-vulkan-slang-matrix-smoke";
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path artifact_root =
            std::filesystem::temp_directory_path() /
            ("termin-vulkan-slang-artifact-smoke-" + std::to_string(now));
        std::filesystem::path artifact_dir = artifact_root / "shaders" / "vulkan";
        std::error_code fs_ec;
        if (!std::filesystem::create_directories(artifact_dir, fs_ec) && fs_ec) {
            fprintf(stderr, "Failed to create Slang artifact directory: %s (%s)\n",
                    artifact_dir.string().c_str(), fs_ec.message().c_str());
            slang_artifact_ok = false;
        }

        const tgfx::EngineShaderStageSource& fsq_shader =
            tgfx::engine_fullscreen_quad_vertex_shader();
        std::filesystem::path fsq_spv = artifact_dir / (std::string(fsq_shader.uuid) + ".vert.spv");
        if (slang_artifact_ok) {
            std::vector<uint8_t> fsq_bytecode;
            termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
            termin::tgfx2_set_shader_cache_root((artifact_root / "shader-cache").string().c_str());
            termin::tgfx2_set_shader_compiler_path(shaderc->string().c_str());
            termin::tgfx2_set_shader_dev_compile_enabled(true);

            if (!termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
                    fsq_shader,
                    tgfx::BackendType::Vulkan,
                    fsq_bytecode)) {
                fprintf(stderr, "FSQ Slang on-demand artifact compile failed\n");
                slang_artifact_ok = false;
            } else if (fsq_bytecode.empty() || !is_existing_file(fsq_spv)) {
                fprintf(stderr, "FSQ Slang on-demand artifact compile produced no bytecode\n");
                slang_artifact_ok = false;
            }
            termin::tgfx2_set_shader_dev_compile_enabled(false);
        }

        std::filesystem::path vertex_slang = artifact_root / "matrix.vert.slang";
        std::filesystem::path fragment_slang = artifact_root / "matrix.frag.slang";
        const std::filesystem::path fsq_source_path =
            std::filesystem::path(TGFX2_SOURCE_DIR) / "resources" / fsq_shader.source_resource_path;
        std::optional<std::string> fsq_source = read_text_file(fsq_source_path);
        std::filesystem::path fsq_slang = artifact_root / "fsq.vert.slang";
        std::filesystem::path vertex_spv = artifact_dir / (std::string(artifact_uuid) + ".vert.spv");
        std::filesystem::path fragment_spv = artifact_dir / (std::string(artifact_uuid) + ".frag.spv");

        if (slang_artifact_ok && !fsq_source) {
            slang_artifact_ok = false;
        }
        if (slang_artifact_ok) {
            slang_artifact_ok =
                write_text_file(vertex_slang, slang_matrix_vertex_src) &&
                write_text_file(fragment_slang, slang_matrix_fragment_src) &&
                write_text_file(fsq_slang, fsq_source->c_str()) &&
                run_shaderc(*shaderc, *slangc, "vertex", "vs_main", vertex_slang, vertex_spv) &&
                run_shaderc(*shaderc, *slangc, "fragment", "fs_main", fragment_slang, fragment_spv) &&
                run_shaderc(*shaderc, *slangc, "vertex", "vs_main", fsq_slang, fsq_spv);
        }

        tgfx::TextureHandle slang_rt_tex;
        tgfx::BufferHandle slang_vb;
        tc_shader_handle slang_shader_handle = tc_shader_handle_invalid();

        if (slang_artifact_ok) {
            termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
            slang_shader_handle = tc_shader_from_sources_ex(
                invalid_fallback_src,
                invalid_fallback_src,
                nullptr,
                "tgfx2 Vulkan Slang matrix smoke",
                nullptr,
                artifact_uuid,
                TC_SHADER_LANGUAGE_SLANG,
                TC_SHADER_ARTIFACT_REQUIRED
            );

            tc_shader* slang_shader = tc_shader_get(slang_shader_handle);
            tgfx::ShaderHandle slang_vs;
            tgfx::ShaderHandle slang_fs;
            if (!slang_shader) {
                fprintf(stderr, "Failed to create Vulkan Slang smoke tc_shader\n");
                slang_artifact_ok = false;
            } else if (!vk_device->ensure_tc_shader(slang_shader, &slang_vs, &slang_fs)) {
                fprintf(stderr, "Failed to load generated Vulkan Slang artifacts\n");
                slang_artifact_ok = false;
            } else {
                tgfx::VertexBufferLayout slang_layout;
                slang_layout.stride = 2 * sizeof(float);
                slang_layout.attributes = {
                    {0, tgfx::VertexFormat::Float2, 0},
                };

                const float slang_vertices[] = {
                    -0.25f, -0.25f,
                     0.25f, -0.25f,
                     0.00f,  0.25f,
                };
                tgfx::BufferDesc slang_vb_desc;
                slang_vb_desc.size = sizeof(slang_vertices);
                slang_vb_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
                slang_vb = device->create_buffer(slang_vb_desc);
                device->upload_buffer(
                    slang_vb,
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(slang_vertices),
                        sizeof(slang_vertices)));

                const std::array<float, 16> mvp_column_major = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                   -0.5f, 0.0f, 0.0f, 1.0f,
                };

                tgfx::TextureDesc slang_rt_desc;
                slang_rt_desc.width = 128;
                slang_rt_desc.height = 128;
                slang_rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
                slang_rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
                slang_rt_tex = device->create_texture(slang_rt_desc);

                if (!slang_vb || !slang_rt_tex) {
                    fprintf(stderr, "Failed to create Vulkan Slang smoke resources\n");
                    slang_artifact_ok = false;
                } else {
                    tgfx::PipelineCache slang_cache(*device);
                    tgfx::RenderContext2 slang_ctx(*device, slang_cache);
                    slang_ctx.begin_frame();

                    float clear[] = {0.0f, 0.0f, 0.0f, 1.0f};
                    slang_ctx.begin_pass(slang_rt_tex, {}, clear);
                    slang_ctx.set_viewport(0, 0, 128, 128);
                    slang_ctx.set_depth_test(false);
                    slang_ctx.set_depth_write(false);
                    slang_ctx.set_blend(false);
                    slang_ctx.set_cull(tgfx::CullMode::None);
                    slang_ctx.bind_shader(slang_vs, slang_fs);
                    slang_ctx.set_vertex_layout(slang_layout);
                    slang_ctx.use_shader_resource_layout(slang_shader);
                    slang_ctx.bind_uniform_data(
                        "u_transform",
                        mvp_column_major.data(),
                        static_cast<uint32_t>(sizeof(mvp_column_major)));
                    slang_ctx.draw_arrays(slang_vb, 3);
                    slang_ctx.end_pass();
                    slang_ctx.end_frame();
                    device->wait_idle();

                    float left_pixel[4] = {};
                    float center_pixel[4] = {};
                    bool left_read_ok = device->read_pixel_rgba8(slang_rt_tex, 32, 64, left_pixel);
                    bool center_read_ok = device->read_pixel_rgba8(slang_rt_tex, 64, 64, center_pixel);
                    printf("Slang matrix left pixel: (%.2f %.2f %.2f %.2f), center: (%.2f %.2f %.2f %.2f)\n",
                           left_pixel[0], left_pixel[1], left_pixel[2], left_pixel[3],
                           center_pixel[0], center_pixel[1], center_pixel[2], center_pixel[3]);

                    const bool left_is_shader_color =
                        left_read_ok &&
                        left_pixel[0] > 0.70f &&
                        left_pixel[1] < 0.20f &&
                        left_pixel[2] > 0.55f;
                    const bool center_is_clear =
                        center_read_ok &&
                        center_pixel[0] < 0.20f &&
                        center_pixel[1] < 0.20f &&
                        center_pixel[2] < 0.20f;
                    slang_artifact_ok = left_is_shader_color && center_is_clear;
                }
            }
        }

        if (slang_rt_tex) device->destroy(slang_rt_tex);
        if (slang_vb) device->destroy(slang_vb);
        if (!tc_shader_handle_is_invalid(slang_shader_handle)) {
            tc_shader_destroy(slang_shader_handle);
        }

        if (slang_artifact_ok && !render_fsq_artifact_smoke(*device)) {
            fprintf(stderr, "FSQ Slang artifact render smoke failed\n");
            slang_artifact_ok = false;
        }

        termin::tgfx2_set_shader_artifact_root("");
        termin::tgfx2_set_shader_cache_root("");
        termin::tgfx2_set_shader_compiler_path("");
        termin::tgfx2_set_shader_dev_compile_enabled(false);
        std::filesystem::remove_all(artifact_root, fs_ec);
        if (fs_ec) {
            fprintf(stderr, "Failed to remove Slang artifact temp root: %s (%s)\n",
                    artifact_root.string().c_str(), fs_ec.message().c_str());
        }
    }

    // Cleanup
    cmd.reset();
    device->destroy(readback_buf);
    device->destroy(rt_tex);
    device->destroy(ib);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device.reset();

    printf("\nCenter drawn: %d, Corner is blue: %d, Bound resources: %d, Slang artifacts: %d\n",
           center_drawn, corner_is_blue, bound_resource_ok, slang_artifact_ok);
    if (center_drawn && corner_is_blue && bound_resource_ok && slang_artifact_ok) {
        printf("VULKAN SMOKE TEST PASSED\n");
        return 0;
    } else {
        printf("VULKAN SMOKE TEST FAILED\n");
        return 1;
    }
#endif
}
