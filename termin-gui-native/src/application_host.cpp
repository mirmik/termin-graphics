#include "termin/gui_native/application_host.hpp"
#include "application_host_internal.hpp"

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
#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/window_input.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
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
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&host_module_anchor), &module)) {
        host_error("GUI application host could not locate its loaded DLL");
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
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

fs::path resolve_sdk_root(const std::string& configured_sdk_root) {
    if (!configured_sdk_root.empty()) return fs::absolute(configured_sdk_root);
    if (const char* environment = std::getenv("TERMIN_SDK")) {
        if (environment[0] != '\0') return fs::absolute(environment);
    }
    // Installed layout is <sdk>/lib/lib...so or <sdk>/bin/...dll.
    return module_path().parent_path().parent_path();
}

fs::path resolve_required_file(const std::string& configured, const char* environment_name,
                               const fs::path& sdk_default, const char* label) {
    fs::path path;
    if (!configured.empty()) {
        path = configured;
    } else if (const char* environment = std::getenv(environment_name)) {
        if (environment[0] != '\0') path = environment;
    }
    if (path.empty()) path = sdk_default;
    path = fs::absolute(executable_path(std::move(path)));
    if (!is_file(path)) {
        host_error(std::string("GUI application host could not find ") + label + " at " +
                   path.string() + "; configure it explicitly or set " + environment_name);
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
        const std::string entry =
            paths.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!entry.empty()) {
            const fs::path candidate = fs::path(entry) / executable_path(executable);
            if (is_file(candidate)) return fs::absolute(candidate);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

fs::path resolve_required_tool(const std::string& configured, const char* environment_name,
                               const fs::path& sdk_default, const char* executable_name,
                               const char* label) {
    if (!configured.empty() ||
        (std::getenv(environment_name) && std::getenv(environment_name)[0] != '\0')) {
        return resolve_required_file(configured, environment_name, sdk_default, label);
    }
    fs::path sdk_path = fs::absolute(executable_path(sdk_default));
    if (is_file(sdk_path)) return sdk_path;
    if (fs::path found = find_on_path(executable_name); !found.empty()) return found;
    host_error(std::string("GUI application host could not find ") + label +
               " in the SDK or PATH; configure it explicitly or set " + environment_name);
}

void create_directory(const fs::path& path, const char* label) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        host_error(std::string("GUI application host failed to create ") + label + " at " +
                   path.string() + ": " + error.message());
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

void resolve_gui_application_config(GuiApplicationConfig& config, const fs::path& sdk_root = {}) {
    const fs::path resolved_sdk_root =
        sdk_root.empty() ? module_path().parent_path().parent_path() : sdk_root;
    const fs::path font = resolve_required_file(
        config.font_path, "TERMIN_UI_FONT",
        resolved_sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf", "UI font");
    config.font_path = font.string();
}

void resolve_gui_window_config(GuiWindowConfig& config, const fs::path& sdk_root = {}) {
    GuiApplicationConfig application_config = config.application_config();
    resolve_gui_application_config(application_config, sdk_root);
    config.font_path = std::move(application_config.font_path);
}

termin::ShaderArtifactResolver resolve_composition_graphics_config(
    std::string& configured_sdk_root, std::string& shader_compiler_path,
    std::string& slang_compiler_path, std::string& shader_cache_root,
    std::string& shader_artifact_root, bool enable_shader_dev_compile) {
    const fs::path sdk_root = resolve_sdk_root(configured_sdk_root);
    configured_sdk_root = sdk_root.string();
    const fs::path shader_compiler =
        resolve_required_file(shader_compiler_path, "TERMIN_SHADERC",
                              sdk_root / "bin" / "termin_shaderc", "termin_shaderc");
    const fs::path slang_compiler = resolve_required_tool(
        slang_compiler_path, "TERMIN_SLANGC", sdk_root / "bin" / "slangc", "slangc", "slangc");

    const fs::path runtime_root = fs::temp_directory_path() / "termin-gui-native-host";
    const fs::path cache_root =
        shader_cache_root.empty() ? runtime_root / "shader-cache" : fs::path(shader_cache_root);
    const fs::path artifact_root = shader_artifact_root.empty() ? runtime_root / "shader-artifacts"
                                                                : fs::path(shader_artifact_root);
    create_directory(cache_root, "shader cache directory");
    create_directory(artifact_root, "shader artifact directory");

    shader_compiler_path = fs::absolute(shader_compiler).string();
    slang_compiler_path = fs::absolute(slang_compiler).string();
    shader_cache_root = fs::absolute(cache_root).string();
    shader_artifact_root = fs::absolute(artifact_root).string();

    // termin_shaderc discovers slangc in its child-process environment. This
    // is configured once by the standalone composition root, never by a
    // per-window host.
    static std::mutex standalone_shader_environment_mutex;
    const std::lock_guard<std::mutex> lock(standalone_shader_environment_mutex);
    set_environment("TERMIN_SLANGC", slang_compiler);
    return termin::ShaderArtifactResolver(shader_artifact_root, shader_cache_root,
                                          shader_compiler_path, enable_shader_dev_compile);
}

termin::ShaderArtifactResolver resolve_standalone_config(StandaloneGuiApplicationConfig& config) {
    const termin::ShaderArtifactResolver resolver = resolve_composition_graphics_config(
        config.sdk_root, config.shader_compiler_path, config.slang_compiler_path,
        config.shader_cache_root, config.shader_artifact_root, config.enable_shader_dev_compile);
    resolve_gui_window_config(config.gui, config.sdk_root);
    return resolver;
}

termin::ShaderArtifactResolver resolve_offscreen_config(OffscreenGuiApplicationConfig& config) {
    if (config.width <= 0 || config.height <= 0) {
        host_error("OffscreenGuiApplication requires positive framebuffer dimensions");
    }
    if (config.backend == tgfx::BackendType::OpenGL) {
        host_error("OffscreenGuiApplication does not support OpenGL because it requires a "
                   "window-system context; use Vulkan or D3D11");
    }
    if (config.backend == tgfx::BackendType::Null || !tgfx::backend_is_compiled(config.backend)) {
        host_error(std::string("OffscreenGuiApplication backend is unavailable: ") +
                   tgfx::backend_name(config.backend));
    }
    const termin::ShaderArtifactResolver resolver = resolve_composition_graphics_config(
        config.sdk_root, config.shader_compiler_path, config.slang_compiler_path,
        config.shader_cache_root, config.shader_artifact_root, config.enable_shader_dev_compile);
    resolve_gui_application_config(config.gui, config.sdk_root);
    return resolver;
}

struct DrawListDeleter {
    void operator()(tc_ui_draw_list* draw_list) const { tc_ui_draw_list_destroy(draw_list); }
};

struct PaintContextDeleter {
    void operator()(tc_ui_paint_context* paint_context) const {
        tc_ui_paint_context_destroy(paint_context);
    }
};

tgfx::TextureHandle create_color_target(tgfx::IRenderDevice& device, int width, int height) {
    tgfx::TextureDesc description;
    description.width = static_cast<uint32_t>(width);
    description.height = static_cast<uint32_t>(height);
    description.format = tgfx::PixelFormat::RGBA8_UNorm;
    description.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::ColorAttachment |
                        tgfx::TextureUsage::CopySrc;
    return device.create_texture(description);
}

WindowCursor window_cursor_for(tc_ui_cursor_intent cursor) {
    switch (cursor) {
    case TC_UI_CURSOR_TEXT:
        return WindowCursor::Text;
    case TC_UI_CURSOR_HAND:
        return WindowCursor::Hand;
    case TC_UI_CURSOR_CROSSHAIR:
        return WindowCursor::Crosshair;
    case TC_UI_CURSOR_MOVE:
        return WindowCursor::Move;
    case TC_UI_CURSOR_RESIZE_HORIZONTAL:
        return WindowCursor::ResizeHorizontal;
    case TC_UI_CURSOR_RESIZE_VERTICAL:
        return WindowCursor::ResizeVertical;
    case TC_UI_CURSOR_RESIZE_NWSE:
        return WindowCursor::ResizeNWSE;
    case TC_UI_CURSOR_RESIZE_NESW:
        return WindowCursor::ResizeNESW;
    case TC_UI_CURSOR_INHERIT:
    case TC_UI_CURSOR_DEFAULT:
    case TC_UI_CURSOR_INTENT_COUNT:
        return WindowCursor::Default;
    }
    return WindowCursor::Default;
}

} // namespace

struct QueuedGuiInputSource::Impl {
    mutable std::mutex mutex;
    std::deque<WindowEvent> events;
    std::atomic<bool> close_requested{false};
};

QueuedGuiInputSource::QueuedGuiInputSource() : impl_(std::make_unique<Impl>()) {
}
QueuedGuiInputSource::~QueuedGuiInputSource() = default;

void QueuedGuiInputSource::push_event(WindowEvent event) {
    if (event.type == WindowEventType::CloseRequested) {
        impl_->close_requested.store(true, std::memory_order_release);
    }
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->events.push_back(std::move(event));
}

bool QueuedGuiInputSource::poll_event(WindowEvent& event) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->events.empty()) return false;
    event = std::move(impl_->events.front());
    impl_->events.pop_front();
    return true;
}

bool QueuedGuiInputSource::should_close() const {
    return impl_->close_requested.load(std::memory_order_acquire);
}

void QueuedGuiInputSource::request_close() {
    impl_->close_requested.store(true, std::memory_order_release);
}

struct InMemoryGuiPlatformServices::Impl {
    mutable std::mutex mutex;
    std::string clipboard;
    WindowCursor cursor = WindowCursor::Default;
    bool text_input_enabled = false;
};

InMemoryGuiPlatformServices::InMemoryGuiPlatformServices() : impl_(std::make_unique<Impl>()) {
}
InMemoryGuiPlatformServices::~InMemoryGuiPlatformServices() = default;

bool InMemoryGuiPlatformServices::supports_text_input() const noexcept {
    return true;
}
bool InMemoryGuiPlatformServices::supports_clipboard() const noexcept {
    return true;
}
bool InMemoryGuiPlatformServices::supports_cursor() const noexcept {
    return true;
}

bool InMemoryGuiPlatformServices::set_text_input_enabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->text_input_enabled = enabled;
    return true;
}

