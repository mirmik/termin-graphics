// Test: RenderContext2 + PipelineCache draw a triangle and verify pixels.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <tgfx2/opengl/opengl_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>

static int test_count = 0;
static int pass_count = 0;

#define CHECK(cond, msg) do { \
    test_count++; \
    if (cond) { pass_count++; printf("  PASS: %s\n", msg); } \
    else { printf("  FAIL: %s\n", msg); } \
} while(0)

static const char* VERT_SRC = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec3 aColor;
out vec3 vColor;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    vColor = aColor;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(vColor, 1.0);
}
)";

// Full-screen red fragment shader for FSQ test
static const char* RED_FRAG_SRC = R"(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
void main() {
    FragColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";

static void test_triangle_draw(tgfx::IRenderDevice& device, tgfx::PipelineCache& cache) {
    printf("\n--- Triangle Draw via RenderContext2 ---\n");

    const uint32_t W = 64, H = 64;

    // Create render target
    tgfx::TextureDesc rt_desc;
    rt_desc.width = W;
    rt_desc.height = H;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    auto rt = device.create_texture(rt_desc);
    CHECK(bool(rt), "render target created");

    // Create shaders
    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = VERT_SRC;
    auto vs = device.create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = FRAG_SRC;
    auto fs = device.create_shader(fs_desc);

    CHECK(bool(vs) && bool(fs), "shaders created");

    // Create vertex buffer: triangle with RGB colors
    float vertices[] = {
         0.0f,  0.8f,  1.f, 0.f, 0.f,  // top: red
        -0.8f, -0.8f,  0.f, 1.f, 0.f,  // bottom-left: green
         0.8f, -0.8f,  0.f, 0.f, 1.f,  // bottom-right: blue
    };
    uint32_t indices[] = {0, 1, 2};

    tgfx::BufferDesc vbo_desc;
    vbo_desc.size = sizeof(vertices);
    vbo_desc.usage = tgfx::BufferUsage::Vertex;
    auto vbo = device.create_buffer(vbo_desc);
    device.upload_buffer(vbo, {reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)});

    tgfx::BufferDesc ibo_desc;
    ibo_desc.size = sizeof(indices);
    ibo_desc.usage = tgfx::BufferUsage::Index;
    auto ibo = device.create_buffer(ibo_desc);
    device.upload_buffer(ibo, {reinterpret_cast<const uint8_t*>(indices), sizeof(indices)});

    // --- Draw using RenderContext2 ---
    tgfx::RenderContext2 ctx(device, cache);
    ctx.begin_frame();

    float clear[] = {0.f, 0.f, 0.2f, 1.f};  // dark blue background
    ctx.begin_pass(rt, {}, clear);
    ctx.set_viewport(0, 0, W, H);

    ctx.set_depth_test(false);
    ctx.set_blend(false);
    ctx.set_cull(tgfx::CullMode::None);

    ctx.bind_shader(vs, fs);

    tgfx::VertexBufferLayout layout;
    layout.stride = 5 * sizeof(float);
    layout.attributes = {
        {0, tgfx::VertexFormat::Float2, 0},
        {1, tgfx::VertexFormat::Float3, 2 * sizeof(float)},
    };
    ctx.set_vertex_layout(layout);

    ctx.draw(vbo, ibo, 3);

    ctx.end_pass();
    ctx.end_frame();

    // --- Read back pixels via glReadPixels ---
    // Need to bind the texture to an FBO for reading
    auto* gl_dev = static_cast<tgfx::OpenGLRenderDevice*>(&device);
    auto* gl_tex = gl_dev->get_texture(rt);
    CHECK(gl_tex != nullptr, "can access GL texture");

    GLuint read_fbo;
    glGenFramebuffers(1, &read_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, read_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_tex->gl_id, 0);

    std::vector<uint8_t> pixels(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &read_fbo);

    // Check center pixel (should be part of triangle — some color)
    size_t center = (H/2 * W + W/2) * 4;
    printf("  Center pixel: (%u, %u, %u, %u)\n",
           pixels[center], pixels[center+1], pixels[center+2], pixels[center+3]);

    // Check corner pixel (should be clear color: 0,0,51,255)
    printf("  Corner pixel: (%u, %u, %u, %u)\n",
           pixels[0], pixels[1], pixels[2], pixels[3]);

    bool center_has_color = (pixels[center] > 20 || pixels[center+1] > 20 || pixels[center+2] > 60);
    bool corner_is_blue = (pixels[0] < 10 && pixels[1] < 10 && pixels[2] > 40);

    CHECK(center_has_color, "center pixel has triangle color");
    CHECK(corner_is_blue, "corner pixel is clear color (dark blue)");

    // Check pipeline cache was used
    CHECK(cache.size() == 1, "exactly one pipeline cached");

    // Cleanup
    device.destroy(vbo);
    device.destroy(ibo);
    device.destroy(vs);
    device.destroy(fs);
    device.destroy(rt);
}

