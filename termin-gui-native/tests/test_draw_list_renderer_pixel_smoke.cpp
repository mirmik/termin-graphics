#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/color_picker.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr uint32_t kWidth = 128;
constexpr uint32_t kHeight = 64;

bool existing_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

void configure_shader_artifacts(const char* argv0, const std::filesystem::path& root) {
    std::vector<std::filesystem::path> candidates;
    if (const char* configured = std::getenv("TERMIN_SHADERC")) {
        if (configured[0] != '\0')
            candidates.emplace_back(configured);
    }
    if (argv0 && argv0[0] != '\0') {
        std::error_code error;
        const std::filesystem::path executable_directory =
            std::filesystem::absolute(argv0, error).parent_path();
        if (!error)
            candidates.push_back(executable_directory / "termin_shaderc");
    }
    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        if (sdk[0] != '\0') {
            candidates.push_back(std::filesystem::path(sdk) / "bin" / "termin_shaderc");
        }
    }
    candidates.push_back(std::filesystem::current_path() / "sdk" / "bin" / "termin_shaderc");
    for (const std::filesystem::path& candidate : candidates) {
        if (existing_file(candidate)) {
            termin::tgfx2_set_shader_compiler_path(candidate.string().c_str());
            break;
        }
    }
    termin::tgfx2_set_shader_artifact_root(root.string().c_str());
    termin::tgfx2_set_shader_cache_root((root / ".cache").string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);
}

struct ScopedTempDirectory {
    std::filesystem::path path;

    ~ScopedTempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

bool looks_green(const float* pixel) {
    return pixel[0] < 0.25f && pixel[1] > 0.65f && pixel[2] < 0.25f;
}

bool looks_red(const float* pixel) {
    return pixel[0] > 0.65f && pixel[1] < 0.25f && pixel[2] < 0.25f;
}

bool looks_blue(const float* pixel) {
    return pixel[0] < 0.25f && pixel[1] < 0.35f && pixel[2] > 0.65f;
}

bool looks_yellow(const float* pixel) {
    return pixel[0] > 0.65f && pixel[1] > 0.65f && pixel[2] < 0.25f;
}

bool looks_black(const float* pixel) {
    return pixel[0] < 0.05f && pixel[1] < 0.05f && pixel[2] < 0.05f;
}

const float* pixel_at(const std::vector<float>& pixels, uint32_t x, uint32_t y) {
    return &pixels[(static_cast<size_t>(y) * kWidth + x) * 4u];
}

int run_smoke(const char* argv0, tgfx::BackendType backend) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const ScopedTempDirectory artifacts{
        std::filesystem::temp_directory_path() /
        ("termin-gui-native-renderer-smoke-" + std::to_string(unique))};
    configure_shader_artifacts(argv0, artifacts.path);