std::string InMemoryGuiPlatformServices::clipboard_text() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->clipboard;
}

bool InMemoryGuiPlatformServices::set_clipboard_text(const std::string& text) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->clipboard = text;
    return true;
}

bool InMemoryGuiPlatformServices::set_cursor(WindowCursor cursor) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cursor = cursor;
    return true;
}

bool InMemoryGuiPlatformServices::text_input_enabled() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->text_input_enabled;
}

WindowCursor InMemoryGuiPlatformServices::cursor() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->cursor;
}

struct GuiApplicationHost::Impl {
    GuiApplicationHost* facade = nullptr;
    Document* document = nullptr;
    GuiApplicationConfig config;
    tgfx::GraphicsHost* graphics = nullptr;
    GuiFrameEndpoint* frame_endpoint = nullptr;
    GuiInputSource* input_source = nullptr;
    GuiPlatformServices* platform_services = nullptr;
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
    std::vector<std::unique_ptr<GuiFrameExtension>> extensions;
    std::function<bool(const WindowEvent&)> event_interceptor;
    std::function<void(GuiFrame&)> before_ui_frame;
    std::function<void(GuiFrame&)> after_ui_frame;
    std::vector<tc_widget_handle> color_pickers;
    std::shared_ptr<GuiApplicationHostLeaseState> texture_leases;
    std::thread::id owner_thread = std::this_thread::get_id();
    std::string clipboard_buffer;
    bool closed = false;

