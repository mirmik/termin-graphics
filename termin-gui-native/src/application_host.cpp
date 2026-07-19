#include "termin/gui_native/application_host.hpp"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <tcbase/tc_log.h>

#include <termin/gui_native/draw_list_renderer.hpp>
#include <termin/gui_native/window_input.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

namespace termin::gui_native {

namespace {

namespace fs = std::filesystem;

int host_module_anchor = 0;

[[noreturn]] void host_error(const std::string& message) {
    tc_log_error("[gui-native-host] %s", message.c_str());
    throw std::runtime_error(message);
}

bool is_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

fs::path module_path() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&host_module_anchor),
            &module)) {
        host_error("ApplicationHost could not locate its loaded DLL");
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        host_error("ApplicationHost could not read its loaded DLL path");
    }
    buffer.resize(length);
    return fs::path(buffer);
#else
    Dl_info info{};
    if (dladdr(&host_module_anchor, &info) == 0 || !info.dli_fname) {
        host_error("ApplicationHost could not locate its loaded shared library");
    }
    return fs::absolute(info.dli_fname);
#endif
}

fs::path executable_path(fs::path path) {
#ifdef _WIN32
    if (path.extension().empty()) path += ".exe";
#endif
    return path;
}

fs::path resolve_sdk_root(const ApplicationHostConfig& config) {
    if (!config.sdk_root.empty()) return fs::absolute(config.sdk_root);
    if (const char* environment = std::getenv("TERMIN_SDK")) {
        if (environment[0] != '\0') return fs::absolute(environment);
    }
    // Installed layout is <sdk>/lib/lib...so or <sdk>/bin/...dll.
    return module_path().parent_path().parent_path();
}

fs::path resolve_required_file(
    const std::string& configured,
    const char* environment_name,
    const fs::path& sdk_default,
    const char* label) {
    fs::path path;
    if (!configured.empty()) {
        path = configured;
    } else if (const char* environment = std::getenv(environment_name)) {
        if (environment[0] != '\0') path = environment;
    }
    if (path.empty()) path = sdk_default;
    path = fs::absolute(executable_path(std::move(path)));
    if (!is_file(path)) {
        host_error(
            std::string("ApplicationHost could not find ") + label +
            " at " + path.string() + "; configure it explicitly or set " +
            environment_name);
    }
    return path;
}

