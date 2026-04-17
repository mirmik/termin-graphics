// tgfx2 Vulkan on-screen smoke test — triangle via full pipeline.
//
// Step-by-step what this proves end-to-end under Vulkan:
//   1. SDL_WINDOW_VULKAN host wiring (extensions, surface_factory).
//   2. VulkanRenderDevice boot (instance, physical, logical, VMA).
//   3. VulkanSwapchain construction & per-frame acquire/present.
//   4. Shader compile (GLSL → SPIR-V via shaderc, V.3).
//   5. Pipeline with vertex layout + two shaders.
//   6. Vertex / index buffer upload via VMA-backed buffers.
//   7. tgfx2 ICommandList render pass against an offscreen texture.
//   8. Raw vkCmdBlitImage composite onto the acquired swapchain image.
//   9. Frame pacing via per-in-flight fences & semaphores.
//
// Auto-exits after ~3 seconds so it can run unattended.
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifdef TGFX2_HAS_VULKAN
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/vulkan/vulkan_render_device.hpp"
#include "tgfx2/vulkan/vulkan_swapchain.hpp"
#endif

#ifdef TGFX2_HAS_VULKAN
// Clamped sine that yields a gentle tint the user can see changing.
static float pulse(float t, float phase) {
    return 0.5f + 0.5f * std::sin(t * 1.7f + phase);
}
#endif

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
#ifndef TGFX2_HAS_VULKAN
    printf("Vulkan backend not compiled, skipping test\n");
    return 0;
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    constexpr int kWidth = 800;
    constexpr int kHeight = 600;

    SDL_Window* window = SDL_CreateWindow(
        "tgfx2 Vulkan — triangle smoke",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kWidth, kHeight, SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    uint32_t ext_count = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, nullptr);
    std::vector<const char*> extensions(ext_count);
    SDL_Vulkan_GetInstanceExtensions(window, &ext_count, extensions.data());

    tgfx::VulkanDeviceCreateInfo info;
    info.enable_validation = true;
    info.instance_extensions = extensions;
    info.swapchain_width = kWidth;
    info.swapchain_height = kHeight;
    info.surface_factory = [window](VkInstance inst) -> VkSurfaceKHR {
        VkSurfaceKHR surf = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(window, inst, &surf)) {
            fprintf(stderr, "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError());
        }
        return surf;
    };

    std::unique_ptr<tgfx::VulkanRenderDevice> device;
    try {
        device = std::make_unique<tgfx::VulkanRenderDevice>(info);
    } catch (const std::exception& e) {
        fprintf(stderr, "VulkanRenderDevice failed: %s\n", e.what());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    tgfx::VulkanSwapchain* sc = device->swapchain();
    if (!sc) {
        fprintf(stderr, "No swapchain — surface_factory didn't give us one\n");
        return 1;
    }

    printf("Swapchain: %ux%u, %u images, format=%d\n",
           sc->width(), sc->height(), sc->image_count(), sc->format());

    // ------------------------------------------------------------------
    // Shaders: simple per-vertex-colour triangle (Vulkan-style GLSL 450).
    // ------------------------------------------------------------------
    const char* vs_src = R"GLSL(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";

    const char* fs_src = R"GLSL(
#version 450 core
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)GLSL";

    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = vs_src;
    tgfx::ShaderHandle vs = device->create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = fs_src;
    tgfx::ShaderHandle fs = device->create_shader(fs_desc);
    printf("Shaders compiled: vs=%u fs=%u\n", vs.id, fs.id);

    // ------------------------------------------------------------------
    // Offscreen render target. We don't render directly into swapchain
    // images because that would require wrapping them as tgfx2
    // TextureHandles — straightforward but needs a Vulkan-specific
    // register_external path we haven't written yet. Instead we draw
    // into a device-owned texture and blit it onto the swapchain at
    // the end of the frame (same pattern RenderEngine uses on OpenGL).
    // ------------------------------------------------------------------
    tgfx::TextureDesc rt_desc;
    rt_desc.width = static_cast<uint32_t>(kWidth);
    rt_desc.height = static_cast<uint32_t>(kHeight);
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment |
                    tgfx::TextureUsage::CopySrc |
                    tgfx::TextureUsage::Sampled;
    tgfx::TextureHandle rt_tex = device->create_texture(rt_desc);

    // ------------------------------------------------------------------
    // Pipeline. Color format must match the RT texture, not the
    // swapchain — the render pass renders into rt_tex.
    // ------------------------------------------------------------------
    tgfx::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.topology = tgfx::PrimitiveTopology::TriangleList;
    pipe_desc.depth_stencil.depth_test = false;
    pipe_desc.depth_stencil.depth_write = false;
    pipe_desc.raster.cull = tgfx::CullMode::None;
    pipe_desc.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};

    tgfx::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.attributes = {
        {0, tgfx::VertexFormat::Float2, 0},
        {1, tgfx::VertexFormat::Float3, 2 * sizeof(float)},
    };
    pipe_desc.vertex_layouts.push_back(layout);

    tgfx::PipelineHandle pipe = device->create_pipeline(pipe_desc);

    // Vertex + index buffers. CPU-visible for trivial upload.
    float vertices[] = {
         0.0f,  0.6f,   1.f, 0.f, 0.f,
        -0.6f, -0.6f,   0.f, 1.f, 0.f,
         0.6f, -0.6f,   0.f, 0.f, 1.f,
    };
    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex;
    vb_desc.cpu_visible = true;
    tgfx::BufferHandle vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    uint32_t indices[] = {0, 1, 2};
    tgfx::BufferDesc ib_desc;
    ib_desc.size = sizeof(indices);
    ib_desc.usage = tgfx::BufferUsage::Index;
    ib_desc.cpu_visible = true;
    tgfx::BufferHandle ib = device->create_buffer(ib_desc);
    device->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

    auto start = std::chrono::steady_clock::now();
    uint64_t frame_index = 0;
    bool running = true;
    SDL_Event ev;

    while (running) {
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            else if (ev.type == SDL_KEYDOWN &&
                     ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                running = false;
            }
        }
        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration<float>(now - start).count();
        if (t > 3.0f) running = false;

        // ------------------------------------------------------------
        // Draw the triangle into rt_tex via the tgfx2 API.
        // submit() here also does vkQueueWaitIdle — by the time it
        // returns, rt_tex has the fragment shader's output.
        // ------------------------------------------------------------
        {
            auto cmd = device->create_command_list();
            cmd->begin();

            tgfx::RenderPassDesc pass;
            tgfx::ColorAttachmentDesc color_att;
            color_att.texture = rt_tex;
            color_att.load = tgfx::LoadOp::Clear;
            color_att.clear_color[0] = pulse(t, 0.0f) * 0.1f;
            color_att.clear_color[1] = pulse(t, 2.1f) * 0.1f;
            color_att.clear_color[2] = 0.15f;
            color_att.clear_color[3] = 1.0f;
            pass.colors.push_back(color_att);

            cmd->begin_render_pass(pass);
            cmd->bind_pipeline(pipe);
            cmd->bind_vertex_buffer(0, vb);
            cmd->bind_index_buffer(ib, tgfx::IndexType::Uint32);
            cmd->draw_indexed(3);
            cmd->end_render_pass();

            cmd->end();
            device->submit(*cmd);
        }

        // One-liner present: swapchain handles acquire/compose/submit/
        // present/advance internally. Returns true on OUT_OF_DATE —
        // recreate before next frame.
        if (sc->compose_and_present(rt_tex)) {
            int w, h;
            SDL_Vulkan_GetDrawableSize(window, &w, &h);
            sc->recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        }
        ++frame_index;
    }

    vkDeviceWaitIdle(device->device());

    device->destroy(ib);
    device->destroy(vb);
    device->destroy(pipe);
    device->destroy(rt_tex);
    device->destroy(fs);
    device->destroy(vs);
    device.reset();

    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("Frames rendered: %llu. TRIANGLE OK.\n",
           static_cast<unsigned long long>(frame_index));
    return 0;
#endif
}