    static const char* clipboard_get(void* user_data) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            self.clipboard_buffer = self.platform_services->clipboard_text();
            return self.clipboard_buffer.c_str();
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] clipboard read failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-host] clipboard read failed with unknown exception");
        }
        return nullptr;
    }

    static bool clipboard_set(void* user_data, const char* text, size_t byte_length) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            return self.platform_services->set_clipboard_text(
                std::string(text ? text : "", byte_length));
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] clipboard write failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-host] clipboard write failed with unknown exception");
        }
        return false;
    }

    static void cursor_changed(void* user_data, tc_ui_cursor_intent cursor) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            if (!self.platform_services->set_cursor(window_cursor_for(cursor))) {
                tc_log_error("[gui-native-host] platform cursor service rejected cursor update");
                return;
            }
            self.facade->request_repaint();
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] cursor update failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-host] cursor update failed with unknown exception");
        }
    }

    Impl(GuiApplicationHost& host, tgfx::GraphicsHost& graphics_ref, Document& document_ref,
         GuiApplicationConfig host_config, GuiFrameEndpoint& endpoint, GuiInputSource& input,
         GuiPlatformServices& platform)
        : facade(&host), document(&document_ref), config(std::move(host_config)),
          graphics(&graphics_ref), frame_endpoint(&endpoint), input_source(&input),
          platform_services(&platform), draw_list(tc_ui_draw_list_create()),
          paint_context(tc_ui_paint_context_create(draw_list.get())) {
        resolve_gui_application_config(config);
        if (!document->valid()) {
            host_error("GuiApplicationHost requires a live Document");
        }
        device = &graphics->device();
        context = &graphics->context();
        if (!draw_list || !paint_context) {
            host_error("GuiApplicationHost failed to allocate native GUI paint state");
        }
        if (!platform_services->supports_text_input() || !platform_services->supports_clipboard() ||
            !platform_services->supports_cursor()) {
            host_error("GuiApplicationHost requires text-input, clipboard and cursor "
                       "platform "
                       "capabilities");
        }
        if (!renderer.set_default_font_path(config.font_path, config.font_size)) {
            host_error("GuiApplicationHost failed to load UI font: " + config.font_path);
        }
        document->attach_application_host();
        try {
            if (!platform_services->set_text_input_enabled(config.enable_text_input)) {
                host_error("GuiApplicationHost platform service rejected text-input "
                           "configuration");
            }
            renderer.bind_text_measurer(document->get());
            document->set_clipboard(&clipboard_get, &clipboard_set, this);
            document->set_cursor_changed_callback(&cursor_changed, this);
            texture_leases = std::make_shared<GuiApplicationHostLeaseState>();
            texture_leases->owner_thread = owner_thread;
            texture_leases->host = facade;
            texture_leases->graphics = graphics;
            texture_leases->document = document;
        } catch (...) {
            document->set_cursor_changed_callback(nullptr, nullptr);
            document->set_clipboard(nullptr, nullptr, nullptr);
            document->set_text_measurer(nullptr, nullptr);
            try {
                platform_services->set_text_input_enabled(false);
            } catch (...) {
                tc_log_error("[gui-native-host] failed to roll back text-input configuration");
            }
            document->detach_application_host();
            throw;
        }
    }

    void require_owner_thread(const char* operation) const {
        if (std::this_thread::get_id() == owner_thread) return;
        const std::string message =
            std::string("GuiApplicationHost::") + operation + " requires the owner thread";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void require_open(const char* operation) const {
        if (!closed && frame_endpoint && input_source && platform_services && graphics &&
            !graphics->is_closed() && document && document->valid()) {
            return;
        }
        const std::string message =
            std::string("GuiApplicationHost::") + operation + " called after dependency shutdown";
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
            host_error("GuiApplicationHost failed to create the GUI color target");
        }
        framebuffer_width = width;
        framebuffer_height = height;
    }

    void close() {
        if (closed) return;
        require_owner_thread("close");
        if (!document || !document->valid()) {
            tc_log_error("[gui-native-host] Document was closed before GuiApplicationHost");
            throw std::logic_error("GuiApplicationHost requires Document to outlive it");
        }
        if (!graphics || graphics->is_closed()) {
            tc_log_error("[gui-native-host] GraphicsHost was closed before "
                         "GuiApplicationHost");
            throw std::logic_error("GuiApplicationHost requires GraphicsHost to outlive it");
        }
        {
            const std::lock_guard<std::mutex> lock(deferred_mutex);
            deferred_callbacks.clear();
        }
        for (auto iterator = extensions.rbegin(); iterator != extensions.rend(); ++iterator) {
            (*iterator)->detach(*facade);
        }
        extensions.clear();
        event_interceptor = {};
        before_ui_frame = {};
        after_ui_frame = {};
        color_pickers.clear();
        try {
            if (!platform_services->set_text_input_enabled(false)) {
                tc_log_error("[gui-native-host] platform service rejected text-input shutdown");
            }
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-host] text-input shutdown failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-host] text-input shutdown failed with unknown "
                         "exception");
        }
        document->set_cursor_changed_callback(nullptr, nullptr);
        document->set_clipboard(nullptr, nullptr, nullptr);
        texture_leases->close_all();
        device->wait_idle();
        renderer.release_gpu();
        if (color_target) {
            device->destroy(color_target);
            device->invalidate_render_target_cache();
            color_target = {};
        }
        document->set_text_measurer(nullptr, nullptr);
        document->detach_application_host();
        document = nullptr;
        frame_endpoint = nullptr;
        input_source = nullptr;
        platform_services = nullptr;
        context = nullptr;
        device = nullptr;
        graphics = nullptr;
        closed = true;
    }
};

