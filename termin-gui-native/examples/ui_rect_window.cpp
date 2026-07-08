#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/widget.hpp>
#include <termin/platform/sdl_backend_window.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

using termin::gui_native::Document;
using termin::gui_native::UiDrawListRenderer;
using termin::gui_native::Widget;

namespace {

class DemoWidget final : public Widget {
public:
    DemoWidget() : Widget(&VTABLE, "DemoWidget") {}

private:
    static void paint(tc_widget* widget, tc_ui_document*, tc_ui_paint_context* context) {
        (void)widget;

        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {0.0f, 0.0f, 800.0f, 600.0f},
            tc_ui_color {0.08f, 0.09f, 0.11f, 1.0f}
        );
        tc_ui_painter_push_clip(context, tc_ui_rect {40.0f, 40.0f, 720.0f, 520.0f});
        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {90.0f, 90.0f, 260.0f, 120.0f},
            tc_ui_color {0.18f, 0.42f, 0.72f, 1.0f}
        );
        tc_ui_painter_stroke_rect(
            context,
            tc_ui_rect {90.0f, 90.0f, 260.0f, 120.0f},
            tc_ui_color {0.85f, 0.92f, 1.0f, 1.0f},
            3.0f
        );
        tc_ui_painter_fill_rect(
            context,
            tc_ui_rect {120.0f, 250.0f, 180.0f, 44.0f},
            tc_ui_color {0.20f, 0.62f, 0.42f, 1.0f}
        );
        tc_ui_painter_stroke_rect(
            context,
            tc_ui_rect {120.0f, 250.0f, 180.0f, 44.0f},
            tc_ui_color {0.75f, 1.0f, 0.86f, 1.0f},
            2.0f
        );
        tc_ui_painter_draw_line(
            context,
            tc_ui_point {410.0f, 120.0f},
            tc_ui_point {690.0f, 320.0f},
            tc_ui_color {0.98f, 0.72f, 0.20f, 1.0f},
            5.0f
        );
        tc_ui_painter_pop_clip(context);
    }

    static const tc_widget_vtable VTABLE;
};

const tc_widget_vtable DemoWidget::VTABLE {
    "DemoWidget",
    nullptr,
    nullptr,
    &DemoWidget::paint,
    nullptr,
    nullptr,
    nullptr,
};

tgfx::TextureHandle create_color_target(tgfx::IRenderDevice& device, int width, int height) {
    tgfx::TextureDesc desc;
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    desc.usage = tgfx::TextureUsage::Sampled
               | tgfx::TextureUsage::ColorAttachment
               | tgfx::TextureUsage::CopySrc;
    return device.create_texture(desc);
}

double example_seconds() {
    const char* value = std::getenv("TERMIN_GUI_NATIVE_EXAMPLE_SECONDS");
    if (!value || value[0] == '\0') {
        return 0.0;
    }
    char* end = nullptr;
    const double parsed = std::strtod(value, &end);
    if (end == value || parsed < 0.0) {
        return 0.0;
    }
    return parsed;
}