fs::path find_on_path(const fs::path& executable) {
    const char* path_environment = std::getenv("PATH");
    if (!path_environment || path_environment[0] == '\0') return {};
    const std::string paths(path_environment);
#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    size_t begin = 0;
    while (begin <= paths.size()) {
        const size_t end = paths.find(separator, begin);
        const std::string entry = paths.substr(
            begin,
            end == std::string::npos ? std::string::npos : end - begin);
        if (!entry.empty()) {
            const fs::path candidate = fs::path(entry) / executable_path(executable);
            if (is_file(candidate)) return fs::absolute(candidate);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

fs::path resolve_required_tool(
    const std::string& configured,
    const char* environment_name,
    const fs::path& sdk_default,
    const char* executable_name,
    const char* label) {
    if (!configured.empty() ||
        (std::getenv(environment_name) && std::getenv(environment_name)[0] != '\0')) {
        return resolve_required_file(
            configured, environment_name, sdk_default, label);
    }
    fs::path sdk_path = fs::absolute(executable_path(sdk_default));
    if (is_file(sdk_path)) return sdk_path;
    if (fs::path found = find_on_path(executable_name); !found.empty()) return found;
    host_error(
        std::string("ApplicationHost could not find ") + label +
        " in the SDK or PATH; configure it explicitly or set " +
        environment_name);
}

void create_directory(const fs::path& path, const char* label) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        host_error(
            std::string("ApplicationHost failed to create ") + label +
            " at " + path.string() + ": " + error.message());
    }
}

void set_environment(const char* name, const fs::path& value) {
#ifdef _WIN32
    if (_putenv_s(name, value.string().c_str()) != 0) {
        host_error(std::string("ApplicationHost failed to set ") + name);
    }
#else
    if (setenv(name, value.string().c_str(), 1) != 0) {
        host_error(std::string("ApplicationHost failed to set ") + name);
    }
#endif
}

void resolve_runtime_config(ApplicationHostConfig& config) {
    const fs::path sdk_root = resolve_sdk_root(config);
    config.sdk_root = sdk_root.string();

    const fs::path font = resolve_required_file(
        config.font_path,
        "TERMIN_UI_FONT",
        sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf",
        "UI font");
    config.font_path = font.string();

    if (!config.configure_shader_runtime) return;

    const fs::path shader_compiler = resolve_required_file(
        config.shader_compiler_path,
        "TERMIN_SHADERC",
        sdk_root / "bin" / "termin_shaderc",
        "termin_shaderc");
    const fs::path slang_compiler = resolve_required_tool(
        config.slang_compiler_path,
        "TERMIN_SLANGC",
        sdk_root / "bin" / "slangc",
        "slangc",
        "slangc");

    const fs::path runtime_root = fs::temp_directory_path() / "termin-gui-native-host";
    const fs::path cache_root = config.shader_cache_root.empty()
        ? runtime_root / "shader-cache"
        : fs::path(config.shader_cache_root);
    const fs::path artifact_root = config.shader_artifact_root.empty()
        ? runtime_root / "shader-artifacts"
        : fs::path(config.shader_artifact_root);
    create_directory(cache_root, "shader cache directory");
    create_directory(artifact_root, "shader artifact directory");

    config.shader_compiler_path = fs::absolute(shader_compiler).string();
    config.slang_compiler_path = fs::absolute(slang_compiler).string();
    config.shader_cache_root = fs::absolute(cache_root).string();
    config.shader_artifact_root = fs::absolute(artifact_root).string();

    static std::mutex shader_runtime_mutex;
    const std::lock_guard<std::mutex> lock(shader_runtime_mutex);
    set_environment("TERMIN_SLANGC", slang_compiler);
    tgfx2_set_shader_compiler_path(config.shader_compiler_path.c_str());
    tgfx2_set_shader_cache_root(config.shader_cache_root.c_str());
    tgfx2_set_shader_artifact_root(config.shader_artifact_root.c_str());
    tgfx2_set_shader_dev_compile_enabled(config.enable_shader_dev_compile);
}

struct DrawListDeleter {
    void operator()(tc_ui_draw_list* draw_list) const {
        tc_ui_draw_list_destroy(draw_list);
    }
};

struct PaintContextDeleter {
    void operator()(tc_ui_paint_context* paint_context) const {
        tc_ui_paint_context_destroy(paint_context);
    }
};

tgfx::TextureHandle create_color_target(
    tgfx::IRenderDevice& device,
    int width,
    int height) {
    tgfx::TextureDesc description;
    description.width = static_cast<uint32_t>(width);
    description.height = static_cast<uint32_t>(height);
    description.format = tgfx::PixelFormat::RGBA8_UNorm;
    description.usage = tgfx::TextureUsage::Sampled |
        tgfx::TextureUsage::ColorAttachment |
        tgfx::TextureUsage::CopySrc;
    return device.create_texture(description);
}

} // namespace

struct ApplicationHost::Impl {
    Document* document = nullptr;
    ApplicationHostConfig config;
    BackendWindowPtr window;
    tgfx::IRenderDevice* device = nullptr;
    tgfx::RenderContext2* context = nullptr;
    UiDrawListRenderer renderer;
    std::unique_ptr<tc_ui_draw_list, DrawListDeleter> draw_list;
    std::unique_ptr<tc_ui_paint_context, PaintContextDeleter> paint_context;
    tgfx::TextureHandle color_target{};
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    size_t rendered_frames = 0;
    bool repaint_requested = true;
    std::deque<std::function<void()>> deferred_callbacks;
    std::string clipboard_buffer;

    static const char* clipboard_get(void* user_data) {
        auto& self = *static_cast<Impl*>(user_data);
        self.clipboard_buffer = self.window->clipboard_text();
        return self.clipboard_buffer.c_str();
    }

