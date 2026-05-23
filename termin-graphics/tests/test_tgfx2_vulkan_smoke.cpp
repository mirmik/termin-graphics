// Smoke test for tgfx2 Vulkan backend: offscreen render to texture.
// No swapchain — we render to a texture and read back pixels.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
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
    tgfx::VulkanDeviceCreateInfo info;
    info.enable_validation = true;

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

    // Cleanup
    cmd.reset();
    device->destroy(readback_buf);
    device->destroy(rt_tex);
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