bool is_existing_file(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::vector<std::filesystem::path> path_entries() {
    std::vector<std::filesystem::path> entries;
    const char* path_env = std::getenv("PATH");
    if (!path_env) {
        return entries;
    }

    const char separator =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    std::string path_text(path_env);
    size_t start = 0;
    while (start <= path_text.size()) {
        const size_t end = path_text.find(separator, start);
        const std::string part = path_text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!part.empty()) {
            entries.emplace_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return entries;
}

std::filesystem::path with_platform_exe_suffix(std::filesystem::path path) {
#ifdef _WIN32
    if (path.extension().empty()) {
        path += ".exe";
    }
#endif
    return path;
}

std::filesystem::path resolve_tool(const char* env_name, const char* tool_name) {
    if (const char* configured = std::getenv(env_name)) {
        std::filesystem::path path(configured);
        if (is_existing_file(path)) {
            return path;
        }
        std::fprintf(stderr, "%s points to missing file: %s\n", env_name, configured);
        return {};
    }

    std::vector<std::filesystem::path> candidates;
    if (const char* sdk = std::getenv("TERMIN_SDK")) {
        candidates.emplace_back(std::filesystem::path(sdk) / "bin" / tool_name);
    }
    candidates.emplace_back(std::filesystem::current_path() / "sdk" / "bin" / tool_name);
    candidates.emplace_back(std::filesystem::current_path() / "build" / "Debug" / "bin" / tool_name);
    candidates.emplace_back(std::filesystem::current_path() / "build" / "Release" / "bin" / tool_name);

    for (const std::filesystem::path& directory : path_entries()) {
        candidates.emplace_back(directory / tool_name);
    }

    for (std::filesystem::path candidate : candidates) {
        candidate = with_platform_exe_suffix(candidate);
        if (is_existing_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

void set_env_value(const char* name, const std::filesystem::path& value) {
#ifdef _WIN32
    _putenv_s(name, value.string().c_str());
#else
    setenv(name, value.string().c_str(), 1);
#endif
}

bool configure_shader_runtime() {
    const std::filesystem::path shaderc = resolve_tool("TERMIN_SHADERC", "termin_shaderc");
    if (shaderc.empty()) {
        std::fprintf(stderr, "termin-gui-native example: termin_shaderc not found; set TERMIN_SHADERC or TERMIN_SDK\n");
        return false;
    }

    const std::filesystem::path slangc = resolve_tool("TERMIN_SLANGC", "slangc");
    if (slangc.empty()) {
        std::fprintf(stderr, "termin-gui-native example: slangc not found; set TERMIN_SLANGC\n");
        return false;
    }

    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "termin-gui-native-example-shaders";
    const std::filesystem::path artifact_root = root / "artifacts";
    const std::filesystem::path cache_root = root / "cache";
    std::error_code ec;
    std::filesystem::create_directories(artifact_root, ec);
    if (ec) {
        std::fprintf(stderr, "termin-gui-native example: failed to create artifact root %s\n", artifact_root.string().c_str());
        return false;
    }
    std::filesystem::create_directories(cache_root, ec);
    if (ec) {
        std::fprintf(stderr, "termin-gui-native example: failed to create cache root %s\n", cache_root.string().c_str());
        return false;
    }

    set_env_value("TERMIN_SLANGC", slangc);
    termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
    termin::tgfx2_set_shader_cache_root(cache_root.string().c_str());
    termin::tgfx2_set_shader_compiler_path(shaderc.string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);
    return true;
}

} // namespace

int main() {
    try {
        if (!configure_shader_runtime()) {
            return 77;
        }

        termin::SDLBackendWindow window("termin-gui-native rectangle example", 800, 600);
        tgfx::IRenderDevice* device = window.device();
        tgfx::RenderContext2* context = window.context();
        if (!device || !context) {
            std::fprintf(stderr, "termin-gui-native example: window has no render device/context\n");
            return 1;
        }

        Document document;
        auto* demo = new DemoWidget();
        tc_widget_handle root = document.adopt(demo);
        if (tc_widget_handle_is_invalid(root) || !tc_ui_document_add_root(document.get(), root)) {
            std::fprintf(stderr, "termin-gui-native example: failed to create root widget\n");
            return 1;
        }

        tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
        tc_ui_paint_context* paint_context = tc_ui_paint_context_create(draw_list);
        UiDrawListRenderer renderer;

        tgfx::TextureHandle color_target {};
        int target_width = 0;
        int target_height = 0;

        const double max_seconds = example_seconds();
        const auto start = std::chrono::steady_clock::now();

        while (!window.should_close()) {
            window.poll_events();

            const auto [width, height] = window.framebuffer_size();
            if (width <= 0 || height <= 0) {
                continue;
            }

            if (color_target.id == 0 || target_width != width || target_height != height) {
                if (color_target.id != 0) {
                    device->destroy(color_target);
                }
                color_target = create_color_target(*device, width, height);
                target_width = width;
                target_height = height;
            }

            tc_ui_draw_list_clear(draw_list);
            tc_ui_document_paint_roots(document.get(), paint_context);

            context->begin_frame();
            const float clear_color[4] = {0.03f, 0.035f, 0.045f, 1.0f};
            context->begin_pass(color_target, tgfx::TextureHandle {}, clear_color, 1.0f, false);
            renderer.render(*context, draw_list, width, height);
            context->end_pass();
            context->end_frame();
            window.present(color_target);

            if (max_seconds > 0.0) {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - start).count();
                if (elapsed >= max_seconds) {
                    break;
                }
            }
        }

        renderer.release_gpu();
        tc_ui_paint_context_destroy(paint_context);
        tc_ui_draw_list_destroy(draw_list);
        if (color_target.id != 0) {
            device->destroy(color_target);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "termin-gui-native rectangle example failed: %s\n", e.what());
        if (std::strstr(e.what(), "No available video device") ||
            std::strstr(e.what(), "Vulkan support is either not configured in SDL")) {
            return 77;
        }
        return 1;
    }
}