    std::unique_ptr<tgfx::IRenderDevice> device;
    try {
        device = tgfx::create_device(backend);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Failed to create %s device: %s\n", tgfx::backend_name(backend),
                     error.what());
        return 1;
    }

    tgfx::TextureDesc target_desc;
    target_desc.width = kWidth;
    target_desc.height = kHeight;
    target_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    target_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
    const tgfx::TextureHandle target = device->create_texture(target_desc);

    tgfx::TextureDesc image_desc;
    image_desc.width = 2;
    image_desc.height = 2;
    image_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    image_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle image = device->create_texture(image_desc);
    if (!target || !image) {
        std::fprintf(stderr, "Failed to create renderer smoke textures\n");
        return 1;
    }
    const uint8_t green_pixels[]{
        20, 230, 30, 255, 20, 230, 30, 255, 20, 230, 30, 255, 20, 230, 30, 255,
    };
    device->upload_texture(image, std::span<const uint8_t>(green_pixels, sizeof(green_pixels)));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* painter = tc_ui_paint_context_create(draw_list);
    tc_ui_painter_draw_texture(painter, image.id, tc_ui_rect{8.0f, 8.0f, 16.0f, 16.0f},
                               tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f}, false);
    tc_ui_painter_push_clip(painter, tc_ui_rect{40.0f, 8.0f, 24.0f, 24.0f});
    tc_ui_painter_push_clip(painter, tc_ui_rect{48.0f, 12.0f, 8.0f, 8.0f});
    tc_ui_painter_fill_rect(painter, tc_ui_rect{36.0f, 4.0f, 40.0f, 40.0f},
                            tc_ui_color{0.9f, 0.05f, 0.05f, 1.0f});
    tc_ui_painter_pop_clip(painter);
    tc_ui_painter_pop_clip(painter);
    tc_ui_painter_fill_rounded_rect(painter, tc_ui_rect{8.0f, 40.0f, 24.0f, 20.0f}, 8.0f,
                                    tc_ui_color{0.05f, 0.15f, 0.9f, 1.0f});
    tc_ui_painter_fill_circle(painter, tc_ui_point{48.0f, 50.0f}, 8.0f,
                              tc_ui_color{0.9f, 0.85f, 0.05f, 1.0f}, 24);
    constexpr uint32_t text_baseline = 30;
    tc_ui_painter_draw_text(
        painter,
        "Native",
        tc_ui_point{72.0f, static_cast<float>(text_baseline)},
        20.0f,
        tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f}
    );

    tgfx::PipelineCache cache(*device);
    tgfx::RenderContext2 context(*device, cache);
    termin::gui_native::UiDrawListRenderer renderer;
    const std::string font_path =
        std::string(TERMIN_GUI_NATIVE_SOURCE_DIR) +
        "/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf";
    if (!renderer.set_default_font_path(font_path, 14)) {
        std::fprintf(stderr, "Failed to configure renderer smoke font\n");
        return 1;
    }

    termin::gui_native::ColorPicker color_picker;

    const float clear[]{0.0f, 0.0f, 0.0f, 1.0f};
    context.begin_frame();
    renderer.sync_color_picker_surfaces(context, color_picker);
    const auto picker_textures = color_picker.texture_ids();
    const bool picker_textures_ready = picker_textures.saturation_value != 0 &&
        picker_textures.hue != 0 && picker_textures.alpha != 0;
    if (picker_textures_ready) {
        tc_ui_painter_draw_texture(
            painter,
            picker_textures.saturation_value,
            tc_ui_rect{90.0f, 40.0f, 28.0f, 20.0f},
            tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
            false
        );
    }
    context.begin_pass(target, {}, clear, 1.0f, false);
    renderer.render(context, draw_list, static_cast<int>(kWidth), static_cast<int>(kHeight));
    context.end_pass();
    context.end_frame();
    device->wait_idle();

    std::vector<float> pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
    const bool read_ok = device->read_texture_rgba_float(target, pixels.data());
    const bool image_ok = read_ok && looks_green(pixel_at(pixels, 16, 16));
    const bool nested_clip_inside_ok = read_ok && looks_red(pixel_at(pixels, 52, 16));
    const bool nested_clip_outside_ok = read_ok && looks_black(pixel_at(pixels, 44, 16));
    const bool rounded_center_ok = read_ok && looks_blue(pixel_at(pixels, 20, 50));
    const bool rounded_corner_ok = read_ok && looks_black(pixel_at(pixels, 9, 41));
    const bool circle_ok = read_ok && looks_yellow(pixel_at(pixels, 48, 50));
    const float* picker_pixel = pixel_at(pixels, 96, 48);
    const bool picker_texture_ok = read_ok && picker_textures_ready &&
        (picker_pixel[0] > 0.1f || picker_pixel[1] > 0.1f || picker_pixel[2] > 0.1f);
    size_t text_signal = 0;
    uint32_t text_min_y = kHeight;
    uint32_t text_max_y = 0;
    if (read_ok) {
        for (uint32_t y = 0; y < 40; ++y) {
            for (uint32_t x = 68; x < kWidth; ++x) {
                const float* pixel = pixel_at(pixels, x, y);
                if (pixel[0] > 0.15f || pixel[1] > 0.15f || pixel[2] > 0.15f) {
                    ++text_signal;
                    text_min_y = std::min(text_min_y, y);
                    text_max_y = std::max(text_max_y, y);
                }
            }
        }
    }
    // The painter position is a baseline. Most ink must therefore be above it;
    // this catches accidentally forwarding the baseline as Canvas2D's line top.
    const bool text_ok = text_signal >= 8 && text_min_y < text_baseline
        && text_max_y <= text_baseline + 5;

    renderer.release_color_picker_surfaces(color_picker);
    renderer.release_gpu();
    tc_ui_paint_context_destroy(painter);
    tc_ui_draw_list_destroy(draw_list);
    device->destroy(image);
    device->destroy(target);

    if (!read_ok || !image_ok || !nested_clip_inside_ok || !nested_clip_outside_ok ||
        !rounded_center_ok || !rounded_corner_ok || !circle_ok || !picker_texture_ok || !text_ok) {
        std::fprintf(stderr,
                     "UI renderer %s pixel smoke failed: read=%d image=%d clip_in=%d clip_out=%d "
                     "round_center=%d round_corner=%d circle=%d picker=%d text=%d signal=%zu y=[%u,%u]\n",
                     tgfx::backend_name(backend), read_ok, image_ok, nested_clip_inside_ok,
                     nested_clip_outside_ok, rounded_center_ok, rounded_corner_ok, circle_ok,
                     picker_texture_ok, text_ok, text_signal, text_min_y, text_max_y);
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    tc_shader_init();
    int result = 0;
    size_t tested_backends = 0;
    for (const tgfx::BackendType backend : {tgfx::BackendType::Vulkan, tgfx::BackendType::D3D11}) {
        if (!tgfx::backend_is_compiled(backend))
            continue;
        ++tested_backends;
        if (run_smoke(argc > 0 ? argv[0] : nullptr, backend) != 0) {
            result = 1;
            break;
        }
    }
    if (tested_backends == 0) {
        std::printf("No headless UI renderer backend compiled; desktop OpenGL smoke is separate\n");
    }
    tc_shader_shutdown();
    return result;
}
