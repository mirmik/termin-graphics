#include <termin/gui_native/offscreen_composition.hpp>

#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <tcbase/tc_log.h>

#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/shader_artifact_resolver.hpp>

namespace termin::gui_native {

namespace {

namespace fs = std::filesystem;

int offscreen_module_anchor = 0;

[[noreturn]] void offscreen_error(const std::string& message) {
    tc_log_error("[gui-native-offscreen] %s", message.c_str());
    throw std::runtime_error(message);
}

fs::path module_path() {
#ifdef _WIN32
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&offscreen_module_anchor), &module)) {
        offscreen_error("could not locate the loaded offscreen library");
    }
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        offscreen_error("could not read the loaded offscreen library path");
    }
    buffer.resize(length);
    return fs::path(buffer);
#else
    Dl_info info{};
    if (dladdr(&offscreen_module_anchor, &info) == 0 || !info.dli_fname) {
        offscreen_error("could not locate the loaded offscreen library");
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

bool is_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

fs::path resolve_sdk_root(const std::string& configured) {
    if (!configured.empty()) return fs::absolute(configured);
    if (const char* environment = std::getenv("TERMIN_SDK");
        environment && environment[0] != '\0') {
        return fs::absolute(environment);
    }
    return module_path().parent_path().parent_path();
}

fs::path resolve_file(
    const std::string& configured, const char* environment_name,
    const fs::path& fallback, const char* label) {
    fs::path result;
    if (!configured.empty()) {
        result = configured;
    } else if (const char* environment = std::getenv(environment_name);
               environment && environment[0] != '\0') {
        result = environment;
    } else {
        result = fallback;
    }
    result = fs::absolute(executable_path(std::move(result)));
    if (!is_file(result)) {
        offscreen_error(
            std::string("could not find ") + label + " at " + result.string());
    }
    return result;
}

fs::path find_on_path(const char* executable) {
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
        const fs::path directory = paths.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (!directory.empty()) {
            const fs::path candidate =
                directory / executable_path(executable);
            if (is_file(candidate)) return fs::absolute(candidate);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return {};
}

void create_directory(const fs::path& path, const char* label) {
    std::error_code error;
    fs::create_directories(path, error);
    if (error) {
        offscreen_error(
            std::string("could not create ") + label + " at " +
            path.string() + ": " + error.message());
    }
}

void set_slang_compiler(const fs::path& path) {
#ifdef _WIN32
    if (_putenv_s("TERMIN_SLANGC", path.string().c_str()) != 0) {
        offscreen_error("could not configure TERMIN_SLANGC");
    }
#else
    if (setenv("TERMIN_SLANGC", path.string().c_str(), 1) != 0) {
        offscreen_error("could not configure TERMIN_SLANGC");
    }
#endif
}

termin::ShaderArtifactResolver resolve_config(
    OffscreenGuiCompositionConfig& config) {
    if (config.width <= 0 || config.height <= 0) {
        offscreen_error("framebuffer dimensions must be positive");
    }
    if (config.backend == tgfx::BackendType::OpenGL) {
        offscreen_error(
            "OpenGL requires a window-system context; use Vulkan or D3D11");
    }
    if (config.backend == tgfx::BackendType::Null ||
        !tgfx::backend_is_compiled(config.backend)) {
        offscreen_error(
            std::string("backend is unavailable: ") +
            tgfx::backend_name(config.backend));
    }

    const fs::path sdk_root = resolve_sdk_root(config.sdk_root);
    config.sdk_root = sdk_root.string();
    config.renderer.font_path = resolve_file(
        config.renderer.font_path, "TERMIN_UI_FONT",
        sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf",
        "UI font").string();
    config.shader_compiler_path = resolve_file(
        config.shader_compiler_path, "TERMIN_SHADERC",
        sdk_root / "bin" / "termin_shaderc", "termin_shaderc").string();
    if (config.slang_compiler_path.empty() &&
        (!std::getenv("TERMIN_SLANGC") ||
         std::getenv("TERMIN_SLANGC")[0] == '\0') &&
        !is_file(executable_path(sdk_root / "bin" / "slangc"))) {
        config.slang_compiler_path = find_on_path("slangc").string();
    }
    config.slang_compiler_path = resolve_file(
        config.slang_compiler_path, "TERMIN_SLANGC",
        sdk_root / "bin" / "slangc", "slangc").string();

    const fs::path runtime_root =
        fs::temp_directory_path() / "termin-gui-native-offscreen";
    const fs::path cache_root = config.shader_cache_root.empty()
        ? runtime_root / "shader-cache"
        : fs::path(config.shader_cache_root);
    const fs::path artifact_root = config.shader_artifact_root.empty()
        ? runtime_root / "shader-artifacts"
        : fs::path(config.shader_artifact_root);
    create_directory(cache_root, "shader cache");
    create_directory(artifact_root, "shader artifact directory");
    config.shader_cache_root = fs::absolute(cache_root).string();
    config.shader_artifact_root = fs::absolute(artifact_root).string();
    static std::mutex shader_environment_mutex;
    const std::lock_guard<std::mutex> lock(shader_environment_mutex);
    set_slang_compiler(config.slang_compiler_path);
    return termin::ShaderArtifactResolver(
        config.shader_artifact_root, config.shader_cache_root,
        config.shader_compiler_path, config.enable_shader_dev_compile);
}

class OffscreenFrameSink final : public DocumentFrameSink {
  public:
    OffscreenFrameSink(int width, int height) : width_(width), height_(height) {}

    std::pair<int, int> framebuffer_size() const override {
        return {width_, height_};
    }

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

    void clear_published_texture() {
        latest_texture_ = {};
        published_width_ = 0;
        published_height_ = 0;
    }

    uint64_t generation() const { return generation_; }
    tgfx::TextureHandle latest_texture() const { return latest_texture_; }
    std::pair<int, int> latest_frame_size() const {
        return {published_width_, published_height_};
    }

  private:
    int width_;
    int height_;
    uint64_t generation_ = 0;
    int published_width_ = 0;
    int published_height_ = 0;
    tgfx::TextureHandle latest_texture_{};
};

struct PointerInput {
    tc_ui_pointer_event event;
};
struct KeyInput {
    tc_ui_key_event event;
};
struct TextInput {
    std::string utf8;
};
using QueuedInput = std::variant<PointerInput, KeyInput, TextInput>;

} // namespace

struct InMemoryDocumentPlatformServices::Impl {
    mutable std::mutex mutex;
    std::string clipboard;
    tc_ui_cursor_intent cursor = TC_UI_CURSOR_DEFAULT;
    bool text_input_enabled = false;
};

InMemoryDocumentPlatformServices::InMemoryDocumentPlatformServices()
    : impl_(std::make_unique<Impl>()) {}
InMemoryDocumentPlatformServices::~InMemoryDocumentPlatformServices() = default;

bool InMemoryDocumentPlatformServices::set_text_input_enabled(bool enabled) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->text_input_enabled = enabled;
    return true;
}