GuiApplicationHost::GuiApplicationHost(tgfx::GraphicsHost& graphics, Document& document,
                                       GuiApplicationConfig config,
                                       GuiFrameEndpoint& frame_endpoint,
                                       GuiInputSource& input_source,
                                       GuiPlatformServices& platform_services)
    : impl_(std::make_unique<Impl>(*this, graphics, document, std::move(config), frame_endpoint,
                                   input_source, platform_services)) {
}

GuiApplicationHost::~GuiApplicationHost() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-host] application host destructor shutdown failed: %s",
                     error.what());
    } catch (...) {
        tc_log_error("[gui-native-host] application host destructor shutdown failed "
                     "with unknown exception");
    }
}

tgfx::GraphicsHost& GuiApplicationHost::graphics() {
    impl_->require_open("graphics");
    return *impl_->graphics;
}

const tgfx::GraphicsHost& GuiApplicationHost::graphics() const {
    impl_->require_open("graphics");
    return *impl_->graphics;
}

tgfx::IRenderDevice& GuiApplicationHost::device() {
    return graphics().device();
}
const tgfx::IRenderDevice& GuiApplicationHost::device() const {
    return graphics().device();
}

size_t GuiApplicationHost::pump_events() {
    impl_->require_owner_thread("pump_events");
    impl_->require_open("pump_events");
    size_t event_count = 0;
    WindowEvent event;
    while (impl_->input_source->poll_event(event)) {
        ++event_count;
        if (event.type == WindowEventType::CloseRequested) {
            impl_->input_source->request_close();
        }
        bool consumed = false;
        if (impl_->event_interceptor) {
            consumed = impl_->event_interceptor(event);
        }
        if (!consumed) {
            dispatch_window_event(*impl_->document, event);
        }
    }
    if (event_count > 0) request_repaint();
    return event_count;
}

bool GuiApplicationHost::render_frame() {
    impl_->require_owner_thread("render_frame");
    impl_->require_open("render_frame");
    const auto [width, height] = impl_->frame_endpoint->framebuffer_size();
    if (width <= 0 || height <= 0) return false;
    impl_->repaint_requested.store(false, std::memory_order_release);
    impl_->ensure_color_target(width, height);
    GuiFrame frame(*this, width, height);
    impl_->context->begin_frame();
    if (impl_->before_ui_frame) {
        impl_->before_ui_frame(frame);
    }
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

    for (auto iterator = impl_->color_pickers.begin();
         iterator != impl_->color_pickers.end();) {
        tc_widget* widget =
            tc_ui_document_resolve_widget(impl_->document->get(), *iterator);
        auto* picker =
            widget ? dynamic_cast<ColorPicker*>(static_cast<Widget*>(widget->body))
                   : nullptr;
        if (!picker) {
            tc_log_error("[gui-native-host] registered ColorPicker was destroyed "
                         "without host unregistration");
            iterator = impl_->color_pickers.erase(iterator);
            continue;
        }
        impl_->renderer.sync_color_picker_surfaces(*impl_->context, *picker);
        ++iterator;
    }
    tc_ui_draw_list_clear(impl_->draw_list.get());
    impl_->document->layout_roots(
        tc_ui_rect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
    impl_->document->paint(impl_->paint_context.get());
    impl_->context->begin_pass(impl_->color_target, tgfx::TextureHandle{},
                               impl_->config.clear_color.data(), 1.0f, false);
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
    if (impl_->after_ui_frame) {
        impl_->after_ui_frame(frame);
    }
    impl_->frame_endpoint->publish_frame(impl_->color_target);
    ++impl_->rendered_frames;
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        if (!impl_->deferred_callbacks.empty()) {
            impl_->repaint_requested.store(true, std::memory_order_release);
        }
    }
    return true;
}

bool GuiApplicationHost::tick() {
    impl_->require_owner_thread("tick");
    pump_events();
    run_deferred();
    if (should_close()) return false;
    if (continuous_rendering() || repaint_requested()) {
        render_frame();
    }
    return true;
}

size_t GuiApplicationHost::run_deferred() {
    impl_->require_owner_thread("run_deferred");
    impl_->require_open("run_deferred");
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
    return callbacks.size();
}

void GuiApplicationHost::defer(std::function<void()> callback) {
    if (!callback) {
        tc_log_error("[gui-native-host] cannot defer an empty callback");
        throw std::invalid_argument("GuiApplicationHost::defer requires a callback");
    }
    if (!impl_ || impl_->closed) {
        tc_log_error("[gui-native-host] cannot defer work after close");
        throw std::logic_error("GuiApplicationHost::defer called after close");
    }
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        impl_->deferred_callbacks.push_back(std::move(callback));
    }
    request_repaint();
}

void GuiApplicationHost::request_repaint() {
    if (!impl_ || impl_->closed) return;
    impl_->repaint_requested.store(true, std::memory_order_release);
}

bool GuiApplicationHost::repaint_requested() const {
    return impl_ && impl_->repaint_requested.load(std::memory_order_acquire);
}

