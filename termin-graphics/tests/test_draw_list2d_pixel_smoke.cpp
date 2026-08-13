#ifdef NDEBUG
#undef NDEBUG
#endif

#include "tgfx2/canvas2d_renderer.hpp"
#include "tgfx2/descriptors.hpp"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

    constexpr std::uint32_t width = 64;
    constexpr std::uint32_t height = 64;

    class NoFonts final : public tgfx::DrawResourceResolver2D {
    public:
        tgfx::FontAtlas* resolve_font(tgfx::FontHandle) override {
            return nullptr;
        }
    };

    class RetainedProbe final : public tgfx::RetainedDrawBatch2D {
    public:
        bool called = false;
        tgfx::RetainedDrawState2D state{};

        bool draw(tgfx::RenderContext2&, const tgfx::RetainedDrawState2D& value) override {
            called = true;
            state = value;
            return true;
        }
    };

    struct TempDirectory {
        std::filesystem::path path;

        ~TempDirectory() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };

    void configure_shader_tools(const char* executable) {
        std::error_code error;
        const auto executable_path = std::filesystem::absolute(executable, error);
        if (!error) {
            const auto compiler = executable_path.parent_path() / "termin_shaderc";
            if (std::filesystem::is_regular_file(compiler, error)) {
                termin::tgfx2_set_shader_compiler_path(compiler.string().c_str());
            }
        }
    }

    bool red(const float pixel[4]) {
        return pixel[0] > 0.8f && pixel[1] < 0.1f && pixel[2] < 0.1f && pixel[3] > 0.9f;
    }

    bool clear(const float pixel[4]) {
        return pixel[0] < 0.05f && pixel[1] < 0.05f && pixel[2] < 0.05f && pixel[3] > 0.9f;
    }

    int run(const char* executable) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        TempDirectory artifacts{std::filesystem::temp_directory_path() /
                                ("termin-draw-list2d-" + std::to_string(unique))};
        std::filesystem::create_directories(artifacts.path);
#ifdef _WIN32
        _putenv_s("TERMIN_BUILTIN_SHADER_ROOT", TGFX2_BUILTIN_SHADER_ROOT);
#else
        setenv("TERMIN_BUILTIN_SHADER_ROOT", TGFX2_BUILTIN_SHADER_ROOT, 1);