std::string InMemoryDocumentPlatformServices::clipboard_text() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->clipboard;
}

bool InMemoryDocumentPlatformServices::set_clipboard_text(
    const std::string& text) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->clipboard = text;
    return true;
}

bool InMemoryDocumentPlatformServices::set_cursor(tc_ui_cursor_intent cursor) {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cursor = cursor;
    return true;
}

bool InMemoryDocumentPlatformServices::text_input_enabled() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->text_input_enabled;
}

tc_ui_cursor_intent InMemoryDocumentPlatformServices::cursor() const {
    const std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->cursor;
}

struct OffscreenGuiComposition::Impl {
    OffscreenGuiCompositionConfig config;
    std::unique_ptr<tgfx::GraphicsHost> graphics;
    tc_ui_document_handle document_handle = tc_ui_document_handle_invalid();
    TcDocument document;
    InMemoryDocumentPlatformServices platform;
    std::unique_ptr<OffscreenFrameSink> sink;
    std::unique_ptr<DocumentRenderer> renderer;
    std::mutex input_mutex;
    std::deque<QueuedInput> input_queue;
    std::atomic<bool> accepting_input{true};
    std::atomic<bool> close_requested{false};
    bool closed = false;

    explicit Impl(OffscreenGuiCompositionConfig composition_config)
        : config(std::move(composition_config)) {
        const termin::ShaderArtifactResolver resolver = resolve_config(config);
        graphics = tgfx::GraphicsHost::create_isolated(config.backend);
        graphics->configure_shader_artifacts(resolver);
        sink = std::make_unique<OffscreenFrameSink>(config.width, config.height);
        document_handle = tc_ui_document_create();
        if (tc_ui_document_handle_is_invalid(document_handle)) {
            offscreen_error("could not create tc_ui_document");
        }
        document = TcDocument(document_handle);
        try {
            renderer = std::make_unique<DocumentRenderer>(
                *graphics, document, config.renderer, *sink, platform);
        } catch (...) {
            tc_ui_document_destroy(document_handle);
            document = TcDocument{};
            document_handle = tc_ui_document_handle_invalid();
            throw;
        }
    }