void GuiApplicationHost::set_event_interceptor(
    std::function<bool(const WindowEvent&)> interceptor) {
    impl_->require_owner_thread("set_event_interceptor");
    impl_->require_open("set_event_interceptor");
    impl_->event_interceptor = std::move(interceptor);
}

void GuiApplicationHost::set_frame_callbacks(
    std::function<void(GuiFrame&)> before_ui_frame,
    std::function<void(GuiFrame&)> after_ui_frame) {
    impl_->require_owner_thread("set_frame_callbacks");
    impl_->require_open("set_frame_callbacks");
    impl_->before_ui_frame = std::move(before_ui_frame);
    impl_->after_ui_frame = std::move(after_ui_frame);
    request_repaint();
}

void GuiApplicationHost::register_color_picker(ColorPicker& picker) {
    impl_->require_owner_thread("register_color_picker");
    impl_->require_open("register_color_picker");
    if (!tc_ui_document_handle_eq(picker.document(), impl_->document->get())) {
        host_error("GuiApplicationHost cannot register a ColorPicker from another Document");
    }
    const tc_widget_handle handle = picker.handle();
    if (std::none_of(impl_->color_pickers.begin(), impl_->color_pickers.end(),
                     [handle](tc_widget_handle candidate) {
                         return tc_widget_handle_eq(candidate, handle);
                     })) {
        impl_->color_pickers.push_back(handle);
        request_repaint();
    }
}

void GuiApplicationHost::unregister_color_picker(ColorPicker& picker) {
    impl_->require_owner_thread("unregister_color_picker");
    impl_->require_open("unregister_color_picker");
    impl_->renderer.release_color_picker_surfaces(picker);
    const tc_widget_handle handle = picker.handle();
    impl_->color_pickers.erase(
        std::remove_if(impl_->color_pickers.begin(), impl_->color_pickers.end(),
                       [handle](tc_widget_handle candidate) {
                           return tc_widget_handle_eq(candidate, handle);
                       }),
        impl_->color_pickers.end());
    request_repaint();
}

GuiFrameExtension&
GuiApplicationHost::install_frame_extension(std::unique_ptr<GuiFrameExtension> extension) {
    impl_->require_owner_thread("install_frame_extension");
    impl_->require_open("install_frame_extension");
    if (!extension) {
        host_error("GuiApplicationHost cannot install a null frame extension");
    }
    auto& result = *extension;
    impl_->extensions.push_back(std::move(extension));
    request_repaint();
    return result;
}

std::unique_ptr<GuiFrameExtension>
GuiApplicationHost::remove_frame_extension(GuiFrameExtension& extension) {
    impl_->require_owner_thread("remove_frame_extension");
    impl_->require_open("remove_frame_extension");
    const auto iterator =
        std::find_if(impl_->extensions.begin(), impl_->extensions.end(),
                     [&extension](const auto& candidate) { return candidate.get() == &extension; });
    if (iterator == impl_->extensions.end()) {
        host_error("GuiApplicationHost does not own the requested frame extension");
    }
    (*iterator)->detach(*this);
    auto result = std::move(*iterator);
    impl_->extensions.erase(iterator);
    return result;
}

void GuiApplicationHost::wait_idle() {
    impl_->require_owner_thread("wait_idle");
    device().wait_idle();
}

bool GuiApplicationHost::should_close() const {
    return !is_open() || impl_->input_source->should_close();
}

void GuiApplicationHost::request_close() {
    impl_->require_owner_thread("request_close");
    impl_->require_open("request_close");
    impl_->input_source->request_close();
}

void GuiApplicationHost::close() {
    if (impl_) impl_->close();
}

bool GuiApplicationHost::is_open() const {
    return impl_ && !impl_->closed;
}

bool GuiApplicationHost::continuous_rendering() const {
    return impl_ && !impl_->closed && impl_->config.continuous_rendering;
}

size_t GuiApplicationHost::rendered_frame_count() const {
    return impl_ ? impl_->rendered_frames : 0;
}

int GuiApplicationHost::framebuffer_width() const {
    return impl_ ? impl_->framebuffer_width : 0;
}

int GuiApplicationHost::framebuffer_height() const {
    return impl_ ? impl_->framebuffer_height : 0;
}

tgfx::TextureHandle GuiApplicationHost::color_target() const {
    return impl_ ? impl_->color_target : tgfx::TextureHandle{};
}

std::shared_ptr<GuiApplicationHostLeaseState> GuiApplicationHost::texture_lease_state() const {
    if (!impl_) {
        tc_log_error("[gui-native-host] texture lease requested from an empty host");
        throw std::logic_error("GuiApplicationHost has no implementation");
    }
    impl_->require_owner_thread("texture_lease_state");
    impl_->require_open("texture_lease_state");
    return impl_->texture_leases;
}

GuiFrame::GuiFrame(GuiApplicationHost& host, int width, int height)
    : host_(&host), width_(width), height_(height) {
}
GuiApplicationHost& GuiFrame::host() const {
    return *host_;
}
tgfx::GraphicsHost& GuiFrame::graphics() const {
    return host_->graphics();
}
tgfx::IRenderDevice& GuiFrame::device() const {
    return host_->device();
}
tgfx::TextureHandle GuiFrame::color_target() const {
    return host_->color_target();
}
int GuiFrame::framebuffer_width() const {
    return width_;
}
int GuiFrame::framebuffer_height() const {
    return height_;
}

namespace {

class BackendWindowFrameEndpoint final : public GuiFrameEndpoint {
  public:
    explicit BackendWindowFrameEndpoint(BackendWindow& window) : window_(&window) {}

    std::pair<int, int> framebuffer_size() const override { return window_->framebuffer_size(); }

    void publish_frame(tgfx::TextureHandle color_texture) override {
        window_->present(color_texture);
    }

