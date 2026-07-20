// test_backend_window_triangle.cpp - Backend-neutral triangle smoke.
//
// One binary, one API, both backends. Pick via TERMIN_BACKEND env-var:
//   TERMIN_BACKEND=opengl ./backend_window_triangle
//   TERMIN_BACKEND=vulkan ./backend_window_triangle
//
// The app code below has zero mention of GL vs Vulkan. All backend
// specifics are inside the lightweight BackendWindow + IRenderDevice.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <cstdio>
#include <cstdlib>

#include "termin/platform/sdl_backend_window.hpp"
#include "tgfx2/graphics_host.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"

int main() {
try {
    const char* backend_env = std::getenv("TERMIN_BACKEND");
    printf("TERMIN_BACKEND=%s\n", backend_env ? backend_env : "(unset, using library default)");

    double max_seconds = 3.0;
    if (const char* max_seconds_env = std::getenv("TERMIN_TEST_MAX_SECONDS")) {
        char* end = nullptr;
        double parsed = std::strtod(max_seconds_env, &end);
        if (end != max_seconds_env && parsed >= 0.0) {
            max_seconds = parsed;
        }
    }
    printf("TERMIN_TEST_MAX_SECONDS=%.2f\n", max_seconds);

    tgfx::PresentationMode presentation_mode = tgfx::PresentationMode::VSync;
    if (const char* mode_env = std::getenv("TERMIN_TEST_PRESENTATION_MODE")) {
        if (std::strcmp(mode_env, "immediate") == 0) {
            presentation_mode = tgfx::PresentationMode::Immediate;
        } else if (std::strcmp(mode_env, "vsync") != 0) {
            fprintf(stderr, "Unknown TERMIN_TEST_PRESENTATION_MODE=%s\n", mode_env);
            return 1;
        }
    }
    printf(
        "TERMIN_TEST_PRESENTATION_MODE=%s\n",
        presentation_mode == tgfx::PresentationMode::VSync ? "vsync" : "immediate");

    auto graphics_session = termin::create_native_windowed_graphics();
    termin::WindowConfig window_config{
        "BackendWindow triangle smoke", 800, 600, presentation_mode};
    auto window = graphics_session->create_window(window_config);
    auto& win = *window;
    if (win.requested_presentation_mode() != presentation_mode) {
        fprintf(stderr, "BackendWindow requested presentation mode mismatch\n");
        return 1;
    }
    printf(
        "APPLIED_PRESENTATION_MODE=%s\n",
        win.presentation_mode() == tgfx::PresentationMode::VSync ? "vsync" : "immediate");
    tgfx::IRenderDevice* dev = &graphics_session->graphics().device();
    if (!dev) {
        fprintf(stderr, "BackendWindow has no device\n");
        return 1;
    }

    // GLSL 410 core — explicit location qualifiers on ALL stage I/O.
    // Needed for Vulkan (SPIR-V strict requirement) and legal on
    // OpenGL 4.1+ (ARB_separate_shader_objects graduated in 4.1).
    // BackendWindow's GL context is configured at 4.1 core for exactly
    // this portability reason.
    const char* vs_src = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 0) out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";
    const char* fs_src = R"GLSL(
#version 410 core
layout(location = 0) in vec3 vColor;
layout(location = 0) out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
}
)GLSL";

    tgfx::ShaderDesc vsd; vsd.stage = tgfx::ShaderStage::Vertex; vsd.source = vs_src;
    tgfx::ShaderDesc fsd; fsd.stage = tgfx::ShaderStage::Fragment; fsd.source = fs_src;
    tgfx::ShaderHandle vs = dev->create_shader(vsd);
    tgfx::ShaderHandle fs = dev->create_shader(fsd);

    // Offscreen render target — we draw the triangle into here and
    // SDLBackendWindow::present() composes it onto the actual surface.
    tgfx::TextureDesc rt_desc;
    rt_desc.width = 800;
    rt_desc.height = 600;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment |
                    tgfx::TextureUsage::CopySrc |
                    tgfx::TextureUsage::Sampled;
    tgfx::TextureHandle rt = dev->create_texture(rt_desc);

    tgfx::PipelineDesc pd;
    pd.vertex_shader = vs;
    pd.fragment_shader = fs;
    pd.topology = tgfx::PrimitiveTopology::TriangleList;
    pd.depth_stencil.depth_test = false;
    pd.depth_stencil.depth_write = false;
    pd.raster.cull = tgfx::CullMode::None;
    pd.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};

    tgfx::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.attributes = {
        {0, tgfx::VertexFormat::Float2, 0},
        {1, tgfx::VertexFormat::Float3, 2 * sizeof(float)},
    };
    pd.vertex_layouts.push_back(tgfx::make_vertex_layout_desc(layout));
    tgfx::PipelineHandle pipe = dev->create_pipeline(pd);

    float vertices[] = {
         0.0f,  0.6f,   1.f, 0.f, 0.f,
        -0.6f, -0.6f,   0.f, 1.f, 0.f,
         0.6f, -0.6f,   0.f, 0.f, 1.f,
    };
    tgfx::BufferDesc vbd; vbd.size = sizeof(vertices);
    vbd.usage = tgfx::BufferUsage::Vertex; vbd.cpu_visible = true;
    tgfx::BufferHandle vb = dev->create_buffer(vbd);
    dev->upload_buffer(vb, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    uint32_t indices[] = {0, 1, 2};
    tgfx::BufferDesc ibd; ibd.size = sizeof(indices);
    ibd.usage = tgfx::BufferUsage::Index; ibd.cpu_visible = true;
    tgfx::BufferHandle ib = dev->create_buffer(ibd);
    dev->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

    auto start = std::chrono::steady_clock::now();
    uint64_t frame_index = 0;

    while (!win.should_close()) {
        win.poll_events();

        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration<float>(now - start).count();
        if (max_seconds > 0.0 && t >= max_seconds) {
            break;
        }
        // Render triangle into `rt` through the tgfx2 command list —
        // same code both backends.
        auto cmd = dev->create_command_list();
        cmd->begin();

        tgfx::RenderPassDesc pass;
        tgfx::ColorAttachmentDesc att;
        att.texture = rt;
        att.load = tgfx::LoadOp::Clear;
        att.clear_color[0] = 0.5f + 0.5f * std::sin(t * 1.7f);
        att.clear_color[1] = 0.1f;
        att.clear_color[2] = 0.5f + 0.5f * std::cos(t * 1.1f);
        att.clear_color[3] = 1.0f;
        pass.colors.push_back(att);

        cmd->begin_render_pass(pass);
        cmd->bind_pipeline(pipe);
        cmd->bind_vertex_buffer(0, vb);
        cmd->bind_index_buffer(ib, tgfx::IndexType::Uint32);
        cmd->draw_indexed(3);
        cmd->end_render_pass();

        cmd->end();
        dev->submit(*cmd);

        // The ONE place where "publish a frame" happens. GL does blit
        // + SwapWindow; Vulkan does acquire + compose + present.
        win.present(rt);

        ++frame_index;
    }

    dev->destroy(ib);
    dev->destroy(vb);
    dev->destroy(pipe);
    dev->destroy(rt);
    dev->destroy(fs);
    dev->destroy(vs);
    window.reset();
    graphics_session->close();

    printf("Frames: %llu. OK.\n", static_cast<unsigned long long>(frame_index));
    return 0;
} catch (const std::exception& e) {
    fprintf(stderr, "backend_window_triangle failed: %s\n", e.what());
    if (std::strstr(e.what(), "No available video device") ||
        std::strstr(e.what(), "Vulkan support is either not configured in SDL")) {
        return 77;
    }
    return 1;
}
}