    void require_open(const char* operation) const {
        if (closed || !graphics || graphics->is_closed() || !document.valid() ||
            !renderer || !renderer->is_open()) {
            offscreen_error(
                std::string("OffscreenGuiComposition::") + operation +
                " called after close");
        }
    }

    void prepare_frame() {
        if (sink->latest_texture() &&
            sink->latest_frame_size() != sink->framebuffer_size()) {
            sink->clear_published_texture();
        }
    }

    void close() {
        if (closed) return;
        accepting_input.store(false, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(input_mutex);
            input_queue.clear();
        }
        if (renderer) {
            renderer->close();
            renderer.reset();
        }
        if (sink) sink->clear_published_texture();
        if (!tc_ui_document_handle_is_invalid(document_handle)) {
            tc_ui_document_destroy(document_handle);
            document = TcDocument{};
            document_handle = tc_ui_document_handle_invalid();
        }
        if (graphics) {
            graphics->close();
            graphics.reset();
        }
        closed = true;
    }
};

OffscreenGuiComposition::OffscreenGuiComposition(
    OffscreenGuiCompositionConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

OffscreenGuiComposition::~OffscreenGuiComposition() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-offscreen] shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-offscreen] shutdown failed with unknown exception");
    }
}

OffscreenGuiComposition::OffscreenGuiComposition(
    OffscreenGuiComposition&&) noexcept = default;
OffscreenGuiComposition& OffscreenGuiComposition::operator=(
    OffscreenGuiComposition&&) noexcept = default;

tgfx::GraphicsHost& OffscreenGuiComposition::graphics() {
    impl_->require_open("graphics");
    return *impl_->graphics;
}
const tgfx::GraphicsHost& OffscreenGuiComposition::graphics() const {
    impl_->require_open("graphics");
    return *impl_->graphics;
}
TcDocument OffscreenGuiComposition::document() const {
    impl_->require_open("document");
    return impl_->document;
}
DocumentRenderer& OffscreenGuiComposition::renderer() {
    impl_->require_open("renderer");
    return *impl_->renderer;
}
const DocumentRenderer& OffscreenGuiComposition::renderer() const {
    impl_->require_open("renderer");
    return *impl_->renderer;
}
InMemoryDocumentPlatformServices&
OffscreenGuiComposition::platform_services() {
    impl_->require_open("platform_services");
    return impl_->platform;
}

void OffscreenGuiComposition::push_pointer(tc_ui_pointer_event event) {
    if (!impl_ || !impl_->accepting_input.load(std::memory_order_acquire)) {
        offscreen_error("push_pointer called after close");
    }
    const std::lock_guard<std::mutex> lock(impl_->input_mutex);
    if (!impl_->accepting_input.load(std::memory_order_relaxed)) {
        offscreen_error("push_pointer raced with close");
    }
    impl_->input_queue.push_back(PointerInput{event});
}

void OffscreenGuiComposition::push_key(tc_ui_key_event event) {
    if (!impl_ || !impl_->accepting_input.load(std::memory_order_acquire)) {
        offscreen_error("push_key called after close");
    }
    const std::lock_guard<std::mutex> lock(impl_->input_mutex);
    if (!impl_->accepting_input.load(std::memory_order_relaxed)) {
        offscreen_error("push_key raced with close");
    }
    impl_->input_queue.push_back(KeyInput{event});
}