  private:
    BackendWindow* window_ = nullptr;
};

class BackendWindowInputSource final : public GuiInputSource {
  public:
    explicit BackendWindowInputSource(BackendWindow& window) : window_(&window) {}

    bool poll_event(WindowEvent& event) override { return window_->poll_event(event); }
    bool should_close() const override { return window_->should_close(); }
    void request_close() override { window_->set_should_close(true); }

  private:
    BackendWindow* window_ = nullptr;
};

class BackendWindowPlatformServices final : public GuiPlatformServices {
  public:
    explicit BackendWindowPlatformServices(BackendWindow& window) : window_(&window) {}

    bool supports_text_input() const noexcept override { return true; }
    bool supports_clipboard() const noexcept override { return true; }
    bool supports_cursor() const noexcept override { return true; }
    bool set_text_input_enabled(bool enabled) override {
        window_->set_text_input_enabled(enabled);
        return true;
    }
    std::string clipboard_text() const override { return window_->clipboard_text(); }
    bool set_clipboard_text(const std::string& text) override {
        return window_->set_clipboard_text(text);
    }
    bool set_cursor(WindowCursor cursor) override {
        window_->set_cursor(cursor);
        return true;
    }

  private:
    BackendWindow* window_ = nullptr;
};

} // namespace

struct GuiWindowHost::Impl {
    Document* document = nullptr;
    tgfx::GraphicsHost* graphics = nullptr;
    BackendWindowPtr window;
    std::unique_ptr<BackendWindowFrameEndpoint> frame_endpoint;
    std::unique_ptr<BackendWindowInputSource> input_source;
    std::unique_ptr<BackendWindowPlatformServices> platform_services;
    std::unique_ptr<GuiApplicationHost> application_host;
    std::thread::id owner_thread = std::this_thread::get_id();
    bool closed = false;

    Impl(tgfx::GraphicsHost& graphics_ref, Document& document_ref, GuiWindowConfig config,
         BackendWindowPtr backend_window)
        : document(&document_ref), graphics(&graphics_ref), window(std::move(backend_window)) {
        if (!window) {
            host_error("GuiWindowHost requires a non-null BackendWindow");
        }
        if (&window->graphics_host() != graphics) {
            host_error("GuiWindowHost rejected a window from another GraphicsHost");
        }
        frame_endpoint = std::make_unique<BackendWindowFrameEndpoint>(*window);
        input_source = std::make_unique<BackendWindowInputSource>(*window);
        platform_services = std::make_unique<BackendWindowPlatformServices>(*window);
        application_host = std::make_unique<GuiApplicationHost>(
            *graphics, *document, config.application_config(), *frame_endpoint, *input_source,
            *platform_services);
    }

