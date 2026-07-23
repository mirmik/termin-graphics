#include <termin/gui_native/document_renderer.hpp>

#include "application_host_internal.hpp"

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <tcbase/tc_log.h>

#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/draw_list_renderer.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

namespace termin::gui_native {

namespace {

[[noreturn]] void renderer_error(const std::string& message) {
    tc_log_error("[gui-native-document-renderer] %s", message.c_str());
    throw std::logic_error(message);
}

struct DrawListDeleter {
    void operator()(tc_ui_draw_list* draw_list) const {
        tc_ui_draw_list_destroy(draw_list);
    }
};

struct PaintContextDeleter {
    void operator()(tc_ui_paint_context* context) const {
        tc_ui_paint_context_destroy(context);
    }
};

tgfx::TextureHandle create_color_target(
    tgfx::IRenderDevice& device, int width, int height) {
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

struct DocumentRenderer::Impl {
    DocumentRenderer* facade;
    tgfx::GraphicsHost* graphics;
    Document* document;
    DocumentRendererConfig config;
    DocumentFrameSink* frame_sink;
    DocumentPlatformServices* platform;
    tgfx::IRenderDevice* device;
    tgfx::RenderContext2* context;
    UiDrawListRenderer renderer;
    std::unique_ptr<tc_ui_draw_list, DrawListDeleter> draw_list;
    std::unique_ptr<tc_ui_paint_context, PaintContextDeleter> paint_context;
    tgfx::TextureHandle color_target{};
    int target_width = 0;
    int target_height = 0;
    size_t rendered_frames = 0;
    std::atomic<bool> repaint_requested{true};
    std::mutex deferred_mutex;
    std::deque<std::function<void()>> deferred_callbacks;
    std::function<void(tgfx::RenderContext2&)> before_frame;
    std::vector<tc_widget_handle> color_pickers;
    std::shared_ptr<GuiApplicationHostLeaseState> texture_leases;
    std::thread::id owner_thread = std::this_thread::get_id();
    std::string clipboard_buffer;
    bool closed = false;

    Impl(DocumentRenderer& renderer_facade, tgfx::GraphicsHost& graphics_ref,
         Document& document_ref, DocumentRendererConfig renderer_config,
         DocumentFrameSink& sink, DocumentPlatformServices& services)
        : facade(&renderer_facade), graphics(&graphics_ref), document(&document_ref),
          config(std::move(renderer_config)), frame_sink(&sink), platform(&services),
          device(&graphics_ref.device()), context(&graphics_ref.context()),
          draw_list(tc_ui_draw_list_create()),
          paint_context(tc_ui_paint_context_create(draw_list.get())) {
        if (!document->valid()) {
            renderer_error("DocumentRenderer requires a live Document");
        }
        if (config.font_path.empty()) {
            renderer_error("DocumentRenderer requires a resolved font path");
        }
        if (!draw_list || !paint_context) {
            renderer_error("DocumentRenderer failed to allocate paint state");
        }
        if (!renderer.set_default_font_path(config.font_path, config.font_size)) {
            renderer_error(
                "DocumentRenderer failed to load UI font: " + config.font_path);
        }

        document->attach_application_host();
        try {
            if (!platform->set_text_input_enabled(config.enable_text_input)) {
                renderer_error("DocumentRenderer platform rejected text-input configuration");
            }
            renderer.bind_text_measurer(document->get());
            document->set_clipboard(&clipboard_get, &clipboard_set, this);
            document->set_cursor_changed_callback(&cursor_changed, this);
            texture_leases = std::make_shared<GuiApplicationHostLeaseState>();
            texture_leases->owner_thread = owner_thread;
            texture_leases->request_repaint = [this]() {
                facade->request_repaint();
            };
            texture_leases->defer = [this](std::function<void()> callback) {
                facade->defer(std::move(callback));
            };
            texture_leases->graphics = graphics;
            texture_leases->document = document;
        } catch (...) {
            document->set_cursor_changed_callback(nullptr, nullptr);
            document->set_clipboard(nullptr, nullptr, nullptr);
            document->set_text_measurer(nullptr, nullptr);
            try {
                platform->set_text_input_enabled(false);
            } catch (...) {
                tc_log_error(
                    "[gui-native-document-renderer] failed to roll back text input");
            }
            document->detach_application_host();
            throw;
        }
    }

    static const char* clipboard_get(void* user_data) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            self.clipboard_buffer = self.platform->clipboard_text();
            return self.clipboard_buffer.c_str();
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-document-renderer] clipboard read failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] clipboard read failed with unknown exception");
        }
        return nullptr;
    }

    static bool clipboard_set(
        void* user_data, const char* text, size_t byte_length) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            return self.platform->set_clipboard_text(
                std::string(text ? text : "", byte_length));
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-document-renderer] clipboard write failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] clipboard write failed with unknown exception");
        }
        return false;
    }

    static void cursor_changed(void* user_data, tc_ui_cursor_intent cursor) {
        auto& self = *static_cast<Impl*>(user_data);
        try {
            if (!self.platform->set_cursor(cursor)) {
                tc_log_error(
                    "[gui-native-document-renderer] platform rejected cursor update");
                return;
            }
            self.facade->request_repaint();
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-document-renderer] cursor update failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] cursor update failed with unknown exception");
        }
    }

    void require_owner(const char* operation) const {
        if (std::this_thread::get_id() != owner_thread) {
            renderer_error(
                std::string("DocumentRenderer::") + operation +
                " requires the owner thread");
        }
    }

    void require_open(const char* operation) const {
        require_owner(operation);
        if (closed || !graphics || graphics->is_closed() || !document ||
            !document->valid() || !frame_sink || !platform) {
            renderer_error(
                std::string("DocumentRenderer::") + operation +
                " called after dependency shutdown");
        }
    }

    void ensure_target(int width, int height) {
        if (color_target && target_width == width && target_height == height) return;
        if (color_target) {
            device->wait_idle();
            device->destroy(color_target);
            device->invalidate_render_target_cache();
            color_target = {};
        }
        color_target = create_color_target(*device, width, height);
        if (!color_target) {
            renderer_error("DocumentRenderer failed to create its color target");
        }
        target_width = width;
        target_height = height;
    }

    void close() {
        if (closed) return;
        require_owner("close");
        if (!document || !document->valid()) {
            renderer_error("Document must outlive DocumentRenderer");
        }
        if (!graphics || graphics->is_closed()) {
            renderer_error("GraphicsHost must outlive DocumentRenderer");
        }
        {
            const std::lock_guard<std::mutex> lock(deferred_mutex);
            deferred_callbacks.clear();
        }
        before_frame = {};
        color_pickers.clear();
        try {
            if (!platform->set_text_input_enabled(false)) {
                tc_log_error(
                    "[gui-native-document-renderer] platform rejected text-input shutdown");
            }
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-document-renderer] text-input shutdown failed: %s",
                error.what());
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] text-input shutdown failed with unknown exception");
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
        frame_sink = nullptr;
        platform = nullptr;
        context = nullptr;
        device = nullptr;
        graphics = nullptr;
        closed = true;
    }
};