static void test_fullscreen_quad(tgfx::IRenderDevice& device, tgfx::PipelineCache& cache) {
    printf("\n--- Fullscreen Quad via RenderContext2 ---\n");

    const uint32_t W = 32, H = 32;

    // Create render target
    tgfx::TextureDesc rt_desc;
    rt_desc.width = W;
    rt_desc.height = H;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    auto rt = device.create_texture(rt_desc);

    // Create fragment shader (outputs solid red)
    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = RED_FRAG_SRC;
    auto fs = device.create_shader(fs_desc);

    // --- Draw FSQ ---
    tgfx::RenderContext2 ctx(device, cache);
    ctx.begin_frame();

    float clear[] = {0.f, 0.f, 0.f, 1.f};
    ctx.begin_pass(rt, {}, clear);
    ctx.set_viewport(0, 0, W, H);
    ctx.set_depth_test(false);
    ctx.set_cull(tgfx::CullMode::None);

    // Bind both VS (built-in from FSQ) and our FS explicitly
    // Ensure FSQ resources are created first by calling draw_fullscreen_quad
    // which internally creates fsq_vs_ if needed.
    // But we need fsq_vs_ before binding... let's just bind only FS
    // and let draw_fullscreen_quad supply the VS.
    ctx.bind_shader({}, fs);

    // Check GL errors before draw
    while (glGetError() != GL_NO_ERROR) {}
    ctx.draw_fullscreen_quad();
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        printf("  GL error after FSQ draw: 0x%x\n", err);
        // Check program link status
        GLint prog;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        printf("  Current program: %d\n", prog);
        if (prog > 0) {
            GLint link_ok;
            glGetProgramiv(prog, GL_LINK_STATUS, &link_ok);
            printf("  Link status: %d\n", link_ok);
            if (!link_ok) {
                char log[512];
                glGetProgramInfoLog(prog, 512, nullptr, log);
                printf("  Link error: %s\n", log);
            }
        }
    }

    ctx.end_pass();
    ctx.end_frame();

    // --- Read back ---
    auto* gl_dev = static_cast<tgfx::OpenGLRenderDevice*>(&device);
    auto* gl_tex = gl_dev->get_texture(rt);

    GLuint read_fbo;
    glGenFramebuffers(1, &read_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, read_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl_tex->gl_id, 0);

    std::vector<uint8_t> pixels(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &read_fbo);

    // All pixels should be red
    size_t center = (H/2 * W + W/2) * 4;
    printf("  Center pixel: (%u, %u, %u, %u)\n",
           pixels[center], pixels[center+1], pixels[center+2], pixels[center+3]);

    bool is_red = (pixels[center] > 200 && pixels[center+1] < 20 && pixels[center+2] < 20);
    CHECK(is_red, "FSQ fills entire render target with red");

    // Check another pixel to be sure
    bool corner_red = (pixels[0] > 200 && pixels[1] < 20 && pixels[2] < 20);
    CHECK(corner_red, "corner also red (full coverage)");

    device.destroy(fs);
    device.destroy(rt);
}

static void test_pipeline_cache_reuse(tgfx::IRenderDevice& device, tgfx::PipelineCache& cache) {
    printf("\n--- Pipeline Cache Reuse ---\n");

    size_t before = cache.size();

    // Create same shaders and draw twice — should reuse pipeline
    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = VERT_SRC;
    auto vs = device.create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = FRAG_SRC;
    auto fs = device.create_shader(fs_desc);

    // First pipeline lookup
    tgfx::PipelineCacheKey key;
    key.vertex_shader = vs;
    key.fragment_shader = fs;
    auto p1 = cache.get(key);

    // Same key — should return same handle
    auto p2 = cache.get(key);
    CHECK(p1 == p2, "same key returns same pipeline handle");

    // Different state — should create new pipeline
    key.depth_stencil.depth_test = false;
    auto p3 = cache.get(key);
    CHECK(p3 != p1, "different state creates different pipeline");

    CHECK(cache.size() == before + 2, "exactly 2 new pipelines cached");

    device.destroy(vs);
    device.destroy(fs);
}

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(64, 64, "test", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoaderLoadGL()) {
        fprintf(stderr, "Failed to load GLAD\n");
        glfwTerminate();
        return 1;
    }

    printf("=== RenderContext2 + PipelineCache test ===\n");
    printf("GL: %s\n", glGetString(GL_RENDERER));

    // Each test uses its own device+cache to avoid FBO cache conflicts
    // when GL texture IDs are reused after deletion.
    {
        tgfx::OpenGLRenderDevice device;
        tgfx::PipelineCache cache(device);
        test_triangle_draw(device, cache);
    }
    {
        tgfx::OpenGLRenderDevice device;
        tgfx::PipelineCache cache(device);
        test_fullscreen_quad(device, cache);
    }
    {
        tgfx::OpenGLRenderDevice device;
        tgfx::PipelineCache cache(device);
        test_pipeline_cache_reuse(device, cache);
    }

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);

    glfwDestroyWindow(window);
    glfwTerminate();

    return (pass_count == test_count) ? 0 : 1;
}
