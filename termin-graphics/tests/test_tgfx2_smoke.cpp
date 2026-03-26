// Smoke test for tgfx2 API: creates GL context via GLFW, draws a triangle.
#include <cstdio>
#include <cstdlib>

#include <glad/glad.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"

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
    auto device = tgfx2::create_device(tgfx2::BackendType::OpenGL);
    auto caps = device->capabilities();
    printf("Backend: OpenGL, max_tex: %u, compute: %s\n",
           caps.max_texture_dimension_2d,
           caps.supports_compute ? "yes" : "no");

    // --- Create shaders ---
    tgfx2::ShaderDesc vs_desc;
    vs_desc.stage = tgfx2::ShaderStage::Vertex;
    vs_desc.source = vertex_src;
    auto vs = device->create_shader(vs_desc);

    tgfx2::ShaderDesc fs_desc;
    fs_desc.stage = tgfx2::ShaderStage::Fragment;
    fs_desc.source = fragment_src;
    auto fs = device->create_shader(fs_desc);

    printf("Shaders created: vs=%u, fs=%u\n", vs.id, fs.id);

    // --- Create pipeline ---
    tgfx2::PipelineDesc pipe_desc;
    pipe_desc.vertex_shader = vs;
    pipe_desc.fragment_shader = fs;
    pipe_desc.topology = tgfx2::PrimitiveTopology::TriangleList;
    pipe_desc.depth_stencil.depth_test = false;
    pipe_desc.raster.cull = tgfx2::CullMode::None;

    tgfx2::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float); // x, y, r, g, b
    layout.attributes = {
        {0, tgfx2::VertexFormat::Float2, 0},                      // aPos
        {1, tgfx2::VertexFormat::Float3, 2 * sizeof(float)},      // aColor
    };
    pipe_desc.vertex_layouts.push_back(layout);

    auto pipeline = device->create_pipeline(pipe_desc);
    printf("Pipeline created: id=%u\n", pipeline.id);

    // --- Create vertex buffer ---
    float vertices[] = {
         0.0f,  0.5f,  1.f, 0.f, 0.f,  // top (red)
        -0.5f, -0.5f,  0.f, 1.f, 0.f,  // left (green)
         0.5f, -0.5f,  0.f, 0.f, 1.f,  // right (blue)
    };

    tgfx2::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx2::BufferUsage::Vertex;
    auto vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});
    printf("Vertex buffer: id=%u\n", vb.id);

    // --- Create index buffer ---
    uint32_t indices[] = {0, 1, 2};

    tgfx2::BufferDesc ib_desc;
    ib_desc.size = sizeof(indices);
    ib_desc.usage = tgfx2::BufferUsage::Index;
    auto ib = device->create_buffer(ib_desc);
    device->upload_buffer(ib, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});
    printf("Index buffer: id=%u\n", ib.id);

    // --- Draw one frame ---
    auto cmd = device->create_command_list();
    cmd->begin();

    tgfx2::RenderPassDesc pass;
    tgfx2::ColorAttachmentDesc color_att;
    color_att.load = tgfx2::LoadOp::Clear;
    color_att.clear_color[0] = 0.1f;
    color_att.clear_color[1] = 0.1f;
    color_att.clear_color[2] = 0.1f;
    color_att.clear_color[3] = 1.0f;
    pass.colors.push_back(color_att);

    cmd->begin_render_pass(pass);
    cmd->set_viewport(0, 0, 640, 480);
    cmd->bind_pipeline(pipeline);
    cmd->bind_vertex_buffer(0, vb);
    cmd->bind_index_buffer(ib, tgfx2::IndexType::Uint32);
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
    tgfx2::TextureDesc rt_desc;
    rt_desc.width = 256;
    rt_desc.height = 256;
    rt_desc.format = tgfx2::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx2::TextureUsage::ColorAttachment | tgfx2::TextureUsage::Sampled;
    auto rt_tex = device->create_texture(rt_desc);
    printf("Render target texture: id=%u (%ux%u)\n", rt_tex.id, rt_desc.width, rt_desc.height);

    // Draw the same triangle into the texture
    auto cmd2 = device->create_command_list();
    cmd2->begin();

    tgfx2::RenderPassDesc rt_pass;
    tgfx2::ColorAttachmentDesc rt_color;
    rt_color.texture = rt_tex;
    rt_color.load = tgfx2::LoadOp::Clear;
    rt_color.clear_color[0] = 0.0f;
    rt_color.clear_color[1] = 0.0f;
    rt_color.clear_color[2] = 0.2f;  // dark blue clear
    rt_color.clear_color[3] = 1.0f;
    rt_pass.colors.push_back(rt_color);

    cmd2->begin_render_pass(rt_pass);
    // viewport auto-set by begin_render_pass to 256x256
    cmd2->bind_pipeline(pipeline);
    cmd2->bind_vertex_buffer(0, vb);
    cmd2->bind_index_buffer(ib, tgfx2::IndexType::Uint32);
    cmd2->draw_indexed(3);
    cmd2->end_render_pass();

    cmd2->end();
    device->submit(*cmd2);

    // Read back from the render target via temporary FBO
    GLuint readback_fbo = 0;
    auto* gl_device = static_cast<tgfx2::OpenGLRenderDevice*>(device.get());
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

    // Show the screen-rendered frame for 3 seconds
    glfwSwapBuffers(window);
    printf("Showing window for 3 seconds...\n");
    double start = glfwGetTime();
    while (glfwGetTime() - start < 3.0 && !glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // --- Cleanup ---
    device->destroy(rt_tex);
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

    if (test1_ok && test2_ok) {
        printf("\nALL TESTS PASSED\n");
        return 0;
    } else {
        printf("\nSOME TESTS FAILED\n");
        return 1;
    }
}
