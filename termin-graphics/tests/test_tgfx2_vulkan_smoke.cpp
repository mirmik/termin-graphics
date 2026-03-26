// Smoke test for tgfx2 Vulkan backend: offscreen render to texture.
// No swapchain — we render to a texture and read back pixels.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "tgfx2/enums.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"

#ifdef TGFX2_HAS_VULKAN
#include "tgfx2/vulkan/vulkan_render_device.hpp"
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

int main() {
#ifndef TGFX2_HAS_VULKAN
    printf("Vulkan backend not compiled, skipping test\n");
    return 0;
#else
    printf("--- tgfx2 Vulkan smoke test (offscreen) ---\n");

    // Create Vulkan device (no surface — offscreen only)
    tgfx2::VulkanDeviceCreateInfo info;
    info.enable_validation = true;

    std::unique_ptr<tgfx2::IRenderDevice> device;
    try {
        device = std::make_unique<tgfx2::VulkanRenderDevice>(info);
    } catch (const std::exception& e) {
        fprintf(stderr, "Failed to create Vulkan device: %s\n", e.what());
        return 1;
    }

    auto caps = device->capabilities();
    printf("Backend: Vulkan, max_tex: %u, compute: %s, geometry: %s\n",
           caps.max_texture_dimension_2d,
           caps.supports_compute ? "yes" : "no",
           caps.supports_geometry_shaders ? "yes" : "no");

    // --- Create shaders (GLSL source — compiled to SPIR-V by shaderc) ---
    tgfx2::ShaderHandle vs, fs;
    try {
        tgfx2::ShaderDesc vs_desc;
        vs_desc.stage = tgfx2::ShaderStage::Vertex;
        vs_desc.source = vertex_src;
        vs = device->create_shader(vs_desc);

        tgfx2::ShaderDesc fs_desc;
        fs_desc.stage = tgfx2::ShaderStage::Fragment;
        fs_desc.source = fragment_src;
        fs = device->create_shader(fs_desc);
    } catch (const std::exception& e) {
        fprintf(stderr, "Shader creation failed: %s\n", e.what());
        return 1;
    }

    printf("Shaders created: vs=%u, fs=%u\n", vs.id, fs.id);

    // --- Create render target texture (256x256) ---
    tgfx2::TextureDesc rt_desc;
    rt_desc.width = 256;
    rt_desc.height = 256;
    rt_desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx2::TextureUsage::ColorAttachment | tgfx2::TextureUsage::CopySrc;
    auto rt_tex = device->create_texture(rt_desc);
    printf("Render target: id=%u\n", rt_tex.id);

    // --- Create pipeline ---
    tgfx2::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.topology = tgfx2::PrimitiveTopology::TriangleList;
    pipe_desc.depth_stencil.depth_test = false;
    pipe_desc.depth_stencil.depth_write = false;
    pipe_desc.raster.cull = tgfx2::CullMode::None;
    pipe_desc.color_formats = {tgfx2::PixelFormat::RGBA8_UNorm};

    tgfx2::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.attributes = {
        {0, tgfx2::VertexFormat::Float2, 0},
        {1, tgfx2::VertexFormat::Float3, 2 * sizeof(float)},
    };
    pipe_desc.vertex_layouts.push_back(layout);

    auto pipeline = device->create_pipeline(pipe_desc);
    printf("Pipeline: id=%u\n", pipeline.id);

    // --- Create vertex buffer (CPU-visible for simplicity) ---
    float vertices[] = {
         0.0f,  0.5f,  1.f, 0.f, 0.f,
        -0.5f, -0.5f,  0.f, 1.f, 0.f,
         0.5f, -0.5f,  0.f, 0.f, 1.f,
    };

    tgfx2::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx2::BufferUsage::Vertex;
    vb_desc.cpu_visible = true;
    auto vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    uint32_t indices[] = {0, 1, 2};
    tgfx2::BufferDesc ib_desc;
    ib_desc.size = sizeof(indices);
    ib_desc.usage = tgfx2::BufferUsage::Index;
    ib_desc.cpu_visible = true;
    auto ib = device->create_buffer(ib_desc);
    device->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

    printf("Buffers created\n");

    // --- Draw ---
    auto cmd = device->create_command_list();
    cmd->begin();

    tgfx2::RenderPassDesc pass;
    tgfx2::ColorAttachmentDesc color_att;
    color_att.texture = rt_tex;
    color_att.load = tgfx2::LoadOp::Clear;
    color_att.clear_color[0] = 0.0f;
    color_att.clear_color[1] = 0.0f;
    color_att.clear_color[2] = 0.2f;
    color_att.clear_color[3] = 1.0f;
    pass.colors.push_back(color_att);

    cmd->begin_render_pass(pass);
    cmd->bind_pipeline(pipeline);
    cmd->bind_vertex_buffer(0, vb);
    cmd->bind_index_buffer(ib, tgfx2::IndexType::Uint32);
    cmd->draw_indexed(3);
    cmd->end_render_pass();

    cmd->end();
    device->submit(*cmd);

    printf("Draw submitted\n");

    // --- Read back via staging buffer ---
    const uint32_t pixel_count = 256 * 256;
    const uint32_t byte_size = pixel_count * 4; // RGBA8

    tgfx2::BufferDesc readback_desc;
    readback_desc.size = byte_size;
    readback_desc.usage = tgfx2::BufferUsage::CopyDst;
    readback_desc.cpu_visible = true;
    auto readback_buf = device->create_buffer(readback_desc);

    // Copy texture to buffer
    auto* vk_device = static_cast<tgfx2::VulkanRenderDevice*>(device.get());
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

    // Cleanup
    device->destroy(readback_buf);
    device->destroy(rt_tex);
    device->destroy(vb);
    device->destroy(ib);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device.reset();

    printf("\nCenter drawn: %d, Corner is blue: %d\n", center_drawn, corner_is_blue);
    if (center_drawn && corner_is_blue) {
        printf("VULKAN SMOKE TEST PASSED\n");
        return 0;
    } else {
        printf("VULKAN SMOKE TEST FAILED\n");
        return 1;
    }
#endif
}