DocumentRenderer::DocumentRenderer(
    tgfx::GraphicsHost& graphics, Document& document,
    DocumentRendererConfig config, DocumentFrameSink& frame_sink,
    DocumentPlatformServices& platform_services)
    : impl_(std::make_unique<Impl>(
          *this, graphics, document, std::move(config),
          frame_sink, platform_services)) {}

DocumentRenderer::~DocumentRenderer() {
    if (!impl_ || impl_->closed) return;
    try {
        impl_->close();
    } catch (const std::exception& error) {
        tc_log_error(
            "[gui-native-document-renderer] destructor shutdown failed: %s",
            error.what());
    } catch (...) {
        tc_log_error(
            "[gui-native-document-renderer] destructor shutdown failed with unknown exception");
    }
}

tgfx::GraphicsHost& DocumentRenderer::graphics() {
    impl_->require_open("graphics");
    return *impl_->graphics;
}

const tgfx::GraphicsHost& DocumentRenderer::graphics() const {
    impl_->require_open("graphics");
    return *impl_->graphics;
}

tgfx::IRenderDevice& DocumentRenderer::device() {
    return graphics().device();
}

const tgfx::IRenderDevice& DocumentRenderer::device() const {
    return graphics().device();
}

Document& DocumentRenderer::document() {
    impl_->require_open("document");
    return *impl_->document;
}

const Document& DocumentRenderer::document() const {
    impl_->require_open("document");
    return *impl_->document;
}

tc_ui_event_result DocumentRenderer::dispatch_pointer(
    const tc_ui_pointer_event& event) {
    impl_->require_open("dispatch_pointer");
    const tc_ui_event_result result = impl_->document->dispatch_pointer_event(event);
    request_repaint();
    return result;
}

tc_ui_event_result DocumentRenderer::dispatch_key(const tc_ui_key_event& event) {
    impl_->require_open("dispatch_key");
    const tc_ui_event_result result = impl_->document->dispatch_key_event(event);
    request_repaint();
    return result;
}

tc_ui_event_result DocumentRenderer::dispatch_text(const std::string& utf8) {
    impl_->require_open("dispatch_text");
    const tc_ui_text_event event{utf8.c_str()};
    const tc_ui_event_result result = impl_->document->dispatch_text_event(event);
    request_repaint();
    return result;
}

std::pair<int, int> DocumentRenderer::framebuffer_size() const {
    impl_->require_open("framebuffer_size");
    return impl_->frame_sink->framebuffer_size();
}

