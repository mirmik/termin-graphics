#include "termin/gui_native/application_host.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

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
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/shader_artifact_resolver.hpp>

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
        host_error("GUI application host could not locate its loaded DLL");
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        host_error("GUI application host could not read its loaded DLL path");
    }
    buffer.resize(length);
    return fs::path(buffer);
#else
    Dl_info info{};
    if (dladdr(&host_module_anchor, &info) == 0 || !info.dli_fname) {
        host_error("GUI application host could not locate its loaded shared library");
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

fs::path resolve_sdk_root(const StandaloneGuiApplicationConfig& config) {
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
            std::string("GUI application host could not find ") + label +
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
        std::string("GUI application host could not find ") + label +
        " in the SDK or PATH; configure it explicitly or set " +
        environment_name);
}

void create_directory(const fs::path& path, const char* label) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        host_error(
            std::string("GUI application host failed to create ") + label +
            " at " + path.string() + ": " + error.message());
    }
}

void set_environment(const char* name, const fs::path& value) {
#ifdef _WIN32
    if (_putenv_s(name, value.string().c_str()) != 0) {
        host_error(std::string("GUI application host failed to set ") + name);
    }
#else
    if (setenv(name, value.string().c_str(), 1) != 0) {
        host_error(std::string("GUI application host failed to set ") + name);
    }
#endif
}

void resolve_gui_window_config(
    GuiWindowConfig& config,
    const fs::path& sdk_root = {}) {
    const fs::path resolved_sdk_root = sdk_root.empty()
        ? module_path().parent_path().parent_path()
        : sdk_root;
    const fs::path font = resolve_required_file(
        config.font_path,
        "TERMIN_UI_FONT",
        resolved_sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf",
        "UI font");
    config.font_path = font.string();
}

