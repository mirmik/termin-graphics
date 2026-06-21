// OpenGL runtime smoke for the backend-facing BoundResourceSetDesc path.
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <utility>

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#include <SDL.h>

#include "tgfx2/backend_binding_plan.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/i_command_list.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/vertex_layout.hpp"

extern "C" {
#include "tgfx/resources/tc_shader.h"
}

static constexpr int kWidth = 32;
static constexpr int kHeight = 32;
static constexpr uint32_t kColorBlockBinding = 2;

static const char* kVertexSource = R"(
#version 420 core
layout(location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* kFragmentSource = R"(
#version 420 core
layout(std140, binding = 2) uniform ColorBlock {
    vec4 color;
};
out vec4 FragColor;
void main() {
    FragColor = color;
}
)";

struct SDLGLContext {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;

    ~SDLGLContext() {
        if (context) {
            SDL_GL_DeleteContext(context);
        }
        if (window) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
    }
};

static bool create_context(SDLGLContext& out) {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "Window creation failed: SDL_Init: %s\n", SDL_GetError());
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    out.window = SDL_CreateWindow(
        "tgfx2 OpenGL bound resource set",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        kWidth,
        kHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!out.window) {
        std::fprintf(stderr, "Window creation failed: SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    out.context = SDL_GL_CreateContext(out.window);
    if (!out.context) {
        std::fprintf(stderr, "Window creation failed: SDL_GL_CreateContext: %s\n", SDL_GetError());
        return false;
    }

    if (SDL_GL_MakeCurrent(out.window, out.context) != 0) {
        std::fprintf(stderr, "Window creation failed: SDL_GL_MakeCurrent: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

int main() {
    SDLGLContext gl;
    if (!create_context(gl)) {
        return 1;
    }

    std::unique_ptr<tgfx::IRenderDevice> device;
    try {
        device = tgfx::create_device(tgfx::BackendType::OpenGL);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Window creation failed: OpenGL device creation: %s\n", e.what());
        return 1;
    }

    tgfx::ShaderDesc vs_desc;
    vs_desc.stage = tgfx::ShaderStage::Vertex;
    vs_desc.source = kVertexSource;
    vs_desc.debug_name = "opengl-bound-resource-set:vertex";
    tgfx::ShaderHandle vs = device->create_shader(vs_desc);

    tgfx::ShaderDesc fs_desc;
    fs_desc.stage = tgfx::ShaderStage::Fragment;
    fs_desc.source = kFragmentSource;
    fs_desc.debug_name = "opengl-bound-resource-set:fragment";
    tgfx::ShaderHandle fs = device->create_shader(fs_desc);

    tgfx::PipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vs;
    pipeline_desc.fragment_shader = fs;
    pipeline_desc.topology = tgfx::PrimitiveTopology::TriangleList;
    pipeline_desc.depth_stencil.depth_test = false;
    pipeline_desc.depth_stencil.depth_write = false;
    pipeline_desc.depth_format = tgfx::PixelFormat::Undefined;
    pipeline_desc.raster.cull = tgfx::CullMode::None;
    pipeline_desc.color_formats = {tgfx::PixelFormat::RGBA8_UNorm};

    tgfx::VertexBufferLayout vertex_layout;
    vertex_layout.stride = 2 * sizeof(float);
    vertex_layout.attributes = {{0, tgfx::VertexFormat::Float2, 0}};
    pipeline_desc.vertex_layouts.push_back(vertex_layout);

    tgfx::PipelineHandle pipeline = device->create_pipeline(pipeline_desc);
    const uintptr_t resource_layout_token =
        device->pipeline_resource_layout_token(pipeline);
    if (resource_layout_token == 0) {
        std::fprintf(stderr, "OpenGL pipeline resource layout token is null\n");
        return 1;
    }

    const float vertices[] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle vb = device->create_buffer(vb_desc);
    device->upload_buffer(
        vb,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)));

    const float color_block[] = {0.20f, 0.70f, 0.10f, 1.0f};
    tgfx::BufferDesc ubo_desc;
    ubo_desc.size = sizeof(color_block);
    ubo_desc.usage = tgfx::BufferUsage::Uniform | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle ubo = device->create_buffer(ubo_desc);
    device->upload_buffer(
        ubo,
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(color_block), sizeof(color_block)));

    tgfx::BackendBindingPlanEntry plan_entry;
    plan_entry.resource.name = "ColorBlock";
    plan_entry.resource.kind = tgfx::ShaderResourceKind::ConstantBuffer;
    plan_entry.resource.scope = tgfx::ShaderResourceScope::Material;
    plan_entry.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    plan_entry.size = sizeof(color_block);
    plan_entry.placement.kind = tgfx::BackendPlacementKind::OpenGLBinding;
    plan_entry.placement.opengl.binding_class = tgfx::OpenGLBindingClass::UniformBuffer;
    plan_entry.placement.opengl.binding_point = kColorBlockBinding;

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
        device->create_bound_resource_set(bound_desc);

    tgfx::TextureDesc rt_desc;
    rt_desc.width = kWidth;
    rt_desc.height = kHeight;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage =
        tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    tgfx::TextureHandle rt = device->create_texture(rt_desc);

    std::unique_ptr<tgfx::ICommandList> cmd = device->create_command_list();
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
    device->submit(*cmd);
    device->wait_idle();

    float pixel[4] = {};
    const bool read_ok = device->read_pixel_rgba8(rt, kWidth / 2, kHeight / 2, pixel);
    std::printf(
        "OpenGL bound resource set center pixel: %s (%.2f %.2f %.2f %.2f)\n",
        read_ok ? "ok" : "failed",
        pixel[0], pixel[1], pixel[2], pixel[3]);

    const bool pass_ok =
        read_ok &&
        pixel[0] > 0.12f && pixel[0] < 0.35f &&
        pixel[1] > 0.55f && pixel[1] < 0.85f &&
        pixel[2] > 0.04f && pixel[2] < 0.25f &&
        pixel[3] > 0.90f;

    device->destroy(resource_set);
    device->destroy(ubo);
    device->destroy(vb);
    device->destroy(rt);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device.reset();

    if (!pass_ok) {
        std::fprintf(stderr, "OpenGL bound resource set smoke failed\n");
        return 1;
    }

    std::printf("OpenGL bound resource set smoke passed\n");
    return 0;
}
