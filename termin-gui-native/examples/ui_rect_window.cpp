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
#include <termin/gui_native/widgets.hpp>
#include <termin/platform/sdl_backend_window.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

using termin::gui_native::Document;
using termin::gui_native::DocumentBuilder;
using termin::gui_native::UiDrawListRenderer;
using termin::gui_native::BoxLayout;
using termin::gui_native::Button;
using termin::gui_native::Checkbox;
using termin::gui_native::Color;
using termin::gui_native::EdgeInsets;
using termin::gui_native::Orientation;
using termin::gui_native::Panel;
using termin::gui_native::ProgressBar;
using termin::gui_native::Slider;
using termin::gui_native::Spacer;
using termin::gui_native::Swatch;

namespace {

struct ShowcaseRefs {
    ProgressBar* progress = nullptr;
    Slider* slider = nullptr;
    Checkbox* checkbox = nullptr;
};

ShowcaseRefs build_showcase(Document& document) {
    DocumentBuilder ui(document);
    ShowcaseRefs refs;

    auto& root = ui.make_root<BoxLayout>(Orientation::Vertical, "showcase-root");
    root.set_padding(EdgeInsets {18.0f, 18.0f, 18.0f, 18.0f})
        .set_spacing(14.0f)
        .set_background(Color {0.055f, 0.060f, 0.070f, 1.0f});

    auto& top = ui.make<BoxLayout>(Orientation::Horizontal, "showcase-top");
    top.set_spacing(12.0f);
    root.add_child(top);

    auto& navigation = ui.make<BoxLayout>(Orientation::Vertical, "navigation");
    navigation.set_padding(EdgeInsets {12.0f, 12.0f, 12.0f, 12.0f})
        .set_spacing(8.0f)
        .set_background(Color {0.10f, 0.11f, 0.13f, 1.0f})
        .set_border(Color {0.28f, 0.30f, 0.34f, 1.0f}, 1.0f);
    top.add_child(navigation);
    navigation.add_child(ui.make<Button>(Color {0.18f, 0.32f, 0.54f, 1.0f}));
    navigation.add_child(ui.make<Button>(Color {0.16f, 0.42f, 0.32f, 1.0f}));
    navigation.add_child(ui.make<Button>(Color {0.48f, 0.28f, 0.20f, 1.0f}));

    auto& content = ui.make<BoxLayout>(Orientation::Vertical, "content");
    content.set_padding(EdgeInsets {14.0f, 14.0f, 14.0f, 14.0f})
        .set_spacing(10.0f)
        .set_background(Color {0.13f, 0.14f, 0.16f, 1.0f})
        .set_border(Color {0.36f, 0.39f, 0.44f, 1.0f}, 1.0f);
    top.add_child(content);

    auto& hero_row = ui.make<BoxLayout>(Orientation::Horizontal, "hero-row");
    hero_row.set_spacing(10.0f);
    content.add_child(hero_row);
    hero_row.add_child(ui.make<Panel>("preview-a").set_fill(Color {0.20f, 0.42f, 0.62f, 1.0f}));
    hero_row.add_child(ui.make<Panel>("preview-b").set_fill(Color {0.26f, 0.48f, 0.34f, 1.0f}));

    auto& controls = ui.make<BoxLayout>(Orientation::Horizontal, "controls");
    controls.set_spacing(10.0f);
    content.add_child(controls);
    refs.checkbox = &ui.make<Checkbox>(true);
    refs.slider = &ui.make<Slider>(0.62f);
    refs.progress = &ui.make<ProgressBar>(0.35f);
    controls.add_child(*refs.checkbox);
    controls.add_child(*refs.slider);
    controls.add_child(*refs.progress);

    auto& palette = ui.make<BoxLayout>(Orientation::Horizontal, "palette");
    palette.set_spacing(8.0f);
    content.add_child(palette);
    palette.add_child(ui.make<Swatch>(Color {0.90f, 0.22f, 0.24f, 1.0f}));
    palette.add_child(ui.make<Swatch>(Color {0.95f, 0.72f, 0.28f, 1.0f}));
    palette.add_child(ui.make<Swatch>(Color {0.24f, 0.68f, 0.48f, 1.0f}));
    palette.add_child(ui.make<Swatch>(Color {0.28f, 0.52f, 0.92f, 1.0f}));

    auto& bottom = ui.make<BoxLayout>(Orientation::Horizontal, "bottom");
    bottom.set_spacing(12.0f);
    root.add_child(bottom);
    bottom.add_child(ui.make<Panel>("status-a").set_fill(Color {0.12f, 0.20f, 0.26f, 1.0f}));
    bottom.add_child(ui.make<Panel>("status-b").set_fill(Color {0.22f, 0.17f, 0.28f, 1.0f}));
    bottom.add_child(ui.make<Spacer>(tc_ui_size {32.0f, 32.0f}));

    return refs;
}

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
        ShowcaseRefs showcase = build_showcase(document);

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
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            if (showcase.progress) {
                showcase.progress->set_value(static_cast<float>((std::sin(elapsed) + 1.0) * 0.5));
            }
            if (showcase.slider && showcase.checkbox && showcase.checkbox->checked()) {
                showcase.slider->set_value(static_cast<float>((std::sin(elapsed * 0.6) + 1.0) * 0.5));
            }
            document.layout_roots(tc_ui_rect {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
            document.paint_roots(paint_context);

            context->begin_frame();
            const float clear_color[4] = {0.03f, 0.035f, 0.045f, 1.0f};
            context->begin_pass(color_target, tgfx::TextureHandle {}, clear_color, 1.0f, false);
            renderer.render(*context, draw_list, width, height);
            context->end_pass();
            context->end_frame();
            window.present(color_target);

            if (max_seconds > 0.0) {
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
