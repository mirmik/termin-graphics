// Pixel regression for the navmesh debug-overlay depth contract.
#include <cstdio>
#include <exception>
#include <memory>
#include <span>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>
#include <glad/glad.h>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_command_list.hpp"

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;

constexpr const char* kVertexSource = R"(
#version 420 core
layout(location = 0) in vec3 aPos;
void main() { gl_Position = vec4(aPos, 1.0); }
)";

constexpr const char* kBlackFragmentSource = R"(
#version 420 core
out vec4 FragColor;
void main() { FragColor = vec4(0.0, 0.0, 0.0, 1.0); }
)";

constexpr const char* kBlueFragmentSource = R"(
#version 420 core
out vec4 FragColor;
void main() { FragColor = vec4(0.0, 0.0, 1.0, 1.0); }
)";

constexpr const char* kNavmeshSurfaceFragmentSource = R"(
#version 420 core
out vec4 FragColor;
void main() { FragColor = vec4(0.0, 1.0, 0.0, 0.5); }
)";

constexpr const char* kNavmeshContourFragmentSource = R"(
#version 420 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 1.0, 0.0, 1.0); }
)";

constexpr const char* kTransparentForegroundFragmentSource = R"(
#version 420 core
out vec4 FragColor;
void main() { FragColor = vec4(1.0, 0.0, 0.0, 1.0); }
)";

struct SDLGLContext {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;