#endif
        configure_shader_tools(executable);
        termin::tgfx2_set_shader_artifact_root(artifacts.path.string().c_str());
        termin::tgfx2_set_shader_cache_root((artifacts.path / "cache").string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(true);

        std::unique_ptr<tgfx::IRenderDevice> device;
        try {
            device = tgfx::create_device(tgfx::BackendType::Vulkan);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Vulkan device unavailable: %s\n", error.what());
            return 1;
        }

        tgfx::TextureDesc target_desc;
        target_desc.width = width;
        target_desc.height = height;
        target_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        target_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
        const auto target = device->create_texture(target_desc);
        if (!target)
            return 2;

        tgfx::DrawList2DBuilder builder;
        constexpr float pi = 3.14159265358979323846f;
        assert(builder.push_transform(termin::Affine2f::translation(32, 32) * termin::Affine2f::rotation(pi * 0.25f)));
        assert(builder.push_clip_rect({-16, -16, 32, 32}));
        tgfx::Path2f ring;
        assert(ring.move_to({-20, -20}));
        assert(ring.line_to({20, -20}));
        assert(ring.line_to({20, 20}));
        assert(ring.line_to({-20, 20}));
        assert(ring.close());
        assert(ring.move_to({-6, -6}));
        assert(ring.line_to({6, -6}));
        assert(ring.line_to({6, 6}));
        assert(ring.line_to({-6, 6}));
        assert(ring.close());
        assert(builder.push_clip(std::move(ring), tgfx::FillRule::EvenOdd));
        assert(builder.rect({-100, -100, 200, 200}, tgfx::FillPaint{{1, 0, 0, 1}}));
        assert(builder.pop_clip());
        assert(builder.pop_clip());
        assert(builder.pop_transform());
        assert(builder.rounded_rect({48, 4, 8, 24}, 4, tgfx::FillPaint{{0, 1, 0, 1}}));
        assert(builder.rounded_rect({36, 52, 24, 8}, 4, tgfx::FillPaint{{0, 1, 0, 1}}));
        auto retained_probe = std::make_shared<RetainedProbe>();
        assert(builder.push_opacity(0.4f));
        assert(builder.push_clip_rect({2, 3, 20, 21}));
        assert(builder.retained_batch(retained_probe));
        assert(builder.pop_clip());
        assert(builder.pop_opacity());
        auto list = builder.freeze();
        assert(list);

        tgfx::PipelineCache cache(*device);
        tgfx::RenderContext2 context(*device, cache);
        tgfx::Canvas2DRenderer canvas;
        NoFonts resources;
        const termin::LinearColor black{0, 0, 0, 1};
        context.begin_frame();
        context.begin_pass(target, {}, &black, 1.0f, false);
        canvas.begin(context, static_cast<int>(width), static_cast<int>(height));
        const bool executed = canvas.execute(*list, resources);
        canvas.draw_rect(0, 0, 4, 4, {0, 0, 1, 1});
        canvas.end();
        context.end_pass();
        context.end_frame();
        device->wait_idle();

        float center[4]{};
        float inside_tip[4]{};
        float outside_corner[4]{};
        float immediate_control[4]{};
        float vertical_capsule[4]{};
        float horizontal_capsule[4]{};
        const bool read = device->read_pixel_rgba8(target, 32, 32, center) &&
                          device->read_pixel_rgba8(target, 32, 16, inside_tip) &&
                          device->read_pixel_rgba8(target, 10, 10, outside_corner) &&
                          device->read_pixel_rgba8(target, 1, 1, immediate_control) &&
                          device->read_pixel_rgba8(target, 52, 16, vertical_capsule) &&
                          device->read_pixel_rgba8(target, 48, 56, horizontal_capsule);
        const bool blue_control = immediate_control[0] < 0.1f && immediate_control[1] < 0.1f &&
                                  immediate_control[2] > 0.8f && immediate_control[3] > 0.9f;
        const auto green = [](const float pixel[4]) {
            return pixel[0] < 0.1f && pixel[1] > 0.8f && pixel[2] < 0.1f && pixel[3] > 0.9f;
        };
        const bool result = executed && read && clear(center) && red(inside_tip) && clear(outside_corner) &&
                            blue_control && green(vertical_capsule) && green(horizontal_capsule) &&
                            retained_probe->called && retained_probe->state.has_clip_rect &&
                            !retained_probe->state.unsupported_clip &&
                            std::fabs(retained_probe->state.opacity - 0.4f) < 1.0e-6f;
        if (!result) {
            std::fprintf(stderr,
                         "DrawList2D clip smoke failed: execute=%d read=%d "
                         "center=(%.2f %.2f %.2f %.2f) "
                         "inside=(%.2f %.2f %.2f %.2f) "
                         "outside=(%.2f %.2f %.2f %.2f)\n",
                         executed,
                         read,
                         center[0],
                         center[1],
                         center[2],
                         center[3],
                         inside_tip[0],
                         inside_tip[1],
                         inside_tip[2],
                         inside_tip[3],
                         outside_corner[0],
                         outside_corner[1],
                         outside_corner[2],
                         outside_corner[3]);
            std::fprintf(stderr,
                         "immediate=(%.2f %.2f %.2f %.2f)\n",
                         immediate_control[0],
                         immediate_control[1],
                         immediate_control[2],
                         immediate_control[3]);
        }
        canvas.release_gpu();
        device->destroy(target);
        return result ? 0 : 3;
    }

} // namespace

int main(int argc, char** argv) {
    if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        std::printf("Vulkan backend not compiled, skipping test\n");
        return 0;
    }
    tc_shader_init();
    const int first_result = run(argc > 0 ? argv[0] : "");
    tc_shader_shutdown();
    if (first_result != 0) {
        return first_result;
    }

    // Process-scoped render hosts may replace their device and restart the
    // global registries across an Activity/surface lifecycle. Exercise a
    // second render in the same process so static built-in shader caches must
    // reject generation-stale handles and reacquire the new registry slots.
    tc_shader_init();
    const int second_result = run(argc > 0 ? argv[0] : "");
    tc_shader_shutdown();
    return second_result;
}
