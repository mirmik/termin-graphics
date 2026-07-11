// Quick smoke: verify per-pipeline descriptor set layout is built correctly.
// Creates a Vulkan device, a simple shader with one UBO, and checks the
// reflected bindings.
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <span>
#include <stdexcept>
#include <memory>
#include <vector>

#include "tgfx/resources/tc_shader.h"
#include "tgfx2/enums.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#endif

static const char* vertex_src = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(binding = 0) uniform PerFrame { mat4 mvp; } u_pf;
void main() {
    gl_Position = u_pf.mvp * vec4(aPos, 0.0, 1.0);
}
)";

static const char* fragment_src = R"(
#version 450 core
layout(location = 0) out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

int main() {
#ifdef TGFX2_HAS_VULKAN
    printf("--- Per-pipeline layout smoke ---\n");

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

    // Create shaders with known bindings
    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = vertex_src;
    vs_desc.debug_name = "smoke:vertex";
    auto vs = device->create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = fragment_src;
    fs_desc.debug_name = "smoke:fragment";
    auto fs = device->create_shader(fs_desc);

    // Check reflected bindings (backend-specific)
    auto* vk_dev = static_cast<tgfx::VulkanRenderDevice*>(device.get());
    auto* vk_vs = vk_dev->get_shader(vs);
    auto* vk_fs = vk_dev->get_shader(fs);

    printf("VS bindings: %zu\n", vk_vs->descriptor_bindings.size());
    for (const auto& b : vk_vs->descriptor_bindings) {
        printf("  binding=%u type=%u count=%u\n", b.binding,
               static_cast<unsigned>(b.descriptor_type), b.count);
    }
    printf("FS bindings: %zu\n", vk_fs->descriptor_bindings.size());
    for (const auto& b : vk_fs->descriptor_bindings) {
        printf("  binding=%u type=%u count=%u\n", b.binding,
               static_cast<unsigned>(b.descriptor_type), b.count);
    }

    // Create pipeline — descriptor set layout is built from reflection
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
    layout.stride = 2 * sizeof(float);
    layout.attributes = {{0, tgfx::VertexFormat::Float2, 0}};
    pipe_desc.vertex_layouts.push_back(tgfx::make_vertex_layout_desc(layout));

    auto pipeline = device->create_pipeline(pipe_desc);

    // Verify the pipeline got a descriptor set layout
    auto* vk_pipe = vk_dev->get_pipeline(pipeline);
    printf("Pipeline descriptor_set_layout: %s\n",
           vk_pipe->descriptor_set_layout != VK_NULL_HANDLE ? "valid" : "NULL");

    // Create a resource set against the pipeline's layout
    tgfx::BufferDesc ubo_desc;
    ubo_desc.size = 64; // mat4
    ubo_desc.usage = tgfx::BufferUsage::Uniform;
    ubo_desc.cpu_visible = true;
    auto ubo = device->create_buffer(ubo_desc);
    const std::array<float, 16> identity_mvp = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    device->upload_buffer(ubo, std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(identity_mvp.data()),
        sizeof(float) * identity_mvp.size()));

    tgfx::BoundResourceSetDesc rs_desc;
    rs_desc.resource_layout_token = device->pipeline_resource_layout_token(pipeline);
    tgfx::BoundResourceBinding rb;
    rb.slot.kind = tgfx::ShaderResourceKind::ConstantBuffer;
    rb.slot.scope = tgfx::ShaderResourceScope::Draw;
    rb.slot.stage_mask = TC_SHADER_STAGE_VERTEX;
    rb.slot.placement.kind = tgfx::BackendPlacementKind::VulkanDescriptor;
    rb.slot.placement.vulkan.set = 0;
    rb.slot.placement.vulkan.binding = 0;
    rb.slot.placement.vulkan.descriptor_kind = tgfx::BackendDescriptorKind::UniformBuffer;
    rb.slot.debug_name = "mvp";
    rb.value.kind = tgfx::BoundResourceKind::UniformBuffer;
    rb.value.buffer = ubo;
    rb.value.range = 64;
    rs_desc.bindings.push_back(rb);

    auto rset = device->create_bound_resource_set(rs_desc);
    printf("Resource set: id=%u\n", rset.id);
    printf("Resource set handle valid: %s\n", rset.id != 0 ? "yes" : "no");

    // Quick draw test — if set layout matches, Vulkan won't complain
    tgfx::TextureDesc rt_desc;
    rt_desc.width = 64;
    rt_desc.height = 64;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    auto rt = device->create_texture(rt_desc);

    float vertices[] = {0, 0.5f, -0.5f, -0.5f, 0.5f, -0.5f};
    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex;
    vb_desc.cpu_visible = true;
    auto vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)));

    auto cmd = device->create_command_list();
    cmd->begin();

    tgfx::RenderPassDesc pass;
    tgfx::ColorAttachmentDesc ca;
    ca.texture = rt;
    ca.load = tgfx::LoadOp::Clear;
    ca.clear_color[0] = 0.0f; ca.clear_color[1] = 0.0f;
    ca.clear_color[2] = 0.0f; ca.clear_color[3] = 1.0f;
    pass.colors.push_back(ca);

    cmd->begin_render_pass(pass);
    cmd->bind_pipeline(pipeline);
    cmd->bind_resource_set(rset);
    cmd->bind_vertex_buffer(0, vb);
    cmd->draw(3);
    cmd->end_render_pass();
    cmd->end();
    device->submit(*cmd);
    device->wait_idle();

    float pixel[4] = {};
    bool ok = device->read_pixel_rgba8(rt, 32, 32, pixel);
    printf("Center pixel read: %s (%.2f %.2f %.2f %.2f)\n",
           ok ? "ok" : "fail", pixel[0], pixel[1], pixel[2], pixel[3]);

    bool test_passed = ok && pixel[0] > 0.5f && pixel[1] < 0.2f;  // red

    // Cleanup
    device->destroy(rset);
    device->destroy(ubo);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device->destroy(rt);
    device->destroy(vb);
    device.reset();

    printf("\n%s\n", test_passed ? "PER-PIPELINE LAYOUT SMOKE: PASSED" : "PER-PIPELINE LAYOUT SMOKE: FAILED");
    return test_passed ? 0 : 1;
#else
    printf("Vulkan not compiled — skipping\n");
    return 0;
#endif
}