    static bool clipboard_set(
        void* user_data,
        const char* text,
        size_t byte_length) {
        auto& self = *static_cast<Impl*>(user_data);
        return self.window->set_clipboard_text(
            std::string(text ? text : "", byte_length));
    }

    static void cursor_changed(void* user_data, tc_ui_cursor_intent cursor) {
        auto& self = *static_cast<Impl*>(user_data);
        WindowCursor window_cursor = WindowCursor::Default;
        switch (cursor) {
            case TC_UI_CURSOR_TEXT: window_cursor = WindowCursor::Text; break;
            case TC_UI_CURSOR_HAND: window_cursor = WindowCursor::Hand; break;
            case TC_UI_CURSOR_CROSSHAIR: window_cursor = WindowCursor::Crosshair; break;
            case TC_UI_CURSOR_MOVE: window_cursor = WindowCursor::Move; break;
            case TC_UI_CURSOR_RESIZE_HORIZONTAL:
                window_cursor = WindowCursor::ResizeHorizontal;
                break;
            case TC_UI_CURSOR_RESIZE_VERTICAL:
                window_cursor = WindowCursor::ResizeVertical;
                break;
            case TC_UI_CURSOR_RESIZE_NWSE:
                window_cursor = WindowCursor::ResizeNWSE;
                break;
            case TC_UI_CURSOR_RESIZE_NESW:
                window_cursor = WindowCursor::ResizeNESW;
                break;
            case TC_UI_CURSOR_INHERIT:
            case TC_UI_CURSOR_DEFAULT:
            case TC_UI_CURSOR_INTENT_COUNT:
                break;
        }
        self.window->set_cursor(window_cursor);
        self.repaint_requested = true;
    }

    Impl(
        Document& document_ref,
        ApplicationHostConfig host_config,
        BackendWindowPtr backend_window)
        : document(&document_ref),
          config(std::move(host_config)),
          window(std::move(backend_window)),
          draw_list(tc_ui_draw_list_create()),
          paint_context(tc_ui_paint_context_create(draw_list.get())) {
        resolve_runtime_config(config);
        if (!window) {
            host_error("ApplicationHost requires a non-null BackendWindow");
        }
        device = window->device();
        context = window->context();
        if (!device || !context) {
            host_error("ApplicationHost window has no render device or context");
        }
        if (!draw_list || !paint_context) {
            host_error("ApplicationHost failed to allocate native GUI paint state");
        }
        if (!renderer.set_default_font_path(config.font_path, config.font_size)) {
            host_error("ApplicationHost failed to load UI font: " + config.font_path);
        }
        renderer.bind_text_measurer(document->get());
        document->set_clipboard(&clipboard_get, &clipboard_set, this);
        document->set_cursor_changed_callback(&cursor_changed, this);
        window->set_text_input_enabled(config.enable_text_input);
    }

    ~Impl() {
        if (!window) return;
        window->set_text_input_enabled(false);
        if (device) {
            device->wait_idle();
        }
        renderer.release_gpu();
        if (device && color_target) {
            device->destroy(color_target);
            device->invalidate_render_target_cache();
            color_target = {};
        }
        // Document outlives the host by contract. Remove the callback that
        // points at `renderer` before the renderer is destroyed.
        if (document) {
            document->set_cursor_changed_callback(nullptr, nullptr);
            document->set_clipboard(nullptr, nullptr, nullptr);
            document->set_text_measurer(nullptr, nullptr);
        }
    }

    void ensure_color_target(int width, int height) {
        if (color_target &&
            framebuffer_width == width && framebuffer_height == height) {
            return;
        }
        if (color_target) {
            device->wait_idle();
            device->destroy(color_target);
            device->invalidate_render_target_cache();
            color_target = {};
        }
        color_target = create_color_target(*device, width, height);
        if (!color_target) {
            host_error("ApplicationHost failed to create the GUI color target");
        }
        framebuffer_width = width;
        framebuffer_height = height;
    }
};

ApplicationHost::ApplicationHost(
    Document& document,
    ApplicationHostConfig config)
    : ApplicationHost(
          document,
          config,
          create_native_window(config.window)) {}

