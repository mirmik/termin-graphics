// Smoke test for tgfx2 API: creates GL context via GLFW, draws a triangle.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

static const char* vertex_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* fragment_src = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

static const char* artifact_vertex_src = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* artifact_fragment_src = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(0.85, 0.10, 0.75, 1.0);
}
)";

static const char* invalid_fallback_src = R"(
#version 330 core
#error artifact-required smoke must not compile fallback source
void main() {}
)";

static bool write_text_file(const std::filesystem::path& path, const char* text) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        fprintf(stderr, "Failed to open artifact for writing: %s\n", path.string().c_str());
        return false;
    }
    out << text;
    if (!out) {
        fprintf(stderr, "Failed to write artifact: %s\n", path.string().c_str());
        return false;
    }
    return true;
}

int main() {
    // --- Init GLFW + GL context ---
    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(640, 480, "tgfx2 smoke test", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Window creation failed (no GPU/display?)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoaderLoadGL()) {
        fprintf(stderr, "GLAD init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    printf("GL: %s\n", glGetString(GL_VERSION));
    printf("Renderer: %s\n", glGetString(GL_RENDERER));

    // --- Create device ---
    auto device = tgfx::create_device(tgfx::BackendType::OpenGL);
    auto caps = device->capabilities();
    printf("Backend: OpenGL, max_tex: %u, compute: %s\n",
           caps.max_texture_dimension_2d,
           caps.supports_compute ? "yes" : "no");

    // Full texture readback must use the public tgfx2 top-left row order,
    // even though OpenGL glReadPixels itself returns bottom-left rows.
    tgfx::TextureDesc readback_desc;
    readback_desc.width = 2;
    readback_desc.height = 2;
    readback_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    readback_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopySrc;
    auto readback_tex = device->create_texture(readback_desc);
    const uint8_t readback_rgba[] = {
        255, 0, 0, 255,      0, 255, 0, 255,
        0, 0, 255, 255,      255, 255, 255, 255,
    };
    device->upload_texture(
        readback_tex,
        std::span<const uint8_t>(readback_rgba, sizeof(readback_rgba)));
    std::vector<float> readback_pixels(2 * 2 * 4, 0.0f);
    bool readback_ok = device->read_texture_rgba_float(readback_tex, readback_pixels.data());
    bool readback_order_ok =
        readback_ok &&
        readback_pixels[0] > 0.9f && readback_pixels[1] < 0.1f &&
        readback_pixels[4] < 0.1f && readback_pixels[5] > 0.9f &&
        readback_pixels[8] < 0.1f && readback_pixels[9] < 0.1f && readback_pixels[10] > 0.9f &&
        readback_pixels[12] > 0.9f && readback_pixels[13] > 0.9f && readback_pixels[14] > 0.9f;
    printf("OpenGL full color readback row order: %s\n", readback_order_ok ? "ok" : "failed");
    if (!readback_order_ok) {
        fprintf(stderr,
                "OpenGL full color readback mismatch: first row=(%.2f %.2f %.2f %.2f) (%.2f %.2f %.2f %.2f), second row=(%.2f %.2f %.2f %.2f) (%.2f %.2f %.2f %.2f)\n",
                readback_pixels[0], readback_pixels[1], readback_pixels[2], readback_pixels[3],
                readback_pixels[4], readback_pixels[5], readback_pixels[6], readback_pixels[7],
                readback_pixels[8], readback_pixels[9], readback_pixels[10], readback_pixels[11],
                readback_pixels[12], readback_pixels[13], readback_pixels[14], readback_pixels[15]);
        return 1;
    }
    device->destroy(readback_tex);

    // --- Create shaders ---
    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = vertex_src;
    auto vs = device->create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = fragment_src;
    auto fs = device->create_shader(fs_desc);

    printf("Shaders created: vs=%u, fs=%u\n", vs.id, fs.id);

    // --- Create pipeline ---
    tgfx::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.topology = tgfx::PrimitiveTopology::TriangleList;
    pipe_desc.depth_stencil.depth_test = false;
    pipe_desc.raster.cull = tgfx::CullMode::None;

    tgfx::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float); // x, y, r, g, b
    layout.attributes = {
        {0, tgfx::VertexFormat::Float2, 0},                      // aPos
        {1, tgfx::VertexFormat::Float3, 2 * sizeof(float)},      // aColor
    };
    pipe_desc.vertex_layouts.push_back(tgfx::make_vertex_layout_desc(layout));

    auto pipeline = device->create_pipeline(pipe_desc);
    printf("Pipeline created: id=%u\n", pipeline.id);
    if (device->pipeline_resource_layout_token(pipeline) == 0) {
        fprintf(stderr, "OpenGL pipeline resource layout token is null\n");
        return 1;
    }

    // --- Create vertex buffer ---
    float vertices[] = {
         0.0f,  0.5f,  1.f, 0.f, 0.f,  // top (red)
        -0.5f, -0.5f,  0.f, 1.f, 0.f,  // left (green)
         0.5f, -0.5f,  0.f, 0.f, 1.f,  // right (blue)
    };

    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex;
    auto vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});
    printf("Vertex buffer: id=%u\n", vb.id);

    // --- Create index buffer ---
    uint32_t indices[] = {0, 1, 2};

    tgfx::BufferDesc ib_desc;
    ib_desc.size = sizeof(indices);
    ib_desc.usage = tgfx::BufferUsage::Index;
    auto ib = device->create_buffer(ib_desc);
    device->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});
    printf("Index buffer: id=%u\n", ib.id);

    // --- Draw one frame ---
    auto cmd = device->create_command_list();
    cmd->begin();

    tgfx::RenderPassDesc pass;
    tgfx::ColorAttachmentDesc color_att;
    color_att.load = tgfx::LoadOp::Clear;
    color_att.clear_color[0] = 0.1f;
    color_att.clear_color[1] = 0.1f;
    color_att.clear_color[2] = 0.1f;
    color_att.clear_color[3] = 1.0f;
    pass.colors.push_back(color_att);

    cmd->begin_render_pass(pass);
    cmd->set_viewport(0, 0, 640, 480);
    cmd->bind_pipeline(pipeline);
    cmd->bind_vertex_buffer(0, vb);
    cmd->bind_index_buffer(ib, tgfx::IndexType::Uint32);
    cmd->draw_indexed(3);
    cmd->end_render_pass();

    cmd->end();
    device->submit(*cmd);

    // --- Verify: read back pixels (before swap, while front buffer has our frame) ---
    float pixel[4] = {0};
    glReadPixels(320, 240, 1, 1, GL_RGBA, GL_FLOAT, pixel);
    printf("Center pixel: (%.2f, %.2f, %.2f, %.2f)\n", pixel[0], pixel[1], pixel[2], pixel[3]);

    bool center_not_clear = (pixel[0] > 0.15f || pixel[1] > 0.15f || pixel[2] > 0.15f);

    float corner[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, corner);
    printf("Corner pixel: (%.2f, %.2f, %.2f, %.2f)\n", corner[0], corner[1], corner[2], corner[3]);

    bool corner_is_clear = (corner[0] < 0.15f && corner[1] < 0.15f && corner[2] < 0.15f);

    // =====================================================
    // Test 2: Render to texture (offscreen render target)
    // =====================================================
    printf("\n--- Render-to-texture test ---\n");

    // Create a 256x256 render target texture
    tgfx::TextureDesc rt_desc;
    rt_desc.width = 256;
    rt_desc.height = 256;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::Sampled;
    auto rt_tex = device->create_texture(rt_desc);
    printf("Render target texture: id=%u (%ux%u)\n", rt_tex.id, rt_desc.width, rt_desc.height);

    // Draw the same triangle into the texture
    auto cmd2 = device->create_command_list();
    cmd2->begin();

    tgfx::RenderPassDesc rt_pass;
    tgfx::ColorAttachmentDesc rt_color;
    rt_color.texture = rt_tex;
    rt_color.load = tgfx::LoadOp::Clear;
    rt_color.clear_color[0] = 0.0f;
    rt_color.clear_color[1] = 0.0f;
    rt_color.clear_color[2] = 0.2f;  // dark blue clear
    rt_color.clear_color[3] = 1.0f;
    rt_pass.colors.push_back(rt_color);

    cmd2->begin_render_pass(rt_pass);
    // viewport auto-set by begin_render_pass to 256x256
    cmd2->bind_pipeline(pipeline);
    cmd2->bind_vertex_buffer(0, vb);
    cmd2->bind_index_buffer(ib, tgfx::IndexType::Uint32);
    cmd2->draw_indexed(3);
    cmd2->end_render_pass();

    cmd2->end();
    device->submit(*cmd2);

    // Read back from the render target via temporary FBO
    GLuint readback_fbo = 0;
    auto* gl_device = static_cast<tgfx::OpenGLRenderDevice*>(device.get());
    auto* rt_gl = gl_device->get_texture(rt_tex);
    glGenFramebuffers(1, &readback_fbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readback_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           rt_gl->target, rt_gl->gl_id, 0);

    float rt_center[4] = {0};
    glReadPixels(128, 128, 1, 1, GL_RGBA, GL_FLOAT, rt_center);
    printf("RT center pixel: (%.2f, %.2f, %.2f, %.2f)\n",
           rt_center[0], rt_center[1], rt_center[2], rt_center[3]);

    float rt_corner[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, rt_corner);
    printf("RT corner pixel: (%.2f, %.2f, %.2f, %.2f)\n",
           rt_corner[0], rt_corner[1], rt_corner[2], rt_corner[3]);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &readback_fbo);

    bool rt_center_drawn = (rt_center[0] > 0.15f || rt_center[1] > 0.15f || rt_center[2] > 0.25f);
    bool rt_corner_is_blue = (rt_corner[2] > 0.15f && rt_corner[0] < 0.05f && rt_corner[1] < 0.05f);

    printf("RT test: center_drawn=%d, corner_is_blue=%d\n", rt_center_drawn, rt_corner_is_blue);

    // =====================================================
    // Test 3: tc_shader required artifacts through OpenGL
    // =====================================================
    printf("\n--- tc_shader artifact test ---\n");

    bool artifact_test_ok = false;
    const char* artifact_uuid = "termin-artifact-smoke";
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path artifact_root =
        std::filesystem::temp_directory_path() /
        ("termin-tgfx2-artifact-smoke-" + std::to_string(now));
    std::filesystem::path artifact_dir = artifact_root / "shaders" / "opengl";

    tgfx::TextureHandle artifact_rt_tex;
    tgfx::PipelineHandle artifact_pipeline;
    tc_shader_handle artifact_shader_handle = tc_shader_handle_invalid();

    std::error_code fs_ec;
    if (!std::filesystem::create_directories(artifact_dir, fs_ec) && fs_ec) {
        fprintf(stderr, "Failed to create artifact directory: %s (%s)\n",
                artifact_dir.string().c_str(), fs_ec.message().c_str());
    } else if (
        !write_text_file(artifact_dir / (std::string(artifact_uuid) + ".vert.glsl"), artifact_vertex_src) ||
        !write_text_file(artifact_dir / (std::string(artifact_uuid) + ".frag.glsl"), artifact_fragment_src)) {
        fprintf(stderr, "Failed to write shader artifacts\n");
    } else {
        termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
        artifact_shader_handle = tc_shader_from_sources_ex(
            invalid_fallback_src,
            invalid_fallback_src,
            nullptr,
            "tgfx2 artifact smoke",
            nullptr,
            artifact_uuid,
            TC_SHADER_LANGUAGE_SLANG,
            TC_SHADER_ARTIFACT_REQUIRED
        );

        tc_shader* artifact_shader = tc_shader_get(artifact_shader_handle);
        tgfx::ShaderHandle artifact_vs;
        tgfx::ShaderHandle artifact_fs;
        if (!artifact_shader) {
            fprintf(stderr, "Failed to create artifact smoke tc_shader\n");
        } else if (!gl_device->ensure_tc_shader(artifact_shader, &artifact_vs, &artifact_fs)) {
            fprintf(stderr, "Failed to load required OpenGL shader artifacts\n");
        } else {
            tgfx::PipelineDesc artifact_pipe_desc;
            artifact_pipe_desc.vertex_shader = artifact_vs;
            artifact_pipe_desc.fragment_shader = artifact_fs;
            artifact_pipe_desc.topology = tgfx::PrimitiveTopology::TriangleList;
            artifact_pipe_desc.depth_stencil.depth_test = false;
            artifact_pipe_desc.raster.cull = tgfx::CullMode::None;
            artifact_pipe_desc.vertex_layouts.push_back(tgfx::make_vertex_layout_desc(layout));

            artifact_pipeline = device->create_pipeline(artifact_pipe_desc);
            printf("Artifact pipeline created: id=%u\n", artifact_pipeline.id);

            tgfx::TextureDesc artifact_rt_desc;
            artifact_rt_desc.width = 64;
            artifact_rt_desc.height = 64;
            artifact_rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
            artifact_rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::Sampled;
            artifact_rt_tex = device->create_texture(artifact_rt_desc);
            printf("Artifact render target texture: id=%u\n", artifact_rt_tex.id);

            if (!artifact_pipeline || !artifact_rt_tex) {
                fprintf(stderr, "Failed to create artifact pipeline resources\n");
            } else {
                auto cmd3 = device->create_command_list();
                cmd3->begin();

                tgfx::RenderPassDesc artifact_pass;
                tgfx::ColorAttachmentDesc artifact_color;
                artifact_color.texture = artifact_rt_tex;
                artifact_color.load = tgfx::LoadOp::Clear;
                artifact_color.clear_color[0] = 0.0f;
                artifact_color.clear_color[1] = 0.0f;
                artifact_color.clear_color[2] = 0.0f;
                artifact_color.clear_color[3] = 1.0f;
                artifact_pass.colors.push_back(artifact_color);

                cmd3->begin_render_pass(artifact_pass);
                cmd3->bind_pipeline(artifact_pipeline);
                cmd3->bind_vertex_buffer(0, vb);
                cmd3->bind_index_buffer(ib, tgfx::IndexType::Uint32);
                cmd3->draw_indexed(3);
                cmd3->end_render_pass();

                cmd3->end();
                device->submit(*cmd3);

                float artifact_center[4] = {0};
                bool artifact_read_ok = device->read_pixel_rgba8(artifact_rt_tex, 32, 32, artifact_center);
                printf("Artifact center pixel: (%.2f, %.2f, %.2f, %.2f), read_ok=%d\n",
                       artifact_center[0], artifact_center[1], artifact_center[2], artifact_center[3],
                       artifact_read_ok ? 1 : 0);

                artifact_test_ok = artifact_read_ok &&
                    artifact_center[0] > 0.70f &&
                    artifact_center[1] < 0.25f &&
                    artifact_center[2] > 0.55f;
            }
        }
    }

    termin::tgfx2_set_shader_artifact_root("");
    std::filesystem::remove_all(artifact_root, fs_ec);
    if (fs_ec) {
        fprintf(stderr, "Failed to remove artifact temp root: %s (%s)\n",
                artifact_root.string().c_str(), fs_ec.message().c_str());
    }
    printf("Artifact test: artifact_loaded_and_drawn=%d\n", artifact_test_ok);

    // Show the screen-rendered frame for 3 seconds
    glfwSwapBuffers(window);
    printf("Showing window for 3 seconds...\n");
    double start = glfwGetTime();
    while (glfwGetTime() - start < 3.0 && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // --- Cleanup ---
    device->destroy(rt_tex);
    if (artifact_rt_tex) device->destroy(artifact_rt_tex);
    if (artifact_pipeline) device->destroy(artifact_pipeline);
    if (!tc_shader_handle_is_invalid(artifact_shader_handle)) {
        tc_shader_destroy(artifact_shader_handle);
    }
    device->destroy(vb);
    device->destroy(ib);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device.reset();

    glfwDestroyWindow(window);
    glfwTerminate();

    // --- Result ---
    bool test1_ok = center_not_clear && corner_is_clear;
    bool test2_ok = rt_center_drawn && rt_corner_is_blue;

    printf("\nTest 1 (draw to screen): %s\n", test1_ok ? "PASSED" : "FAILED");
    printf("Test 2 (render to texture): %s\n", test2_ok ? "PASSED" : "FAILED");
    printf("Test 3 (tc_shader artifacts): %s\n", artifact_test_ok ? "PASSED" : "FAILED");

    if (test1_ok && test2_ok && artifact_test_ok) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\nSOME TESTS FAILED\n");
        return 1;
    }
}