    ~SDLGLContext() {
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};

bool create_context(SDLGLContext& out) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Navmesh overlay smoke: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    out.window = SDL_CreateWindow(
        "tgfx2 navmesh overlay smoke",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        kWidth,
        kHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!out.window) {
        std::fprintf(stderr, "Navmesh overlay smoke: window creation failed: %s\n", SDL_GetError());
        return false;
    }
    out.context = SDL_GL_CreateContext(out.window);
    if (!out.context || SDL_GL_MakeCurrent(out.window, out.context) != 0) {
        std::fprintf(stderr, "Navmesh overlay smoke: OpenGL context creation failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

tgfx::BufferHandle make_vertex_buffer(
    tgfx::IRenderDevice& device,
    const float* data,
    size_t size) {
    tgfx::BufferDesc desc;
    desc.size = size;
    desc.usage = tgfx::BufferUsage::Vertex;
    const tgfx::BufferHandle buffer = device.create_buffer(desc);
    device.upload_buffer(buffer, {
        reinterpret_cast<const uint8_t*>(data),
        size,
    });
    return buffer;
}

tgfx::ShaderHandle make_shader(
    tgfx::IRenderDevice& device,
    tgfx::ShaderStage stage,
    const char* source,
    const char* name) {
    tgfx::ShaderDesc desc;
    desc.stage = stage;
    desc.source = source;
    desc.debug_name = name;
    return device.create_shader(desc);
}

bool is_blue(const float* pixel) {
    return pixel[2] > 0.75f && pixel[0] < 0.20f && pixel[1] < 0.20f;
}

bool is_red(const float* pixel) {
    return pixel[0] > 0.75f && pixel[1] < 0.20f && pixel[2] < 0.20f;
}

} // namespace

int main() {
    SDLGLContext gl;
    if (!create_context(gl)) return 1;

    std::unique_ptr<tgfx::IRenderDevice> device;
    try {
        device = tgfx::create_device(tgfx::BackendType::OpenGL);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Navmesh overlay smoke: OpenGL device creation failed: %s\n", error.what());
        return 1;
    }

    const float far_scene[] = {
        -1.0f, -1.0f, 0.8f, 3.0f, -1.0f, 0.8f, -1.0f, 3.0f, 0.8f,
    };
    const float left_occluder[] = {
        -1.0f, -1.0f, 0.1f, 0.0f, -1.0f, 0.1f, -1.0f, 1.0f, 0.1f,
         0.0f, -1.0f, 0.1f, 0.0f, 1.0f, 0.1f, -1.0f, 1.0f, 0.1f,
    };
    const float navmesh_overlay[] = {
        -1.0f, -1.0f, 0.3f, 3.0f, -1.0f, 0.3f, -1.0f, 3.0f, 0.3f,
    };
    const float transparent_foreground[] = {
        -1.0f, -1.0f, 0.4f, 3.0f, -1.0f, 0.4f, -1.0f, 3.0f, 0.4f,
    };

    const auto far_vbo = make_vertex_buffer(*device, far_scene, sizeof(far_scene));
    const auto occluder_vbo = make_vertex_buffer(*device, left_occluder, sizeof(left_occluder));
    const auto navmesh_vbo = make_vertex_buffer(*device, navmesh_overlay, sizeof(navmesh_overlay));
    const auto transparent_vbo = make_vertex_buffer(
        *device, transparent_foreground, sizeof(transparent_foreground));

    const auto vs = make_shader(*device, tgfx::ShaderStage::Vertex, kVertexSource, "navmesh-overlay-vs");
    const auto black_fs = make_shader(*device, tgfx::ShaderStage::Fragment, kBlackFragmentSource, "navmesh-overlay-black");
    const auto blue_fs = make_shader(*device, tgfx::ShaderStage::Fragment, kBlueFragmentSource, "navmesh-overlay-blue");
    const auto surface_fs = make_shader(*device, tgfx::ShaderStage::Fragment, kNavmeshSurfaceFragmentSource, "navmesh-overlay-surface");
    const auto contour_fs = make_shader(*device, tgfx::ShaderStage::Fragment, kNavmeshContourFragmentSource, "navmesh-overlay-contour");
    const auto transparent_fs = make_shader(*device, tgfx::ShaderStage::Fragment, kTransparentForegroundFragmentSource, "navmesh-overlay-transparent");

    tgfx::TextureDesc color_desc;
    color_desc.width = kWidth;
    color_desc.height = kHeight;
    color_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    color_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    const auto color = device->create_texture(color_desc);
    tgfx::TextureDesc depth_desc;
    depth_desc.width = kWidth;
    depth_desc.height = kHeight;
    depth_desc.format = tgfx::PixelFormat::D32F;
    depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment | tgfx::TextureUsage::CopySrc;
    const auto depth = device->create_texture(depth_desc);

    tgfx::PipelineHandle black_pipeline;
    tgfx::PipelineHandle blue_pipeline;
    tgfx::PipelineHandle surface_pipeline;
    tgfx::PipelineHandle contour_pipeline;
    tgfx::PipelineHandle transparent_pipeline;
    bool pass = vs && black_fs && blue_fs && surface_fs && contour_fs && transparent_fs && color && depth;
    if (pass) {
        tgfx::VertexBufferLayout layout;
        layout.stride = 3 * sizeof(float);
        layout.attributes = {{0, tgfx::VertexFormat::Float3, 0}};

        const auto make_pipeline = [&](tgfx::ShaderHandle fragment_shader,
                                       bool depth_write,
                                       bool blend) {
            tgfx::PipelineDesc desc;
            desc.vertex_shader = vs;
            desc.fragment_shader = fragment_shader;
            desc.topology = tgfx::PrimitiveTopology::TriangleList;
            desc.vertex_layouts = {tgfx::make_vertex_layout_desc(layout)};
            desc.raster.cull = tgfx::CullMode::None;
            desc.depth_stencil.depth_test = true;
            desc.depth_stencil.depth_write = depth_write;
            desc.blend.enabled = blend;
            desc.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};
            desc.depth_format = tgfx::PixelFormat::D32F;
            return device->create_pipeline(desc);
        };
        black_pipeline = make_pipeline(black_fs, true, false);
        blue_pipeline = make_pipeline(blue_fs, true, false);
        surface_pipeline = make_pipeline(surface_fs, false, true);
        contour_pipeline = make_pipeline(contour_fs, false, false);
        transparent_pipeline = make_pipeline(transparent_fs, false, true);
        std::printf(
            "Navmesh overlay pipelines: %u %u %u %u %u\n",
            black_pipeline.id,
            blue_pipeline.id,
            surface_pipeline.id,
            contour_pipeline.id,
            transparent_pipeline.id);
        pass = black_pipeline && blue_pipeline && surface_pipeline && contour_pipeline &&
            transparent_pipeline;

        if (pass) {
            auto cmd = device->create_command_list();
            tgfx::RenderPassDesc render_pass;
            tgfx::ColorAttachmentDesc color_attachment;
            color_attachment.texture = color;
            color_attachment.load = tgfx::LoadOp::Clear;
            color_attachment.clear_color[3] = 1.0f;
            render_pass.colors.push_back(color_attachment);
            render_pass.has_depth = true;
            render_pass.depth.texture = depth;
            render_pass.depth.load = tgfx::LoadOp::Clear;
            render_pass.depth.clear_depth = 1.0f;

            cmd->begin();
            cmd->begin_render_pass(render_pass);
            cmd->set_viewport(0, 0, kWidth, kHeight);
            cmd->bind_pipeline(black_pipeline);
            cmd->bind_vertex_buffer(0, far_vbo);
            cmd->draw(3);
            cmd->bind_pipeline(blue_pipeline);
            cmd->bind_vertex_buffer(0, occluder_vbo);
            cmd->draw(6);

            // The surface and contours run before the transparent scene
            // phase. They preserve opaque-scene depth testing, but cannot
            // turn the debug overlay into a later-transparent occluder.
            cmd->bind_pipeline(surface_pipeline);
            cmd->bind_vertex_buffer(0, navmesh_vbo);
            cmd->draw(3);
            cmd->bind_pipeline(contour_pipeline);
            cmd->draw(3);
            cmd->bind_pipeline(transparent_pipeline);
            cmd->bind_vertex_buffer(0, transparent_vbo);
            cmd->draw(3);
            cmd->end_render_pass();
            cmd->end();
            device->submit(*cmd);
            device->wait_idle();
            const GLenum gl_error = glGetError();
            if (gl_error != GL_NO_ERROR) {
                std::fprintf(stderr, "Navmesh overlay smoke: OpenGL error 0x%X\n", gl_error);
                pass = false;
            }
        }

        float left[4] = {};
        float right[4] = {};
        pass = device->read_pixel_rgba8(color, kWidth / 4, kHeight / 2, left) &&
            device->read_pixel_rgba8(color, 3 * kWidth / 4, kHeight / 2, right) &&
            is_blue(left) && is_red(right);
        std::printf(
            "Navmesh overlay pixels: left=(%.2f %.2f %.2f) right=(%.2f %.2f %.2f)\n",
            left[0], left[1], left[2], right[0], right[1], right[2]);
    }

    device->destroy(far_vbo);
    device->destroy(occluder_vbo);
    device->destroy(navmesh_vbo);
    device->destroy(transparent_vbo);
    device->destroy(black_pipeline);
    device->destroy(blue_pipeline);
    device->destroy(surface_pipeline);
    device->destroy(contour_pipeline);
    device->destroy(transparent_pipeline);
    device->destroy(vs);
    device->destroy(black_fs);
    device->destroy(blue_fs);
    device->destroy(surface_fs);
    device->destroy(contour_fs);
    device->destroy(transparent_fs);
    device->destroy(color);
    device->destroy(depth);

    if (!pass) {
        std::fprintf(stderr, "Navmesh overlay depth-ordering smoke failed\n");
        return 1;
    }
    std::printf("Navmesh overlay depth-ordering smoke passed\n");
    return 0;
}