ApplicationHost::ApplicationHost(
    Document& document,
    ApplicationHostConfig config,
    BackendWindowPtr window)
    : impl_(std::make_unique<Impl>(
          document,
          std::move(config),
          std::move(window))) {}

ApplicationHost::~ApplicationHost() = default;
ApplicationHost::ApplicationHost(ApplicationHost&&) noexcept = default;
ApplicationHost& ApplicationHost::operator=(ApplicationHost&&) noexcept = default;

BackendWindow& ApplicationHost::window() { return *impl_->window; }
const BackendWindow& ApplicationHost::window() const { return *impl_->window; }

size_t ApplicationHost::pump_events() {
    size_t event_count = 0;
    WindowEvent event;
    while (impl_->window->poll_event(event)) {
        ++event_count;
        if (event.type == WindowEventType::CloseRequested) {
            impl_->window->set_should_close(true);
        }
        dispatch_window_event(*impl_->document, event);
    }
    if (event_count > 0) impl_->repaint_requested = true;
    return event_count;
}

bool ApplicationHost::render_frame() {
    const auto [width, height] = impl_->window->framebuffer_size();
    if (width <= 0 || height <= 0) {
        return false;
    }
    // Consume the request for this frame before application paint/render
    // callbacks run. A request raised while producing the frame therefore
    // remains visible to the next tick instead of being cleared afterwards.
    impl_->repaint_requested = false;
    impl_->ensure_color_target(width, height);

    tc_ui_draw_list_clear(impl_->draw_list.get());
    impl_->document->layout_roots(tc_ui_rect{
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
    impl_->document->paint_roots(impl_->paint_context.get());

    impl_->context->begin_frame();
    impl_->context->begin_pass(
        impl_->color_target,
        tgfx::TextureHandle{},
        impl_->config.clear_color.data(),
        1.0f,
        false);
    impl_->renderer.render(
        *impl_->context,
        impl_->draw_list.get(),
        width,
        height);
    impl_->context->end_pass();
    impl_->context->end_frame();
    impl_->window->present(impl_->color_target);
    ++impl_->rendered_frames;
    return true;
}

bool ApplicationHost::tick() {
    pump_events();
    const size_t deferred_count = impl_->deferred_callbacks.size();
    for (size_t index = 0; index < deferred_count; ++index) {
        auto callback = std::move(impl_->deferred_callbacks.front());
        impl_->deferred_callbacks.pop_front();
        try {
            callback();
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-host] deferred callback failed: %s",
                error.what());
            throw;
        } catch (...) {
            tc_log_error(
                "[gui-native-host] deferred callback failed with an unknown exception");
            throw;
        }
    }
    if (should_close()) {
        return false;
    }
    if (impl_->config.continuous_rendering || impl_->repaint_requested) {
        render_frame();
    }
    if (!impl_->deferred_callbacks.empty()) {
        impl_->repaint_requested = true;
    }
    return true;
}

void ApplicationHost::defer(std::function<void()> callback) {
    if (!callback) {
        tc_log_error("[gui-native-host] cannot defer an empty callback");
        return;
    }
    impl_->deferred_callbacks.push_back(std::move(callback));
    impl_->repaint_requested = true;
}

void ApplicationHost::request_repaint() {
    impl_->repaint_requested = true;
}

bool ApplicationHost::repaint_requested() const {
    return impl_->repaint_requested;
}

bool ApplicationHost::should_close() const {
    return impl_->window->should_close();
}

void ApplicationHost::request_close() {
    impl_->window->set_should_close(true);
}

void ApplicationHost::wait_idle() {
    impl_->device->wait_idle();
}

size_t ApplicationHost::rendered_frame_count() const {
    return impl_->rendered_frames;
}

int ApplicationHost::framebuffer_width() const {
    return impl_->framebuffer_width;
}

int ApplicationHost::framebuffer_height() const {
    return impl_->framebuffer_height;
}

tgfx::TextureHandle ApplicationHost::color_target() const {
    return impl_->color_target;
}

} // namespace termin::gui_native
