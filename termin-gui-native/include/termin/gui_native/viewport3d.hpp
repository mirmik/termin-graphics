#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

struct ViewportSurfaceSize {
    int width = 0;
    int height = 0;

    friend bool operator==(const ViewportSurfaceSize&, const ViewportSurfaceSize&) = default;
};

class ViewportSurfaceHost {
public:
    virtual ~ViewportSurfaceHost() = default;

    virtual bool is_valid() const = 0;
    virtual uint32_t texture_id() const = 0;
    virtual ViewportSurfaceSize framebuffer_size() const = 0;
    virtual bool resize(int width, int height) = 0;
    virtual bool pointer_move(double x, double y) = 0;
    virtual bool pointer_button(int button, int action, int modifiers, uint32_t click_count) = 0;
    virtual bool scroll(double x, double y, int modifiers) = 0;
    virtual bool key(int key, int scancode, int action, int modifiers) = 0;
    virtual bool text(uint32_t codepoint) = 0;
};

enum class ViewportExternalDragPhase {
    Enter,
    Move,
    Leave,
    Drop,
};

struct ViewportExternalDragEvent {
    ViewportExternalDragPhase phase = ViewportExternalDragPhase::Move;
    std::string mime_type;
    std::string payload;
    float x = 0.0f;
    float y = 0.0f;
};

class Viewport3D : public NativeWidget {
public:
    using ExternalDragHandler = std::function<bool(const ViewportExternalDragEvent&)>;

    Viewport3D();
    ~Viewport3D() override;

    void set_surface_host(std::shared_ptr<ViewportSurfaceHost> host);
    std::shared_ptr<ViewportSurfaceHost> surface_host() const { return surface_host_; }
    void detach_surface();
    bool has_surface() const { return static_cast<bool>(surface_host_); }
    bool surface_valid() const;
    uint32_t texture_id() const;
    ViewportSurfaceSize surface_size() const;

    Signal<Viewport3D&, ViewportSurfaceSize, ViewportSurfaceSize>& before_resize() {
        return before_resize_;
    }
    void set_external_drag_handler(ExternalDragHandler handler) {
        external_drag_handler_ = std::move(handler);
    }
    bool dispatch_external_drag(const ViewportExternalDragEvent& event);

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
    tc_ui_event_result text_event(tc_ui_document* document, const tc_ui_text_event* event) override;
    void on_destroy(tc_ui_document* document) override;

private:
    bool sync_surface_size();
    bool sync_pointer_position(const tc_ui_pointer_event& event);
    void log_host_failure(const char* operation) const;

    std::shared_ptr<ViewportSurfaceHost> surface_host_;
    Signal<Viewport3D&, ViewportSurfaceSize, ViewportSurfaceSize> before_resize_;
    ExternalDragHandler external_drag_handler_;
};

} // namespace termin::gui_native