termin::ShaderArtifactResolver resolve_standalone_config(
    StandaloneGuiApplicationConfig& config) {
    const fs::path sdk_root = resolve_sdk_root(config);
    config.sdk_root = sdk_root.string();
    resolve_gui_window_config(config.gui, sdk_root);

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

    // termin_shaderc discovers slangc in its child-process environment. This
    // is configured once by the standalone composition root, never by a
    // per-window host.
    static std::mutex standalone_shader_environment_mutex;
    const std::lock_guard<std::mutex> lock(standalone_shader_environment_mutex);
    set_environment("TERMIN_SLANGC", slang_compiler);
    return termin::ShaderArtifactResolver(
        config.shader_artifact_root,
        config.shader_cache_root,
        config.shader_compiler_path,
        config.enable_shader_dev_compile);
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

struct GuiWindowHost::Impl {
    GuiWindowHost* facade = nullptr;
    Document* document = nullptr;
    GuiWindowConfig config;
    tgfx::GraphicsHost* graphics = nullptr;
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
    std::atomic<bool> repaint_requested{true};
    std::mutex deferred_mutex;
    std::deque<std::function<void()>> deferred_callbacks;
    std::vector<std::unique_ptr<GuiWindowFrameExtension>> extensions;
    std::thread::id owner_thread = std::this_thread::get_id();
    std::string clipboard_buffer;
    bool closed = false;

    static const char* clipboard_get(void* user_data) {
        auto& self = *static_cast<Impl*>(user_data);
        self.clipboard_buffer = self.window->clipboard_text();
        return self.clipboard_buffer.c_str();
    }

    static bool clipboard_set(void* user_data, const char* text, size_t byte_length) {
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
        self.repaint_requested.store(true, std::memory_order_release);
    }

    Impl(
        GuiWindowHost& host,
        tgfx::GraphicsHost& graphics_ref,
        Document& document_ref,
        GuiWindowConfig host_config,
        BackendWindowPtr backend_window)
        : facade(&host),
          document(&document_ref),
          config(std::move(host_config)),
          graphics(&graphics_ref),
          window(std::move(backend_window)),
          draw_list(tc_ui_draw_list_create()),
          paint_context(tc_ui_paint_context_create(draw_list.get())) {
        resolve_gui_window_config(config);
        if (!document->valid()) {
            host_error("GuiWindowHost requires a live Document");
        }
        if (!window) {
            host_error("GuiWindowHost requires a non-null BackendWindow");
        }
        if (&window->graphics_host() != graphics) {
            host_error("GuiWindowHost rejected a window from another GraphicsHost");
        }
        device = &graphics->device();
        context = &graphics->context();
        if (!draw_list || !paint_context) {
            host_error("GuiWindowHost failed to allocate native GUI paint state");
        }
        if (!renderer.set_default_font_path(config.font_path, config.font_size)) {
            host_error("GuiWindowHost failed to load UI font: " + config.font_path);
        }
        window->set_text_input_enabled(config.enable_text_input);
        document->attach_window_host();
        renderer.bind_text_measurer(document->get());
        document->set_clipboard(&clipboard_get, &clipboard_set, this);
        document->set_cursor_changed_callback(&cursor_changed, this);
    }

    void require_owner_thread(const char* operation) const {
        if (std::this_thread::get_id() == owner_thread) return;
        const std::string message =
            std::string("GuiWindowHost::") + operation + " requires the owner thread";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void require_open(const char* operation) const {
        if (!closed && window && graphics && !graphics->is_closed() &&
            document && document->valid()) {
            return;
        }
        const std::string message =
            std::string("GuiWindowHost::") + operation + " called after dependency shutdown";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void ensure_color_target(int width, int height) {
        if (color_target && framebuffer_width == width && framebuffer_height == height) {
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
            host_error("GuiWindowHost failed to create the GUI color target");
        }
        framebuffer_width = width;
        framebuffer_height = height;
    }

    void close() {
        if (closed) return;
        require_owner_thread("close");
        if (!document || !document->valid()) {
            tc_log_error("[gui-native-host] Document was closed before GuiWindowHost");
            throw std::logic_error("GuiWindowHost requires Document to outlive it");
        }
        if (!graphics || graphics->is_closed()) {
            tc_log_error("[gui-native-host] GraphicsHost was closed before GuiWindowHost");
            throw std::logic_error("GuiWindowHost requires GraphicsHost to outlive it");
        }
        {
            const std::lock_guard<std::mutex> lock(deferred_mutex);
            deferred_callbacks.clear();
        }
        for (auto iterator = extensions.rbegin(); iterator != extensions.rend(); ++iterator) {
            (*iterator)->detach(*facade);
        }
        extensions.clear();
        window->set_text_input_enabled(false);
        device->wait_idle();
        renderer.release_gpu();
        if (color_target) {
            device->destroy(color_target);
            device->invalidate_render_target_cache();
            color_target = {};
        }
        document->set_cursor_changed_callback(nullptr, nullptr);
        document->set_clipboard(nullptr, nullptr, nullptr);
        document->set_text_measurer(nullptr, nullptr);
        document->detach_window_host();
        window->close();
        window.reset();
        document = nullptr;
        context = nullptr;
        device = nullptr;
        graphics = nullptr;
        closed = true;
    }
};

GuiWindowHost::GuiWindowHost(
    WindowedGraphicsSession& graphics_session,
    Document& document,
    GuiWindowConfig config)
    : GuiWindowHost(
          graphics_session.graphics(),
          document,
          config,
          graphics_session.create_window(config.window)) {}

GuiWindowHost::GuiWindowHost(
    tgfx::GraphicsHost& graphics,
    Document& document,
    GuiWindowConfig config,
    BackendWindowPtr window)
    : impl_(std::make_unique<Impl>(
          *this, graphics, document, std::move(config), std::move(window))) {}

GuiWindowHost::~GuiWindowHost() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-host] destructor shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-host] destructor shutdown failed with unknown exception");
    }
}

BackendWindow& GuiWindowHost::window() { impl_->require_open("window"); return *impl_->window; }
const BackendWindow& GuiWindowHost::window() const { impl_->require_open("window"); return *impl_->window; }
tgfx::GraphicsHost& GuiWindowHost::graphics() { impl_->require_open("graphics"); return *impl_->graphics; }
const tgfx::GraphicsHost& GuiWindowHost::graphics() const { impl_->require_open("graphics"); return *impl_->graphics; }
tgfx::IRenderDevice& GuiWindowHost::device() { return graphics().device(); }
const tgfx::IRenderDevice& GuiWindowHost::device() const { return graphics().device(); }

