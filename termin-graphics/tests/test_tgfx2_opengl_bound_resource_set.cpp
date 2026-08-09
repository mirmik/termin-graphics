// OpenGL runtime smoke for the backend-facing BoundResourceSetDesc path.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <thread>
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

#include "tgfx2_ordered_mrt_smoke.hpp"

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

static const char* kSrgbSamplingFragmentSource = R"(
#version 420 core
layout(binding = 0) uniform sampler2D source_texture;
out vec4 FragColor;
void main() {
    FragColor = textureLod(source_texture, vec2(0.5, 0.5), 1.0);
}
)";

static const char* kMrtFragmentSource = R"(
#version 420 core
layout(location = 0) out vec4 Target0;
layout(location = 1) out vec4 Target1;
layout(location = 2) out vec4 Target2;
void main() {
    Target0 = vec4(0.0, 1.0, 1.0, 1.0);
    Target1 = vec4(1.0, 0.0, 1.0, 1.0);
    Target2 = vec4(1.0, 1.0, 0.0, 1.0);
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

    out.window = SDL_CreateWindow("tgfx2 OpenGL bound resource set",
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

static bool render_ordered_mrt_smoke(tgfx::IRenderDevice& device,
                                     tgfx::ShaderHandle vertex,
                                     const tgfx::VertexBufferLayout& vertex_layout,
                                     tgfx::BufferHandle vertex_buffer) {
    tgfx::ShaderDesc fragment_desc;
    fragment_desc.stage = tgfx::ShaderStage::Fragment;
    fragment_desc.source = kMrtFragmentSource;
    fragment_desc.debug_name = "opengl-ordered-mrt:fragment";
    const tgfx::ShaderHandle fragment = device.create_shader(fragment_desc);
    if (!fragment) {
        std::fprintf(stderr, "OpenGL MRT smoke: fragment shader creation failed\n");
        return false;
    }

    const bool passed = tgfx::tests::run_ordered_mrt_smoke(device, "OpenGL", [&](tgfx::RenderContext2& context) {
        context.bind_shader(vertex, fragment);
        context.set_vertex_layout(vertex_layout);
        context.draw_arrays(vertex_buffer, 3);
    });
    device.destroy(fragment);
    return passed;
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

    const auto caps = device->capabilities();
    if (caps.supports_dynamic_uniform_offsets) {
        std::fprintf(stderr, "OpenGL 3.3 must not advertise dynamic uniform offsets\n");
        return 1;
    }
    if (caps.supports_storage_textures) {
        std::fprintf(stderr, "OpenGL 3.3 must not advertise storage textures\n");
        return 1;
    }

    tgfx::TextureDesc unsupported_storage_desc;
    unsupported_storage_desc.width = 2;
    unsupported_storage_desc.height = 2;
    unsupported_storage_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    unsupported_storage_desc.usage = tgfx::TextureUsage::Storage | tgfx::TextureUsage::Sampled;
    if (device->create_texture(unsupported_storage_desc)) {
        std::fprintf(stderr, "OpenGL storage texture creation should fail explicitly\n");
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
    pipeline_desc.vertex_layouts.push_back(tgfx::make_vertex_layout_desc(vertex_layout));

    tgfx::PipelineHandle pipeline = device->create_pipeline(pipeline_desc);
    const uintptr_t resource_layout_token = device->pipeline_resource_layout_token(pipeline);
    if (resource_layout_token == 0) {
        std::fprintf(stderr, "OpenGL pipeline resource layout token is null\n");
        return 1;
    }

    const float vertices[] = {
        -1.0f,
        -1.0f,
        3.0f,
        -1.0f,
        -1.0f,
        3.0f,
    };
    tgfx::BufferDesc vb_desc;
    vb_desc.size = sizeof(vertices);
    vb_desc.usage = tgfx::BufferUsage::Vertex | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle vb = device->create_buffer(vb_desc);
    device->upload_buffer(vb, std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(vertices), sizeof(vertices)));

    const bool ordered_mrt_ok = render_ordered_mrt_smoke(*device, vs, vertex_layout, vb);

    const float color_block[] = {0.20f, 0.70f, 0.10f, 1.0f};
    tgfx::BufferDesc ubo_desc;
    ubo_desc.size = sizeof(color_block);
    ubo_desc.usage = tgfx::BufferUsage::Uniform | tgfx::BufferUsage::CopyDst;
    tgfx::BufferHandle ubo = device->create_buffer(ubo_desc);
    device->upload_buffer(ubo,
                          std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(color_block), sizeof(color_block)));

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

    const tgfx::BoundResourceBinding material_binding = {
        tgfx::bound_resource_slot_from_plan_entry(plan_entry),
        value,
    };
    tgfx::BoundResourceSetStorage bound_storage;
    bound_storage.set_resource_layout_token(resource_layout_token);
    bound_storage.append_group(tgfx::ShaderResourceScope::Material, true, &material_binding, 1);
    const tgfx::BoundResourceSetDesc bound_desc = bound_storage.view();
    tgfx::ResourceSetHandle resource_set = device->create_bound_resource_set(bound_desc);

    tgfx::TextureDesc rt_desc;
    rt_desc.width = kWidth;
    rt_desc.height = kHeight;
    rt_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    rt_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    tgfx::TextureHandle rt = device->create_texture(rt_desc);

    std::unique_ptr<tgfx::ICommandList> cmd = device->create_command_list();
    cmd->begin();

    tgfx::RenderPassDesc pass;
    tgfx::ColorAttachmentDesc color_attachment;
    color_attachment.texture = rt;
    color_attachment.load = tgfx::LoadOp::Clear;
    color_attachment.clear_color.r = 0.0f;
    color_attachment.clear_color.g = 0.0f;
    color_attachment.clear_color.b = 0.0f;
    color_attachment.clear_color.a = 1.0f;
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

    tgfx::TextureDesc depth_desc;
    depth_desc.width = kWidth;
    depth_desc.height = kHeight;
    depth_desc.format = tgfx::PixelFormat::D32F;
    depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment | tgfx::TextureUsage::CopySrc;
    const tgfx::TextureHandle depth = device->create_texture(depth_desc);
    std::unique_ptr<tgfx::ICommandList> depth_cmd = device->create_command_list();
    depth_cmd->begin();
    tgfx::RenderPassDesc depth_pass;
    depth_pass.has_depth = true;
    depth_pass.depth.texture = depth;
    depth_pass.depth.load = tgfx::LoadOp::Clear;
    depth_pass.depth.clear_depth = 0.37f;
    depth_cmd->begin_render_pass(depth_pass);
    depth_cmd->end_render_pass();
    depth_cmd->end();
    device->submit(*depth_cmd);

    std::array<uint64_t, 4> color_requests{};
    for (size_t i = 0; i < color_requests.size(); ++i) {
        color_requests[i] = device->request_pixel_rgba8(rt, kWidth / 2 + static_cast<int>(i), kHeight / 2);
    }
    const uint64_t depth_request = device->request_pixel_depth_float(depth, kWidth / 2, kHeight / 2);
    device->flush();

    std::array<bool, 4> color_ready{};
    std::array<std::array<float, 4>, 4> async_colors{};
    bool depth_ready = false;
    float async_depth = 0.0f;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        for (size_t i = 0; i < color_requests.size(); ++i) {
            if (!color_ready[i] && color_requests[i] != 0) {
                color_ready[i] = device->poll_pixel_rgba8(color_requests[i], async_colors[i].data());
            }
        }
        if (!depth_ready && depth_request != 0) {
            depth_ready = device->poll_pixel_depth_float(depth_request, &async_depth);
        }
        if (std::all_of(color_ready.begin(), color_ready.end(), [](bool ready) { return ready; }) && depth_ready) {
            break;
        }
        std::this_thread::yield();
    }

    float pixel[4] = {};
    const bool read_ok = device->read_pixel_rgba8(rt, kWidth / 2, kHeight / 2, pixel);
    std::printf("OpenGL bound resource set center pixel: %s (%.2f %.2f %.2f %.2f)\n",
                read_ok ? "ok" : "failed",
                pixel[0],
                pixel[1],
                pixel[2],
                pixel[3]);

    const bool pass_ok = read_ok && pixel[0] > 0.12f && pixel[0] < 0.35f && pixel[1] > 0.55f && pixel[1] < 0.85f &&
                         pixel[2] > 0.04f && pixel[2] < 0.25f && pixel[3] > 0.90f &&
                         std::all_of(color_ready.begin(), color_ready.end(), [](bool ready) { return ready; }) &&
                         std::all_of(async_colors.begin(),
                                     async_colors.end(),
                                     [](const std::array<float, 4>& color) {
                                         return color[0] > 0.12f && color[0] < 0.35f && color[1] > 0.55f &&
                                                color[1] < 0.85f && color[2] > 0.04f && color[2] < 0.25f &&
                                                color[3] > 0.90f;
                                     }) &&
                         depth_ready && async_depth > 0.36f && async_depth < 0.38f;

    tgfx::ShaderDesc srgb_fs_desc;
    srgb_fs_desc.stage = tgfx::ShaderStage::Fragment;
    srgb_fs_desc.source = kSrgbSamplingFragmentSource;
    srgb_fs_desc.debug_name = "opengl-srgb-sampling:fragment";
    const tgfx::ShaderHandle srgb_fs = device->create_shader(srgb_fs_desc);
    tgfx::PipelineDesc srgb_pipeline_desc = pipeline_desc;
    srgb_pipeline_desc.fragment_shader = srgb_fs;
    const tgfx::PipelineHandle srgb_pipeline = device->create_pipeline(srgb_pipeline_desc);

    tgfx::TextureDesc source_desc;
    source_desc.width = 2;
    source_desc.height = 2;
    source_desc.mip_levels = 2;
    source_desc.format = tgfx::PixelFormat::RGBA8_sRGB;
    source_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle srgb_source = device->create_texture(source_desc);
    source_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    const tgfx::TextureHandle linear_source = device->create_texture(source_desc);
    const uint8_t encoded_base[] = {
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
        128,
    };
    const uint8_t encoded_mip[] = {128, 128, 128, 128};
    device->upload_texture(srgb_source, std::span<const uint8_t>(encoded_base, sizeof(encoded_base)), 0);
    device->upload_texture(srgb_source, std::span<const uint8_t>(encoded_mip, sizeof(encoded_mip)), 1);
    device->upload_texture(linear_source, std::span<const uint8_t>(encoded_base, sizeof(encoded_base)), 0);
    device->upload_texture(linear_source, std::span<const uint8_t>(encoded_mip, sizeof(encoded_mip)), 1);
    const tgfx::SamplerHandle sampler = device->create_sampler(tgfx::SamplerDesc{});

    tgfx::BackendBindingPlanEntry texture_plan;
    texture_plan.resource.name = "source_texture";
    texture_plan.resource.kind = tgfx::ShaderResourceKind::Texture;
    texture_plan.resource.scope = tgfx::ShaderResourceScope::Material;
    texture_plan.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    texture_plan.placement.kind = tgfx::BackendPlacementKind::OpenGLBinding;
    texture_plan.placement.opengl.binding_class = tgfx::OpenGLBindingClass::TextureUnit;
    texture_plan.placement.opengl.texture_unit = 0;

    const auto create_texture_set = [&](tgfx::TextureHandle texture) -> tgfx::ResourceSetHandle {
        tgfx::BoundResourceValue texture_value;
        texture_value.kind = tgfx::BoundResourceKind::SampledTexture;
        texture_value.texture = texture;
        texture_value.sampler = sampler;
        const tgfx::BoundResourceBinding texture_binding = {
            tgfx::bound_resource_slot_from_plan_entry(texture_plan),
            texture_value,
        };
        tgfx::BoundResourceSetStorage texture_storage;
        texture_storage.set_resource_layout_token(device->pipeline_resource_layout_token(srgb_pipeline));
        texture_storage.append_group(tgfx::ShaderResourceScope::Material, true, &texture_binding, 1);
        return device->create_bound_resource_set(texture_storage.view());
    };
    const tgfx::ResourceSetHandle srgb_texture_set = create_texture_set(srgb_source);
    const tgfx::ResourceSetHandle linear_texture_set = create_texture_set(linear_source);
    const auto sample = [&](tgfx::ResourceSetHandle texture_set, float out_pixel[4]) {
        auto cmd = device->create_command_list();
        cmd->begin();
        cmd->begin_render_pass(pass);
        cmd->set_viewport(0, 0, kWidth, kHeight);
        cmd->bind_pipeline(srgb_pipeline);
        cmd->bind_resource_set(texture_set);
        cmd->bind_vertex_buffer(0, vb);
        cmd->draw(3);
        cmd->end_render_pass();
        cmd->end();
        device->submit(*cmd);
        return device->read_pixel_rgba8(rt, kWidth / 2, kHeight / 2, out_pixel);
    };

    float srgb_pixel[4] = {};
    float linear_pixel[4] = {};
    const bool srgb_read_ok = sample(srgb_texture_set, srgb_pixel);
    const bool linear_read_ok = sample(linear_texture_set, linear_pixel);
    const bool srgb_sampling_ok =
        srgb_read_ok && std::abs(srgb_pixel[0] - 0.21586f) < 0.015f && std::abs(srgb_pixel[1] - 0.21586f) < 0.015f &&
        std::abs(srgb_pixel[2] - 0.21586f) < 0.015f && std::abs(srgb_pixel[3] - 0.50196f) < 0.015f;
    const bool linear_sampling_ok = linear_read_ok && std::abs(linear_pixel[0] - 0.50196f) < 0.015f &&
                                    std::abs(linear_pixel[1] - 0.50196f) < 0.015f &&
                                    std::abs(linear_pixel[2] - 0.50196f) < 0.015f &&
                                    std::abs(linear_pixel[3] - 0.50196f) < 0.015f;
    std::printf("OpenGL mipmapped texture encoding sampling: %s "
                "(sRGB %.3f %.3f %.3f %.3f; Linear %.3f %.3f %.3f %.3f)\n",
                srgb_sampling_ok && linear_sampling_ok ? "ok" : "failed",
                srgb_pixel[0],
                srgb_pixel[1],
                srgb_pixel[2],
                srgb_pixel[3],
                linear_pixel[0],
                linear_pixel[1],
                linear_pixel[2],
                linear_pixel[3]);

    device->destroy(linear_texture_set);
    device->destroy(srgb_texture_set);
    device->destroy(sampler);
    device->destroy(linear_source);
    device->destroy(srgb_source);
    device->destroy(srgb_pipeline);
    device->destroy(srgb_fs);
    device->destroy(resource_set);
    device->destroy(ubo);
    device->destroy(vb);
    device->destroy(rt);
    device->destroy(depth);
    device->destroy(pipeline);
    device->destroy(vs);
    device->destroy(fs);
    device.reset();

    if (!pass_ok || !srgb_sampling_ok || !linear_sampling_ok || !ordered_mrt_ok) {
        std::fprintf(stderr, "OpenGL bound resource set smoke failed\n");
        return 1;
    }

    std::printf("OpenGL bound resource set smoke passed\n");
    return 0;
}