    void require_owner_thread(const char* operation) const {
        if (std::this_thread::get_id() == owner_thread) return;
        const std::string message =
            std::string("GuiWindowHost::") + operation + " requires the owner thread";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void require_open(const char* operation) const {
        if (!closed && window && application_host && application_host->is_open()) {
            return;
        }
        const std::string message =
            std::string("GuiWindowHost::") + operation + " called after dependency shutdown";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
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
        application_host->close();
        application_host.reset();
        platform_services.reset();
        input_source.reset();
        frame_endpoint.reset();
        window->close();
        window.reset();
        document = nullptr;
        graphics = nullptr;
        closed = true;
    }
};

GuiWindowHost::GuiWindowHost(WindowedGraphicsSession& graphics_session, Document& document,
                             GuiWindowConfig config)
    : GuiWindowHost(graphics_session.graphics(), document, config,
                    graphics_session.create_window(config.window)) {
}

GuiWindowHost::GuiWindowHost(tgfx::GraphicsHost& graphics, Document& document,
                             GuiWindowConfig config, BackendWindowPtr window)
    : impl_(std::make_unique<Impl>(graphics, document, std::move(config), std::move(window))) {
}

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

BackendWindow& GuiWindowHost::window() {
    impl_->require_open("window");
    return *impl_->window;
}
const BackendWindow& GuiWindowHost::window() const {
    impl_->require_open("window");
    return *impl_->window;
}
GuiApplicationHost& GuiWindowHost::application_host() {
    impl_->require_open("application_host");
    return *impl_->application_host;
}
const GuiApplicationHost& GuiWindowHost::application_host() const {
    impl_->require_open("application_host");
    return *impl_->application_host;
}
tgfx::GraphicsHost& GuiWindowHost::graphics() {
    return application_host().graphics();
}
const tgfx::GraphicsHost& GuiWindowHost::graphics() const {
    return application_host().graphics();
}
tgfx::IRenderDevice& GuiWindowHost::device() {
    return graphics().device();
}
const tgfx::IRenderDevice& GuiWindowHost::device() const {
    return graphics().device();
}

size_t GuiWindowHost::pump_events() {
    return application_host().pump_events();
}

bool GuiWindowHost::render_frame() {
    return application_host().render_frame();
}

bool GuiWindowHost::tick() {
    return application_host().tick();
}

size_t GuiWindowHost::run_deferred() {
    return application_host().run_deferred();
}

void GuiWindowHost::defer(std::function<void()> callback) {
    application_host().defer(std::move(callback));
}

void GuiWindowHost::request_repaint() {
    if (!impl_ || impl_->closed || !impl_->application_host) return;
    impl_->application_host->request_repaint();
}

bool GuiWindowHost::repaint_requested() const {
    return impl_ && !impl_->closed && impl_->application_host &&
           impl_->application_host->repaint_requested();
}

void GuiWindowHost::set_event_interceptor(
    std::function<bool(const WindowEvent&)> interceptor) {
    application_host().set_event_interceptor(std::move(interceptor));
}

void GuiWindowHost::set_frame_callbacks(
    std::function<void(GuiFrame&)> before_ui_frame,
    std::function<void(GuiFrame&)> after_ui_frame) {
    application_host().set_frame_callbacks(std::move(before_ui_frame),
                                           std::move(after_ui_frame));
}

void GuiWindowHost::register_color_picker(ColorPicker& picker) {
    application_host().register_color_picker(picker);
}

void GuiWindowHost::unregister_color_picker(ColorPicker& picker) {
    application_host().unregister_color_picker(picker);
}

GuiWindowFrameExtension&
GuiWindowHost::install_frame_extension(std::unique_ptr<GuiWindowFrameExtension> extension) {
    return application_host().install_frame_extension(std::move(extension));
}

std::unique_ptr<GuiWindowFrameExtension>
GuiWindowHost::remove_frame_extension(GuiWindowFrameExtension& extension) {
    return application_host().remove_frame_extension(extension);
}

bool GuiWindowHost::should_close() const {
    return !is_open() || impl_->application_host->should_close();
}
void GuiWindowHost::request_close() {
    application_host().request_close();
}
void GuiWindowHost::wait_idle() {
    application_host().wait_idle();
}
void GuiWindowHost::close() {
    if (impl_) impl_->close();
}
bool GuiWindowHost::is_open() const {
    return impl_ && !impl_->closed && impl_->application_host && impl_->application_host->is_open();
}
size_t GuiWindowHost::rendered_frame_count() const {
    return impl_ && impl_->application_host ? impl_->application_host->rendered_frame_count() : 0;
}
int GuiWindowHost::framebuffer_width() const {
    return impl_ && impl_->application_host ? impl_->application_host->framebuffer_width() : 0;
}
int GuiWindowHost::framebuffer_height() const {
    return impl_ && impl_->application_host ? impl_->application_host->framebuffer_height() : 0;
}
tgfx::TextureHandle GuiWindowHost::color_target() const {
    return impl_ && impl_->application_host ? impl_->application_host->color_target()
                                            : tgfx::TextureHandle{};
}

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
        window_host = std::make_unique<GuiWindowHost>(*graphics_session, document, config.gui);
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

StandaloneGuiApplication::StandaloneGuiApplication(StandaloneGuiApplicationConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {
}

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
StandaloneGuiApplication&
StandaloneGuiApplication::operator=(StandaloneGuiApplication&&) noexcept = default;
WindowedGraphicsSession& StandaloneGuiApplication::graphics_session() {
    return *impl_->graphics_session;
}
const WindowedGraphicsSession& StandaloneGuiApplication::graphics_session() const {
    return *impl_->graphics_session;
}
Document& StandaloneGuiApplication::document() {
    return impl_->document;
}
const Document& StandaloneGuiApplication::document() const {
    return impl_->document;
}
GuiWindowHost& StandaloneGuiApplication::window_host() {
    return *impl_->window_host;
}
const GuiWindowHost& StandaloneGuiApplication::window_host() const {
    return *impl_->window_host;
}
void StandaloneGuiApplication::close() {
    if (impl_) impl_->close();
}
bool StandaloneGuiApplication::is_open() const {
    return impl_ && !impl_->closed;
}

namespace {

class OffscreenFrameEndpoint final : public GuiFrameEndpoint {
  public:
    OffscreenFrameEndpoint(int width, int height) : width_(width), height_(height) {}

    std::pair<int, int> framebuffer_size() const override { return {width_, height_}; }

    void publish_frame(tgfx::TextureHandle texture) override {
        latest_texture_ = texture;
        published_width_ = width_;
        published_height_ = height_;
        ++generation_;
    }

    void resize(int width, int height) {
        width_ = width;
        height_ = height;
    }

    uint64_t generation() const { return generation_; }
    tgfx::TextureHandle latest_texture() const { return latest_texture_; }
    std::pair<int, int> latest_frame_size() const { return {published_width_, published_height_}; }
    void clear_published_texture() {
        latest_texture_ = {};
        published_width_ = 0;
        published_height_ = 0;
    }

  private:
    int width_ = 0;
    int height_ = 0;
    uint64_t generation_ = 0;
    int published_width_ = 0;
    int published_height_ = 0;
    tgfx::TextureHandle latest_texture_{};
};

} // namespace

struct OffscreenGuiApplication::Impl {
    OffscreenGuiApplicationConfig config;
    std::unique_ptr<tgfx::GraphicsHost> graphics;
    Document document;
    QueuedGuiInputSource input_source;
    InMemoryGuiPlatformServices platform_services;
    std::unique_ptr<OffscreenFrameEndpoint> frame_endpoint;
    std::unique_ptr<GuiApplicationHost> application_host;
    std::thread::id owner_thread = std::this_thread::get_id();
    bool closed = false;

    explicit Impl(OffscreenGuiApplicationConfig application_config)
        : config(std::move(application_config)) {
        const termin::ShaderArtifactResolver resolver = resolve_offscreen_config(config);
        graphics = tgfx::GraphicsHost::create_isolated(config.backend);
        graphics->configure_shader_artifacts(resolver);
        frame_endpoint = std::make_unique<OffscreenFrameEndpoint>(config.width, config.height);
        application_host = std::make_unique<GuiApplicationHost>(
            *graphics, document, config.gui, *frame_endpoint, input_source, platform_services);
    }

    void require_owner_thread(const char* operation) const {
        if (std::this_thread::get_id() == owner_thread) return;
        const std::string message =
            std::string("OffscreenGuiApplication::") + operation + " requires the owner thread";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void require_open(const char* operation) const {
        if (!closed && graphics && !graphics->is_closed() && document.valid() && application_host &&
            application_host->is_open()) {
            return;
        }
        const std::string message =
            std::string("OffscreenGuiApplication::") + operation + " called after close";
        tc_log_error("[gui-native-host] %s", message.c_str());
        throw std::logic_error(message);
    }

    void prepare_frame() {
        if (frame_endpoint->latest_texture() &&
            frame_endpoint->latest_frame_size() != frame_endpoint->framebuffer_size()) {
            // GuiApplicationHost destroys the old-sized color target before
            // recording its replacement. Stop publishing that handle first so a
            // failed replacement frame cannot leave a stale texture observable.
            frame_endpoint->clear_published_texture();
        }
    }

    void close() {
        if (closed) return;
        require_owner_thread("close");
        if (application_host) {
            application_host->close();
            application_host.reset();
        }
        if (frame_endpoint) frame_endpoint->clear_published_texture();
        document.close();
        if (graphics) {
            graphics->close();
            graphics.reset();
        }
        closed = true;
    }
};

OffscreenGuiApplication::OffscreenGuiApplication(OffscreenGuiApplicationConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {
}

OffscreenGuiApplication::~OffscreenGuiApplication() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-host] offscreen shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-host] offscreen shutdown failed with unknown exception");
    }
}