size_t GuiWindowHost::pump_events() {
    impl_->require_owner_thread("pump_events");
    impl_->require_open("pump_events");
    size_t event_count = 0;
    WindowEvent event;
    while (impl_->window->poll_event(event)) {
        ++event_count;
        if (event.type == WindowEventType::CloseRequested) {
            impl_->window->set_should_close(true);
        }
        dispatch_window_event(*impl_->document, event);
    }
    if (event_count > 0) request_repaint();
    return event_count;
}

bool GuiWindowHost::render_frame() {
    impl_->require_owner_thread("render_frame");
    impl_->require_open("render_frame");
    const auto [width, height] = impl_->window->framebuffer_size();
    if (width <= 0 || height <= 0) return false;
    impl_->repaint_requested.store(false, std::memory_order_release);
    impl_->ensure_color_target(width, height);
    GuiWindowFrame frame(*this, width, height);
    for (const auto& extension : impl_->extensions) {
        try {
            extension->before_ui_frame(frame);
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] before_ui_frame failed: %s", error.what());
            throw;
        } catch (...) {
            tc_log_error("[gui-native-host] before_ui_frame failed with unknown exception");
            throw;
        }
    }

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
    impl_->renderer.render(*impl_->context, impl_->draw_list.get(), width, height);
    impl_->context->end_pass();
    impl_->context->end_frame();

    for (const auto& extension : impl_->extensions) {
        try {
            extension->after_ui_frame(frame);
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] after_ui_frame failed: %s", error.what());
            throw;
        } catch (...) {
            tc_log_error("[gui-native-host] after_ui_frame failed with unknown exception");
            throw;
        }
    }
    impl_->window->present(impl_->color_target);
    ++impl_->rendered_frames;
    return true;
}

bool GuiWindowHost::tick() {
    impl_->require_owner_thread("tick");
    pump_events();
    std::deque<std::function<void()>> callbacks;
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        callbacks.swap(impl_->deferred_callbacks);
    }
    for (auto& callback : callbacks) {
        try {
            callback();
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] deferred callback failed: %s", error.what());
            throw;
        } catch (...) {
            tc_log_error("[gui-native-host] deferred callback failed with unknown exception");
            throw;
        }
    }
    if (should_close()) return false;
    if (impl_->config.continuous_rendering || repaint_requested()) render_frame();
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        if (!impl_->deferred_callbacks.empty()) request_repaint();
    }
    return true;
}

void GuiWindowHost::defer(std::function<void()> callback) {
    if (!callback) {
        tc_log_error("[gui-native-host] cannot defer an empty callback");
        throw std::invalid_argument("GuiWindowHost::defer requires a callback");
    }
    if (!impl_ || impl_->closed) {
        tc_log_error("[gui-native-host] cannot defer work after close");
        throw std::logic_error("GuiWindowHost::defer called after close");
    }
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        impl_->deferred_callbacks.push_back(std::move(callback));
    }
    request_repaint();
}

void GuiWindowHost::request_repaint() {
    if (!impl_ || impl_->closed) return;
    impl_->repaint_requested.store(true, std::memory_order_release);
}

bool GuiWindowHost::repaint_requested() const {
    return impl_ && impl_->repaint_requested.load(std::memory_order_acquire);
}

GuiWindowFrameExtension& GuiWindowHost::install_frame_extension(
    std::unique_ptr<GuiWindowFrameExtension> extension) {
    impl_->require_owner_thread("install_frame_extension");
    impl_->require_open("install_frame_extension");
    if (!extension) {
        host_error("GuiWindowHost cannot install a null frame extension");
    }
    auto& result = *extension;
    impl_->extensions.push_back(std::move(extension));
    request_repaint();
    return result;
}

