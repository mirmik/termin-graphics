#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include <cstdio>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "termin/platform/sdl_backend_window.hpp"
#include "tgfx2/canvas2d_renderer.hpp"
#include "tgfx2/graphics_host.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/shader_artifact_target.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

    bool approximately(const float pixel[4], float red, float green, float blue) {
        constexpr float tolerance = 0.16f;
        return std::abs(pixel[0] - red) < tolerance && std::abs(pixel[1] - green) < tolerance &&
               std::abs(pixel[2] - blue) < tolerance && pixel[3] > 0.9f;
    }

    bool render_offline_canvas_smoke(tgfx::GraphicsHost& graphics) {
        const std::filesystem::path artifact_root = TGFX2_OPENGL33_SHADER_ROOT;
        const std::filesystem::path canvas_vertex =
            artifact_root / "shaders" / "opengl330" / "termin-engine-canvas2d-solid.vert.glsl";
        if (!std::filesystem::is_regular_file(canvas_vertex)) {
            std::fprintf(stderr, "OpenGL 3.3 built-in shader package is missing: %s\n", canvas_vertex.string().c_str());
            return false;
        }

        // This resolver deliberately has no compiler, cache, or environment
        // fallback. The smoke therefore exercises the exact offline SDK path.
        graphics.configure_shader_artifacts(
            termin::ShaderArtifactResolver(artifact_root.string(), "", "", false, false));

        constexpr std::uint32_t width = 160;
        constexpr std::uint32_t height = 96;
        tgfx::IRenderDevice& device = graphics.device();
        tgfx::TextureDesc target_desc;
        target_desc.width = width;
        target_desc.height = height;
        target_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        target_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
        const tgfx::TextureHandle target = device.create_texture(target_desc);
        if (!target) {
            std::fprintf(stderr, "OpenGL 3.3 Canvas smoke could not create its render target\n");
            return false;
        }
        tgfx::TextureDesc sample_desc;
        sample_desc.width = 2;
        sample_desc.height = 2;
        sample_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        sample_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
        const tgfx::TextureHandle sample = device.create_texture(sample_desc);
        const std::uint8_t sample_pixels[16] = {
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
        };
        device.upload_texture(sample, sample_pixels);

        tgfx::FontAtlas font(TGFX2_OPENGL33_TEST_FONT, 14, 512, 512);
        tgfx::Canvas2DRenderer canvas(&font);
        tgfx::RenderContext2& context = graphics.context();
        const termin::LinearColor black{0.0f, 0.0f, 0.0f, 1.0f};
        context.begin_frame();
        context.begin_pass(target, {}, &black, 1.0f, false);
        canvas.begin(context, static_cast<int>(width), static_cast<int>(height));
        canvas.draw_rect(0, 0, static_cast<float>(width), 48, termin::SrgbColor::red());
        canvas.draw_rect(0, 48, static_cast<float>(width), 48, termin::SrgbColor::green());
        canvas.begin_clip(16, 12, 24, 20);
        canvas.draw_rect(0, 0, 80, 60, termin::SrgbColor::blue());
        canvas.end_clip();
        canvas.draw_texture(sample, 112, 60, 24, 20, termin::SrgbColor::white());
        canvas.draw_text("GL33", 56, 4, 28, termin::SrgbColor::white());
        canvas.end();
        context.end_pass();
        context.end_frame();
        device.wait_idle();

        float top[4]{};
        float bottom[4]{};
        float clipped_inside[4]{};
        float clipped_outside[4]{};
        float textured[4]{};
        const bool probes = device.read_pixel_rgba8(target, 8, 8, top) &&
                            device.read_pixel_rgba8(target, 8, 72, bottom) &&
                            device.read_pixel_rgba8(target, 20, 20, clipped_inside) &&
                            device.read_pixel_rgba8(target, 8, 20, clipped_outside) &&
                            device.read_pixel_rgba8(target, 120, 68, textured);

        std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4u);
        const bool full_read = device.read_texture_rgba_float(target, pixels.data());
        bool text_visible = false;
        float text_probe_max = 0.0f;
        if (full_read) {
            for (std::uint32_t y = 0; y < 48 && !text_visible; ++y) {
                for (std::uint32_t x = 48; x < width; ++x) {
                    const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
                    text_probe_max = std::max(text_probe_max, std::min(pixels[index + 1], pixels[index + 2]));
                    // Anti-aliased glyphs need not contain a fully
                    // covered texel. On the red background, simultaneous
                    // green/blue coverage is an unambiguous text probe.
                    if (pixels[index] > 0.92f && pixels[index + 1] > 0.12f && pixels[index + 2] > 0.12f) {
                        text_visible = true;
                        break;
                    }
                }
            }
        }

        const bool result = probes && approximately(top, 1, 0, 0) && approximately(bottom, 0, 1, 0) &&
                            approximately(clipped_inside, 0, 0, 1) && approximately(clipped_outside, 1, 0, 0) &&
                            approximately(textured, 1, 1, 1) && text_visible;
        if (!result) {
            std::fprintf(stderr,
                         "OpenGL 3.3 offline Canvas smoke failed: probes=%d text=%d "
                         "top=(%.2f %.2f %.2f) bottom=(%.2f %.2f %.2f) "
                         "clip=(%.2f %.2f %.2f)/(%.2f %.2f %.2f)\n",
                         probes,
                         text_visible,
                         top[0], top[1], top[2],
                         bottom[0], bottom[1], bottom[2],
                         clipped_inside[0], clipped_inside[1], clipped_inside[2],
                         clipped_outside[0], clipped_outside[1], clipped_outside[2]);
            std::fprintf(stderr, "OpenGL 3.3 text coverage probe max: %.4f\n", text_probe_max);
        }
        canvas.release_gpu();
        font.release_gpu();
        device.destroy(target);
        device.destroy(sample);
        return result;
    }

} // namespace