bool DocumentRenderer::render_frame() {
    impl_->require_open("render_frame");
    const auto [width, height] = framebuffer_size();
    if (width <= 0 || height <= 0) return false;
    impl_->repaint_requested.store(false, std::memory_order_release);
    impl_->ensure_target(width, height);

    impl_->context->begin_frame();
    if (impl_->before_frame) {
        try {
            impl_->before_frame(*impl_->context);
        } catch (const std::exception& error) {
            tc_log_error(
                "[gui-native-document-renderer] before-frame callback failed: %s",
                error.what());
            throw;
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] before-frame callback failed with "
                "an unknown exception");
            throw;
        }
    }
    for (auto iterator = impl_->color_pickers.begin();
         iterator != impl_->color_pickers.end();) {
        tc_widget* widget =
            tc_ui_document_resolve_widget(impl_->document->get(), *iterator);
        auto* picker = widget
            ? dynamic_cast<ColorPicker*>(static_cast<Widget*>(widget->body))
            : nullptr;
        if (!picker) {
            tc_log_error(
                "[gui-native-document-renderer] registered ColorPicker was "
                "destroyed without renderer unregistration");
            iterator = impl_->color_pickers.erase(iterator);
            continue;
        }
        impl_->renderer.sync_color_picker_surfaces(*impl_->context, *picker);
        ++iterator;
    }
    tc_ui_draw_list_clear(impl_->draw_list.get());
    impl_->document->layout_roots(
        tc_ui_rect{0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)});
    impl_->document->paint(impl_->paint_context.get());
    impl_->context->begin_pass(
        impl_->color_target, tgfx::TextureHandle{},
        impl_->config.clear_color.data(), 1.0f, false);
    impl_->renderer.render(
        *impl_->context, impl_->draw_list.get(), width, height);
    impl_->context->end_pass();
    impl_->context->end_frame();
    impl_->frame_sink->publish_frame(impl_->color_target);
    ++impl_->rendered_frames;
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        if (!impl_->deferred_callbacks.empty()) {
            impl_->repaint_requested.store(true, std::memory_order_release);
        }
    }
    return true;
}

void DocumentRenderer::set_before_frame_callback(
    std::function<void(tgfx::RenderContext2&)> callback) {
    impl_->require_open("set_before_frame_callback");
    impl_->before_frame = std::move(callback);
    request_repaint();
}

void DocumentRenderer::register_color_picker(ColorPicker& picker) {
    impl_->require_open("register_color_picker");
    if (!tc_ui_document_handle_eq(picker.document(), impl_->document->get())) {
        renderer_error(
            "DocumentRenderer cannot register a ColorPicker from another Document");
    }
    const tc_widget_handle handle = picker.handle();
    if (std::none_of(
            impl_->color_pickers.begin(), impl_->color_pickers.end(),
            [handle](tc_widget_handle candidate) {
                return tc_widget_handle_eq(candidate, handle);
            })) {
        impl_->color_pickers.push_back(handle);
        request_repaint();
    }
}

void DocumentRenderer::unregister_color_picker(ColorPicker& picker) {
    impl_->require_open("unregister_color_picker");
    impl_->renderer.release_color_picker_surfaces(picker);
    const tc_widget_handle handle = picker.handle();
    impl_->color_pickers.erase(
        std::remove_if(
            impl_->color_pickers.begin(), impl_->color_pickers.end(),
            [handle](tc_widget_handle candidate) {
                return tc_widget_handle_eq(candidate, handle);
            }),
        impl_->color_pickers.end());
    request_repaint();
}

void DocumentRenderer::defer(std::function<void()> callback) {
    if (!callback) {
        renderer_error("DocumentRenderer::defer requires a callback");
    }
    if (!impl_ || impl_->closed) {
        renderer_error("DocumentRenderer::defer called after close");
    }
    {
        const std::lock_guard<std::mutex> lock(impl_->deferred_mutex);
        impl_->deferred_callbacks.push_back(std::move(callback));
    }
    request_repaint();
}

size_t DocumentRenderer::run_deferred() {
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
            tc_log_error(
                "[gui-native-document-renderer] deferred callback failed: %s",
                error.what());
            throw;
        } catch (...) {
            tc_log_error(
                "[gui-native-document-renderer] deferred callback failed with "
                "an unknown exception");
            throw;
        }
    }
    return callbacks.size();
}

void DocumentRenderer::request_repaint() {
    if (impl_ && !impl_->closed) {
        impl_->repaint_requested.store(true, std::memory_order_release);
    }
}

bool DocumentRenderer::repaint_requested() const {
    return impl_ && impl_->repaint_requested.load(std::memory_order_acquire);
}

size_t DocumentRenderer::rendered_frame_count() const {
    impl_->require_open("rendered_frame_count");
    return impl_->rendered_frames;
}

tgfx::TextureHandle DocumentRenderer::color_target() const {
    impl_->require_open("color_target");
    return impl_->color_target;
}

void DocumentRenderer::wait_idle() {
    impl_->require_open("wait_idle");
    impl_->device->wait_idle();
}

void DocumentRenderer::close() {
    if (impl_) impl_->close();
}

bool DocumentRenderer::is_open() const {
    return impl_ && !impl_->closed;
}

std::shared_ptr<GuiApplicationHostLeaseState>
DocumentRenderer::texture_lease_state() const {
    impl_->require_open("texture_lease_state");
    return impl_->texture_leases;
}

} // namespace termin::gui_native