std::unique_ptr<GuiWindowFrameExtension> GuiWindowHost::remove_frame_extension(
    GuiWindowFrameExtension& extension) {
    impl_->require_owner_thread("remove_frame_extension");
    impl_->require_open("remove_frame_extension");
    const auto iterator = std::find_if(
        impl_->extensions.begin(), impl_->extensions.end(),
        [&extension](const auto& candidate) { return candidate.get() == &extension; });
    if (iterator == impl_->extensions.end()) {
        host_error("GuiWindowHost does not own the requested frame extension");
    }
    (*iterator)->detach(*this);
    auto result = std::move(*iterator);
    impl_->extensions.erase(iterator);
    return result;
}

bool GuiWindowHost::should_close() const { return !is_open() || impl_->window->should_close(); }
void GuiWindowHost::request_close() { impl_->require_open("request_close"); impl_->window->set_should_close(true); }
void GuiWindowHost::wait_idle() { impl_->require_owner_thread("wait_idle"); device().wait_idle(); }
void GuiWindowHost::close() { if (impl_) impl_->close(); }
bool GuiWindowHost::is_open() const { return impl_ && !impl_->closed; }
size_t GuiWindowHost::rendered_frame_count() const { return impl_ ? impl_->rendered_frames : 0; }
int GuiWindowHost::framebuffer_width() const { return impl_ ? impl_->framebuffer_width : 0; }
int GuiWindowHost::framebuffer_height() const { return impl_ ? impl_->framebuffer_height : 0; }
tgfx::TextureHandle GuiWindowHost::color_target() const { return impl_ ? impl_->color_target : tgfx::TextureHandle{}; }

GuiWindowFrame::GuiWindowFrame(GuiWindowHost& host, int width, int height)
    : host_(&host), width_(width), height_(height) {}
GuiWindowHost& GuiWindowFrame::host() const { return *host_; }
tgfx::GraphicsHost& GuiWindowFrame::graphics() const { return host_->graphics(); }
tgfx::IRenderDevice& GuiWindowFrame::device() const { return host_->device(); }
tgfx::TextureHandle GuiWindowFrame::color_target() const { return host_->color_target(); }
int GuiWindowFrame::framebuffer_width() const { return width_; }
int GuiWindowFrame::framebuffer_height() const { return height_; }

struct StandaloneGuiApplication::Impl {
    StandaloneGuiApplicationConfig config;
    std::unique_ptr<WindowedGraphicsSession> graphics_session;
    Document document;
    std::unique_ptr<GuiWindowHost> window_host;
    bool closed = false;

    explicit Impl(StandaloneGuiApplicationConfig application_config)
        : config(std::move(application_config)) {
        const termin::ShaderArtifactResolver resolver = resolve_standalone_config(config);
        graphics_session = create_native_windowed_graphics();
        graphics_session->graphics().configure_shader_artifacts(resolver);
        window_host = std::make_unique<GuiWindowHost>(
            *graphics_session, document, config.gui);
    }

    void close() {
        if (closed) return;
        if (window_host) {
            window_host->close();
            window_host.reset();
        }
        document.close();
        if (graphics_session) {
            graphics_session->close();
            graphics_session.reset();
        }
        closed = true;
    }
};

StandaloneGuiApplication::StandaloneGuiApplication(
    StandaloneGuiApplicationConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

StandaloneGuiApplication::~StandaloneGuiApplication() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-host] standalone shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-host] standalone shutdown failed with unknown exception");
    }
}

StandaloneGuiApplication::StandaloneGuiApplication(StandaloneGuiApplication&&) noexcept = default;
StandaloneGuiApplication& StandaloneGuiApplication::operator=(StandaloneGuiApplication&&) noexcept = default;
WindowedGraphicsSession& StandaloneGuiApplication::graphics_session() { return *impl_->graphics_session; }
const WindowedGraphicsSession& StandaloneGuiApplication::graphics_session() const { return *impl_->graphics_session; }
Document& StandaloneGuiApplication::document() { return impl_->document; }
const Document& StandaloneGuiApplication::document() const { return impl_->document; }
GuiWindowHost& StandaloneGuiApplication::window_host() { return *impl_->window_host; }
const GuiWindowHost& StandaloneGuiApplication::window_host() const { return *impl_->window_host; }
void StandaloneGuiApplication::close() { if (impl_) impl_->close(); }
bool StandaloneGuiApplication::is_open() const { return impl_ && !impl_->closed; }

} // namespace termin::gui_native