OffscreenGuiApplication::OffscreenGuiApplication(OffscreenGuiApplication&&) noexcept = default;
OffscreenGuiApplication&
OffscreenGuiApplication::operator=(OffscreenGuiApplication&&) noexcept = default;

tgfx::GraphicsHost& OffscreenGuiApplication::graphics() {
    impl_->require_open("graphics");
    return *impl_->graphics;
}
const tgfx::GraphicsHost& OffscreenGuiApplication::graphics() const {
    impl_->require_open("graphics");
    return *impl_->graphics;
}
Document& OffscreenGuiApplication::document() {
    impl_->require_open("document");
    return impl_->document;
}
const Document& OffscreenGuiApplication::document() const {
    impl_->require_open("document");
    return impl_->document;
}
GuiApplicationHost& OffscreenGuiApplication::application_host() {
    impl_->require_open("application_host");
    return *impl_->application_host;
}
const GuiApplicationHost& OffscreenGuiApplication::application_host() const {
    impl_->require_open("application_host");
    return *impl_->application_host;
}
QueuedGuiInputSource& OffscreenGuiApplication::input_source() {
    impl_->require_open("input_source");
    return impl_->input_source;
}
InMemoryGuiPlatformServices& OffscreenGuiApplication::platform_services() {
    impl_->require_open("platform_services");
    return impl_->platform_services;
}

void OffscreenGuiApplication::push_event(WindowEvent event) {
    input_source().push_event(std::move(event));
}
size_t OffscreenGuiApplication::pump_events() {
    return application_host().pump_events();
}
bool OffscreenGuiApplication::render_frame() {
    impl_->require_open("render_frame");
    impl_->prepare_frame();
    return impl_->application_host->render_frame();
}
bool OffscreenGuiApplication::tick() {
    impl_->require_open("tick");
    impl_->prepare_frame();
    return impl_->application_host->tick();
}

void OffscreenGuiApplication::resize(int width, int height) {
    impl_->require_owner_thread("resize");
    impl_->require_open("resize");
    if (width <= 0 || height <= 0) {
        host_error("OffscreenGuiApplication::resize requires positive dimensions");
    }
    impl_->frame_endpoint->resize(width, height);
    impl_->application_host->request_repaint();
}

std::pair<int, int> OffscreenGuiApplication::framebuffer_size() const {
    impl_->require_open("framebuffer_size");
    return impl_->frame_endpoint->framebuffer_size();
}

uint64_t OffscreenGuiApplication::frame_generation() const {
    impl_->require_open("frame_generation");
    return impl_->frame_endpoint->generation();
}

tgfx::TextureHandle OffscreenGuiApplication::latest_frame_texture() const {
    impl_->require_open("latest_frame_texture");
    return impl_->frame_endpoint->latest_texture();
}

std::pair<int, int> OffscreenGuiApplication::latest_frame_size() const {
    impl_->require_open("latest_frame_size");
    return impl_->frame_endpoint->latest_frame_size();
}

std::vector<float> OffscreenGuiApplication::read_frame_rgba_float() {
    impl_->require_owner_thread("read_frame_rgba_float");
    impl_->require_open("read_frame_rgba_float");
    const tgfx::TextureHandle texture = impl_->frame_endpoint->latest_texture();
    if (!texture) {
        host_error("OffscreenGuiApplication has no published frame to read");
    }
    const auto [width, height] = impl_->frame_endpoint->latest_frame_size();
    std::vector<float> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    impl_->application_host->wait_idle();
    if (!impl_->graphics->device().read_texture_rgba_float(texture, pixels.data())) {
        host_error("OffscreenGuiApplication failed to read the published frame");
    }
    return pixels;
}

void OffscreenGuiApplication::request_repaint() {
    application_host().request_repaint();
}
bool OffscreenGuiApplication::repaint_requested() const {
    return application_host().repaint_requested();
}
bool OffscreenGuiApplication::should_close() const {
    return !is_open() || impl_->application_host->should_close();
}
void OffscreenGuiApplication::request_close() {
    application_host().request_close();
}
void OffscreenGuiApplication::wait_idle() {
    application_host().wait_idle();
}
void OffscreenGuiApplication::close() {
    if (impl_) impl_->close();
}
bool OffscreenGuiApplication::is_open() const {
    return impl_ && !impl_->closed;
}

} // namespace termin::gui_native