int main() {
    tc_shader_init();
    try {
        termin::SDLWindowSystem windows;
        std::unique_ptr<tgfx::GraphicsHost> graphics = windows.create_graphics_host();
        tgfx::IRenderDevice& device = graphics->device();

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major != 3 || minor != 3) {
            std::fprintf(stderr,
                         "OpenGL 3.3 tier smoke requires a true 3.3 context, got %d.%d\n",
                         major,
                         minor);
            return 1;
        }
        if (device.shader_artifact_target() != tgfx::ShaderArtifactTarget::OpenGL330) {
            std::fprintf(stderr,
                         "OpenGL 3.3 tier selected shader target '%s'\n",
                         tgfx::shader_artifact_target_name(device.shader_artifact_target()));
            return 1;
        }
        const tgfx::BackendCapabilities caps = device.capabilities();
        if (caps.supports_compute || caps.supports_storage_textures) {
            std::fprintf(stderr, "OpenGL 3.3 tier advertised compute/storage support\n");
            return 1;
        }
        if (caps.max_shadow_maps != 8 || caps.max_fragment_texture_units < 16) {
            std::fprintf(stderr,
                         "OpenGL 3.3 tier resource budget mismatch: fragment=%u shadows=%u\n",
                         caps.max_fragment_texture_units,
                         caps.max_shadow_maps);
            return 1;
        }

        tgfx::ShaderDesc broken;
        broken.stage = tgfx::ShaderStage::Fragment;
        broken.debug_name = "opengl33-broken-fragment";
        broken.source = "#version 330\nthis is not valid GLSL\n";
        try {
            (void)device.create_shader(broken);
            std::fprintf(stderr, "OpenGL 3.3 tier accepted an invalid shader\n");
            return 1;
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (diagnostic.find(broken.debug_name) == std::string::npos ||
                diagnostic.find("fragment") == std::string::npos ||
                diagnostic.find("opengl330") == std::string::npos) {
                std::fprintf(stderr, "OpenGL shader diagnostic lacks identity: %s\n", error.what());
                return 1;
            }
        }

        tgfx::ShaderDesc compute;
        compute.stage = tgfx::ShaderStage::Compute;
        compute.debug_name = "opengl33-unsupported-compute";
        compute.source = "#version 330\nvoid main() {}\n";
        try {
            (void)device.create_shader(compute);
            std::fprintf(stderr, "OpenGL 3.3 tier accepted a compute shader\n");
            return 1;
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (diagnostic.find(compute.debug_name) == std::string::npos ||
                diagnostic.find("opengl330") == std::string::npos) {
                std::fprintf(stderr, "OpenGL unsupported-stage diagnostic lacks identity: %s\n", error.what());
                return 1;
            }
        }

        tgfx::ShaderDesc link_vertex;
        link_vertex.stage = tgfx::ShaderStage::Vertex;
        link_vertex.debug_name = "opengl33-link-vertex";
        link_vertex.source =
            "#version 330\nout vec3 link_value; void main() { link_value=vec3(1); gl_Position=vec4(0,0,0,1); }\n";
        tgfx::ShaderDesc link_fragment;
        link_fragment.stage = tgfx::ShaderStage::Fragment;
        link_fragment.debug_name = "opengl33-link-fragment";
        link_fragment.source =
            "#version 330\nin vec4 link_value; out vec4 color; void main() { color=link_value; }\n";
        const tgfx::ShaderHandle link_vs = device.create_shader(link_vertex);
        const tgfx::ShaderHandle link_fs = device.create_shader(link_fragment);
        tgfx::PipelineDesc bad_pipeline;
        bad_pipeline.vertex_shader = link_vs;
        bad_pipeline.fragment_shader = link_fs;
        try {
            (void)device.create_pipeline(bad_pipeline);
            std::fprintf(stderr, "OpenGL 3.3 tier linked incompatible shader interfaces\n");
            return 1;
        } catch (const std::exception& error) {
            const std::string diagnostic = error.what();
            if (diagnostic.find(link_vertex.debug_name) == std::string::npos ||
                diagnostic.find(link_fragment.debug_name) == std::string::npos ||
                diagnostic.find("opengl330") == std::string::npos) {
                std::fprintf(stderr, "OpenGL link diagnostic lacks shader identities: %s\n", error.what());
                return 1;
            }
        }
        device.destroy(link_fs);
        device.destroy(link_vs);

        if (!render_offline_canvas_smoke(*graphics)) {
            windows.close(*graphics);
            tc_shader_shutdown();
            return 1;
        }

        const tgfx::AdapterInfo adapter = device.adapter_info();
        std::printf("OPENGL33_CONTEXT=%d.%d TARGET=%s RENDERER=%s DRIVER=%s SOFTWARE=%s\n",
                    major,
                    minor,
                    tgfx::shader_artifact_target_name(device.shader_artifact_target()),
                    adapter.adapter_name.c_str(),
                    adapter.driver_name.c_str(),
                    adapter.is_software() ? "yes" : "no");

        windows.close(*graphics);
        tc_shader_shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "OpenGL 3.3 tier smoke skipped: %s\n", error.what());
        tc_shader_shutdown();
        return 77;
    }
}
