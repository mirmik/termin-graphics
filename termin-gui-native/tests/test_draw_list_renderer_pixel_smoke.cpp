#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/draw_list2d_bridge.hpp>
#include <termin/gui_native/native_document_painter.hpp>
#include <termin/gui_native/native_widget.hpp>

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
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {

    constexpr uint32_t kWidth = 128;
    constexpr uint32_t kHeight = 96;

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
            const std::filesystem::path executable_directory = std::filesystem::absolute(argv0, error).parent_path();
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

    bool looks_purple(const float* pixel) {
        return pixel[0] > 0.25f && pixel[2] > 0.25f && pixel[1] < 0.15f;
    }

    const float* pixel_at(const std::vector<float>& pixels, uint32_t x, uint32_t y) {
        return &pixels[(static_cast<size_t>(y) * kWidth + x) * 4u];
    }

    class PainterProbe final : public termin::gui_native::NativeWidget {
    public:
        bool draw_commands = false;
        bool draw_shared_composition = false;
        bool malformed_scope = false;
        uint32_t image = 0;
        uint32_t sampling_image = 0;
        uint32_t picker_image = 0;
        tc_ui_srgb_color order_color{};
        tc_ui_rect order_rect{112.0f, 72.0f, 8.0f, 8.0f};

        void paint(tc_ui_document_handle, tc_ui_paint_context* painter) override {
            if (malformed_scope) {
                tc_ui_painter_fill_rect(
                    painter, tc_ui_rect{2.0f, 2.0f, 4.0f, 4.0f}, tc_ui_srgb_color{0.9f, 0.05f, 0.05f, 1.0f});
                tc_ui_painter_pop_clip(painter);
                return;
            }
            if (order_color.a > 0.0f) {
                tc_ui_painter_fill_rect(painter, order_rect, order_color);
            }
            if (draw_shared_composition) {
                const tc_ui_uniform_transform transform{{64.25f, 40.25f}, 1.5f};
                tc_ui_painter_push_uniform_transform(painter, transform);
                tc_ui_painter_push_clip(painter, tc_ui_rect{0.0f, 0.0f, 8.0f, 8.0f});
                tgfx::DrawList2DBuilder nested_builder;
                if (!nested_builder.rect(
                        {4.0f, 2.0f, 8.0f, 4.0f},
                        tgfx::FillPaint{termin::srgb_to_linear(termin::SrgbColor{0.05f, 0.85f, 0.05f, 1.0f})})) {
                    throw std::runtime_error("failed to build nested composition probe");
                }
                auto nested = nested_builder.freeze();
                if (!nested || !termin::gui_native::append_draw_list2d(painter, std::move(*nested))) {
                    throw std::runtime_error("failed to append nested composition probe");
                }
                tc_ui_painter_pop_clip(painter);
                tc_ui_painter_pop_transform(painter);
            }
            if (!draw_commands) {
                return;
            }
            tc_ui_painter_draw_texture(painter,
                                       image,
                                       tc_ui_rect{8.0f, 8.0f, 16.0f, 16.0f},
                                       tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_LINEAR,
                                       false);
            tc_ui_painter_fill_rect(painter,
                                    tc_ui_rect{100.0f, 84.0f, 8.0f, 8.0f},
                                    tc_ui_srgb_color{128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f});
            tc_ui_painter_push_clip(painter, tc_ui_rect{40.0f, 8.0f, 24.0f, 24.0f});
            tc_ui_painter_push_clip(painter, tc_ui_rect{48.0f, 12.0f, 8.0f, 8.0f});
            tc_ui_painter_fill_rect(
                painter, tc_ui_rect{36.0f, 4.0f, 40.0f, 40.0f}, tc_ui_srgb_color{0.9f, 0.05f, 0.05f, 1.0f});
            tc_ui_painter_pop_clip(painter);
            tc_ui_painter_pop_clip(painter);
            tc_ui_painter_fill_rounded_rect(
                painter, tc_ui_rect{8.0f, 40.0f, 24.0f, 20.0f}, 8.0f, tc_ui_srgb_color{0.05f, 0.15f, 0.9f, 1.0f});
            tc_ui_painter_fill_circle(
                painter, tc_ui_point{48.0f, 50.0f}, 8.0f, tc_ui_srgb_color{0.9f, 0.85f, 0.05f, 1.0f}, 24);
            tc_ui_painter_draw_texture(painter,
                                       sampling_image,
                                       tc_ui_rect{8.0f, 72.0f, 16.0f, 16.0f},
                                       tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_NEAREST,
                                       false);
            tc_ui_painter_draw_texture(painter,
                                       sampling_image,
                                       tc_ui_rect{32.0f, 72.0f, 16.0f, 16.0f},
                                       tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_LINEAR,
                                       false);
            tc_ui_painter_draw_icon(
                painter, "add", tc_ui_rect{64.0f, 72.0f, 16.0f, 16.0f}, tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f});
            tc_ui_painter_draw_text(
                painter, "Native", tc_ui_point{72.0f, 30.0f}, 20.0f, tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f});
            if (picker_image != 0) {
                tc_ui_painter_draw_texture(painter,
                                           picker_image,
                                           tc_ui_rect{90.0f, 40.0f, 28.0f, 20.0f},
                                           tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f},
                                           TC_UI_TEXTURE_SAMPLING_LINEAR,
                                           false);
            }
        }
    };

    struct ProbeDocument {
        tc_ui_document_handle handle = tc_ui_document_create();
        termin::gui_native::TcDocument document{handle};
        PainterProbe* probe = new PainterProbe();

        ProbeDocument() {
            const tc_widget_handle root = document.adopt(probe);
            if (tc_widget_handle_is_invalid(root) || !tc_ui_document_add_root(handle, root)) {
                throw std::runtime_error("failed to create painter probe document");
            }
        }

        ~ProbeDocument() {
            if (tc_ui_document_is_valid(handle)) {
                tc_ui_document_destroy(handle);
            }
        }

        ProbeDocument(const ProbeDocument&) = delete;
        ProbeDocument& operator=(const ProbeDocument&) = delete;
    };

    int run_smoke(const char* argv0, tgfx::BackendType backend) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const ScopedTempDirectory artifacts{std::filesystem::temp_directory_path() /
                                            ("termin-gui-native-renderer-smoke-" + std::to_string(unique))};
        configure_shader_artifacts(argv0, artifacts.path);

        std::unique_ptr<tgfx::IRenderDevice> device;
        try {
            device = tgfx::create_device(backend);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Failed to create %s device: %s\n", tgfx::backend_name(backend), error.what());
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
        image_desc.format = tgfx::PixelFormat::RGBA8_sRGB;
        image_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
        const tgfx::TextureHandle image = device->create_texture(image_desc);
        image_desc.height = 1;
        image_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        const tgfx::TextureHandle sampling_image = device->create_texture(image_desc);
        if (!target || !image || !sampling_image) {
            std::fprintf(stderr, "Failed to create renderer smoke textures\n");
            return 1;
        }
        const uint8_t srgb_gray_pixels[]{
            128,
            128,
            128,
            255,
            128,
            128,
            128,
            255,
            128,
            128,
            128,
            255,
            128,
            128,
            128,
            255,
        };
        device->upload_texture(image, std::span<const uint8_t>(srgb_gray_pixels, sizeof(srgb_gray_pixels)));
        const uint8_t sampling_pixels[]{255, 0, 0, 255, 0, 0, 255, 255};
        device->upload_texture(sampling_image, std::span<const uint8_t>(sampling_pixels, sizeof(sampling_pixels)));

        constexpr uint32_t text_baseline = 30;

        tgfx::PipelineCache cache(*device);
        tgfx::RenderContext2 context(*device, cache);
        termin::gui_native::NativeDocumentPainter painter;
        const std::string font_path = std::string(TERMIN_GUI_NATIVE_SOURCE_DIR) +
                                      "/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf";
        if (!painter.set_default_font_path(font_path, 14)) {
            std::fprintf(stderr, "Failed to configure renderer smoke font\n");
            return 1;
        }

        ProbeDocument commands;
        commands.probe->draw_commands = true;
        commands.probe->image = image.id;
        commands.probe->sampling_image = sampling_image.id;
        ProbeDocument early;
        early.probe->order_color = tc_ui_srgb_color{0.9f, 0.85f, 0.05f, 1.0f};
        ProbeDocument identity_first;
        identity_first.probe->order_color = tc_ui_srgb_color{0.05f, 0.85f, 0.05f, 1.0f};
        ProbeDocument identity_last;
        identity_last.probe->order_color = tc_ui_srgb_color{0.9f, 0.05f, 0.05f, 1.0f};
        ProbeDocument shared_composition;
        shared_composition.probe->draw_shared_composition = true;
        ProbeDocument malformed;
        malformed.probe->malformed_scope = true;
        ProbeDocument recovered;
        recovered.probe->order_color = tc_ui_srgb_color{0.05f, 0.85f, 0.05f, 1.0f};
        recovered.probe->order_rect = tc_ui_rect{2.0f, 2.0f, 4.0f, 4.0f};

        const termin::SrgbColor picker_srgb{0x92 / 255.0f, 0x30 / 255.0f, 0x30 / 255.0f, 1.0f};
        auto picker_model = std::make_shared<termin::gui_native::ColorPickerModel>(picker_srgb, true);
        termin::gui_native::ColorPicker color_picker(picker_model);

        const termin::LinearColor clear{0.0f, 0.0f, 0.0f, 1.0f};
        context.begin_frame();
        painter.sync_color_picker_surfaces(context, color_picker);
        const auto picker_textures = color_picker.texture_ids();
        const bool picker_textures_ready =
            picker_textures.saturation_value != 0 && picker_textures.hue != 0 && picker_textures.alpha != 0;
        commands.probe->picker_image = picker_textures.saturation_value;
        const tc_ui_presentation_metrics metrics =
            tc_ui_presentation_metrics_identity(tc_ui_size{static_cast<float>(kWidth), static_cast<float>(kHeight)});
        const termin::gui_native::UiDocumentSubmission submissions[]{
            {identity_last.document, 0, 20, metrics},
            {termin::gui_native::TcDocument{}, -100, 0, metrics},
            {commands.document, 100, 1, metrics},
            {shared_composition.document, 50, 0, metrics},
            {malformed.document, -20, 0, metrics},
            {recovered.document, -10, 0, metrics},
            {early.document, -1, 100, metrics},
            {identity_first.document, 0, 10, metrics},
        };
        context.begin_pass(target, {}, &clear, 1.0f, false);
        const std::size_t painted =
            painter.paint_documents(context, static_cast<int>(kWidth), static_cast<int>(kHeight), submissions);
        context.end_pass();
        context.end_frame();
        device->wait_idle();

        std::vector<float> pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
        const bool read_ok = device->read_texture_rgba_float(target, pixels.data());
        constexpr float srgb_mid_linear = 0.21586f;
        const auto looks_linear_mid_gray = [srgb_mid_linear](const float* pixel) {
            return std::fabs(pixel[0] - srgb_mid_linear) < 0.025f && std::fabs(pixel[1] - srgb_mid_linear) < 0.025f &&
                   std::fabs(pixel[2] - srgb_mid_linear) < 0.025f;
        };
        const bool image_ok = read_ok && looks_linear_mid_gray(pixel_at(pixels, 16, 16));
        const bool authored_gray_ok = read_ok && looks_linear_mid_gray(pixel_at(pixels, 104, 88));
        const bool nested_clip_inside_ok = read_ok && looks_red(pixel_at(pixels, 52, 16));
        const bool nested_clip_outside_ok = read_ok && looks_black(pixel_at(pixels, 44, 16));
        const bool rounded_center_ok = read_ok && looks_blue(pixel_at(pixels, 20, 50));
        const bool rounded_corner_ok = read_ok && looks_black(pixel_at(pixels, 9, 41));
        const bool circle_ok = read_ok && looks_yellow(pixel_at(pixels, 48, 50));
        const bool nearest_left_ok = read_ok && looks_red(pixel_at(pixels, 15, 80));
        const bool nearest_right_ok = read_ok && looks_blue(pixel_at(pixels, 16, 80));
        const bool linear_mid_ok = read_ok && looks_purple(pixel_at(pixels, 39, 80));
        const bool icon_center_ok = read_ok && pixel_at(pixels, 72, 80)[0] > 0.8f;
        const bool icon_corner_ok = read_ok && looks_black(pixel_at(pixels, 66, 74));
        const bool ordering_ok = read_ok && looks_red(pixel_at(pixels, 116, 76));
        const bool nested_transform_clip_inside_ok = read_ok && looks_green(pixel_at(pixels, 72, 46));
        const bool nested_transform_clip_outside_ok = read_ok && looks_black(pixel_at(pixels, 78, 46));
        const bool malformed_batch_recovery_ok = read_ok && looks_green(pixel_at(pixels, 3, 3));
        constexpr float picker_left = 90.0f;
        constexpr float picker_top = 40.0f;
        constexpr float picker_width = 28.0f;
        constexpr float picker_height = 20.0f;
        const uint32_t picker_x =
            static_cast<uint32_t>(std::floor(picker_left + picker_model->saturation() * picker_width));
        const uint32_t picker_y =
            static_cast<uint32_t>(std::floor(picker_top + (1.0f - picker_model->value()) * picker_height));
        const float* picker_pixel = pixel_at(pixels, picker_x, picker_y);
        const termin::LinearColor picker_linear = termin::srgb_to_linear(picker_srgb);
        const bool picker_texture_ok = read_ok && picker_textures_ready &&
                                       std::fabs(picker_pixel[0] - picker_linear.r) < 0.04f &&
                                       std::fabs(picker_pixel[1] - picker_linear.g) < 0.04f &&
                                       std::fabs(picker_pixel[2] - picker_linear.b) < 0.04f;
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
        const bool text_ok = text_signal >= 8 && text_min_y < text_baseline && text_max_y <= text_baseline + 5;

        bool scaled_geometry_ok = true;
        ProbeDocument scaled_document;
        scaled_document.probe->order_color = tc_ui_srgb_color{0.05f, 0.85f, 0.05f, 1.0f};
        scaled_document.probe->order_rect = tc_ui_rect{8.0f, 8.0f, 8.0f, 8.0f};
        for (const float scale : {1.0f, 1.5f, 2.0f, 3.0f}) {
            const tc_ui_presentation_metrics scaled_metrics{
                scale,
                1.0f,
                tc_ui_size{
                    static_cast<float>(kWidth),
                    static_cast<float>(kHeight),
                },
                tc_ui_insets{},
            };
            const termin::gui_native::UiDocumentSubmission scaled_submission{
                scaled_document.document, 0, 0, scaled_metrics};
            context.begin_frame();
            context.begin_pass(target, {}, &clear, 1.0f, false);
            scaled_geometry_ok =
                scaled_geometry_ok && painter.paint_documents(context,
                                                              static_cast<int>(kWidth),
                                                              static_cast<int>(kHeight),
                                                              std::span<const termin::gui_native::UiDocumentSubmission>(
                                                                  &scaled_submission, 1)) == 1;
            context.end_pass();
            context.end_frame();
            device->wait_idle();
            std::vector<float> scaled_pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
            scaled_geometry_ok = scaled_geometry_ok && device->read_texture_rgba_float(target, scaled_pixels.data());
            const uint32_t sample =
                static_cast<uint32_t>((std::round(8.0f * scale) + std::round(16.0f * scale)) * 0.5f);
            scaled_geometry_ok = scaled_geometry_ok && looks_green(pixel_at(scaled_pixels, sample, sample));
            const tc_ui_rect logical_bounds = scaled_document.probe->bounds();
            scaled_geometry_ok = scaled_geometry_ok &&
                                 std::fabs(logical_bounds.width - static_cast<float>(kWidth) / scale) < 0.001f &&
                                 std::fabs(logical_bounds.height - static_cast<float>(kHeight) / scale) < 0.001f;
        }

        painter.release_color_picker_surfaces(color_picker);
        painter.close();
        device->destroy(image);
        device->destroy(sampling_image);
        device->destroy(target);

        if (painted != std::size(submissions) - 1 || !read_ok || !image_ok || !nested_clip_inside_ok ||
            !authored_gray_ok || !nested_clip_outside_ok || !rounded_center_ok || !rounded_corner_ok || !circle_ok ||
            !picker_texture_ok || !icon_center_ok || !icon_corner_ok || !text_ok || !scaled_geometry_ok ||
            !nested_transform_clip_inside_ok || !nested_transform_clip_outside_ok || !malformed_batch_recovery_ok) {
            std::fprintf(stderr,
                         "UI renderer %s pixel smoke failed: read=%d image=%d authored_gray=%d clip_in=%d clip_out=%d "
                         "round_center=%d round_corner=%d circle=%d picker=%d icon_center=%d icon_corner=%d "
                         "picker_rgb=(%.3f,%.3f,%.3f) "
                         "text=%d scaled=%d nested_shared=(%d,%d) recovery=%d signal=%zu y=[%u,%u]\n",
                         tgfx::backend_name(backend),
                         read_ok,
                         image_ok,
                         authored_gray_ok,
                         nested_clip_inside_ok,
                         nested_clip_outside_ok,
                         rounded_center_ok,
                         rounded_corner_ok,
                         circle_ok,
                         picker_texture_ok,
                         icon_center_ok,
                         icon_corner_ok,
                         picker_pixel[0],
                         picker_pixel[1],
                         picker_pixel[2],
                         text_ok,
                         scaled_geometry_ok,
                         nested_transform_clip_inside_ok,
                         nested_transform_clip_outside_ok,
                         malformed_batch_recovery_ok,
                         text_signal,
                         text_min_y,
                         text_max_y);
            return 1;
        }
        if (!nearest_left_ok || !nearest_right_ok || !linear_mid_ok) {
            std::fprintf(stderr,
                         "UI renderer %s sampling smoke failed: nearest_left=%d nearest_right=%d "
                         "linear_mid=%d\n",
                         tgfx::backend_name(backend),
                         nearest_left_ok,
                         nearest_right_ok,
                         linear_mid_ok);
            return 1;
        }
        if (!ordering_ok) {
            std::fprintf(stderr, "UI painter %s submission ordering smoke failed\n", tgfx::backend_name(backend));
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
        std::printf("UI renderer pixel smoke exercising backend: %s\n", tgfx::backend_name(backend));
        if (run_smoke(argc > 0 ? argv[0] : nullptr, backend) != 0) {
            result = 1;
            break;
        }
        std::printf("UI renderer pixel smoke validated control, text, clip and sampling "
                    "pixels on backend: %s\n",
                    tgfx::backend_name(backend));
    }
    if (tested_backends == 0) {
        std::printf("UI renderer pixel smoke skipped: no Vulkan or D3D11 backend compiled; "
                    "desktop OpenGL smoke is separate\n");
        result = 77;
    }
    tc_shader_shutdown();
    return result;
}