void OffscreenGuiComposition::push_text(std::string utf8) {
    if (!impl_ || !impl_->accepting_input.load(std::memory_order_acquire)) {
        offscreen_error("push_text called after close");
    }
    const std::lock_guard<std::mutex> lock(impl_->input_mutex);
    if (!impl_->accepting_input.load(std::memory_order_relaxed)) {
        offscreen_error("push_text raced with close");
    }
    impl_->input_queue.push_back(TextInput{std::move(utf8)});
}

size_t OffscreenGuiComposition::pump_input() {
    impl_->require_open("pump_input");
    std::deque<QueuedInput> pending;
    {
        const std::lock_guard<std::mutex> lock(impl_->input_mutex);
        pending.swap(impl_->input_queue);
    }
    for (const QueuedInput& input : pending) {
        std::visit(
            [this](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, PointerInput>) {
                    impl_->renderer->dispatch_pointer(value.event);
                } else if constexpr (std::is_same_v<T, KeyInput>) {
                    impl_->renderer->dispatch_key(value.event);
                } else {
                    impl_->renderer->dispatch_text(value.utf8);
                }
            },
            input);
    }
    return pending.size();
}

bool OffscreenGuiComposition::render_frame() {
    impl_->require_open("render_frame");
    impl_->prepare_frame();
    return impl_->renderer->render_frame();
}

bool OffscreenGuiComposition::tick() {
    impl_->require_open("tick");
    pump_input();
    if (should_close()) return false;
    if (impl_->config.continuous_rendering ||
        impl_->renderer->repaint_requested()) {
        render_frame();
    }
    return true;
}

void OffscreenGuiComposition::resize(int width, int height) {
    impl_->require_open("resize");
    if (width <= 0 || height <= 0) {
        offscreen_error("resize requires positive dimensions");
    }
    impl_->sink->resize(width, height);
    impl_->renderer->request_repaint();
}

std::pair<int, int> OffscreenGuiComposition::framebuffer_size() const {
    impl_->require_open("framebuffer_size");
    return impl_->sink->framebuffer_size();
}
uint64_t OffscreenGuiComposition::frame_generation() const {
    impl_->require_open("frame_generation");
    return impl_->sink->generation();
}
tgfx::TextureHandle OffscreenGuiComposition::latest_frame_texture() const {
    impl_->require_open("latest_frame_texture");
    return impl_->sink->latest_texture();
}
std::pair<int, int> OffscreenGuiComposition::latest_frame_size() const {
    impl_->require_open("latest_frame_size");
    return impl_->sink->latest_frame_size();
}

std::vector<float> OffscreenGuiComposition::read_frame_rgba_float() {
    impl_->require_open("read_frame_rgba_float");
    const tgfx::TextureHandle texture = impl_->sink->latest_texture();
    if (!texture) offscreen_error("no published frame is available to read");
    const auto [width, height] = impl_->sink->latest_frame_size();
    std::vector<float> pixels(
        static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    impl_->renderer->wait_idle();
    if (!impl_->graphics->device().read_texture_rgba_float(
            texture, pixels.data())) {
        offscreen_error("failed to read the published frame");
    }
    return pixels;
}

void OffscreenGuiComposition::request_repaint() {
    renderer().request_repaint();
}
bool OffscreenGuiComposition::repaint_requested() const {
    return is_open() && impl_->renderer->repaint_requested();
}
void OffscreenGuiComposition::request_close() {
    if (impl_) impl_->close_requested.store(true, std::memory_order_release);
}
bool OffscreenGuiComposition::should_close() const {
    return !is_open() ||
        impl_->close_requested.load(std::memory_order_acquire);
}
void OffscreenGuiComposition::wait_idle() {
    renderer().wait_idle();
}
void OffscreenGuiComposition::close() {
    if (impl_) impl_->close();
}
bool OffscreenGuiComposition::is_open() const {
    return impl_ && !impl_->closed;
}

} // namespace termin::gui_native
