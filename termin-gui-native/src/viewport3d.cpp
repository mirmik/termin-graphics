#include <termin/gui_native/viewport3d.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string_view>

namespace termin::gui_native {

namespace {

constexpr int kInputRelease = 0;
constexpr int kInputPress = 1;
constexpr int kInputRepeat = 2;

struct ViewportPixelGeometry {
    tc_ui_rect rect{};
    ViewportSurfaceSize surface_size{};
};

ViewportPixelGeometry pixel_aligned_geometry(tc_ui_rect layout_rect) {
    if (!std::isfinite(layout_rect.x) || !std::isfinite(layout_rect.y) ||
        !std::isfinite(layout_rect.width) || !std::isfinite(layout_rect.height) ||
        layout_rect.width <= 0.0f || layout_rect.height <= 0.0f) {
        return {};
    }

    const double left = std::round(static_cast<double>(layout_rect.x));
    const double top = std::round(static_cast<double>(layout_rect.y));
    const double right = std::round(static_cast<double>(layout_rect.x) +
                                    static_cast<double>(layout_rect.width));
    const double bottom = std::round(static_cast<double>(layout_rect.y) +
                                     static_cast<double>(layout_rect.height));
    const double width = right - left;
    const double height = bottom - top;
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0.0 || height <= 0.0) {
        return {};
    }

    const double max_dimension = static_cast<double>(std::numeric_limits<int>::max());
    const int pixel_width = static_cast<int>(std::min(width, max_dimension));
    const int pixel_height = static_cast<int>(std::min(height, max_dimension));
    return {
        tc_ui_rect{static_cast<float>(left), static_cast<float>(top),
                   static_cast<float>(pixel_width), static_cast<float>(pixel_height)},
        ViewportSurfaceSize{pixel_width, pixel_height},
    };
}

} // namespace

Viewport3D::Viewport3D() : NativeWidget("Viewport3D") {
    set_style_role(TC_UI_STYLE_PANEL);
    set_focusable(true);
    set_preferred_size(tc_ui_size{320.0f, 200.0f});
}

Viewport3D::~Viewport3D() { detach_surface(); }

void Viewport3D::set_surface_host(std::shared_ptr<ViewportSurfaceHost> host) {
    if (surface_host_ == host)
        return;
    surface_host_ = std::move(host);
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    sync_surface_size();
}

void Viewport3D::detach_surface() {
    if (!surface_host_)
        return;
    surface_host_.reset();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void Viewport3D::log_host_failure(const char* operation) const {
    tc_log_error("[termin-gui-native] Viewport3D surface host failed during %s", operation);
}

bool Viewport3D::surface_valid() const {
    if (!surface_host_)
        return false;
    try {
        return surface_host_->is_valid();
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D surface validity check failed: %s",
                     error.what());
    } catch (...) {
        log_host_failure("validity check");
    }
    return false;
}

uint32_t Viewport3D::texture_id() const {
    if (!surface_valid())
        return 0;
    try {
        return surface_host_->texture_id();
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D texture lookup failed: %s", error.what());
    } catch (...) {
        log_host_failure("texture lookup");
    }
    return 0;
}

ViewportSurfaceSize Viewport3D::surface_size() const {
    if (!surface_valid())
        return {};
    try {
        const ViewportSurfaceSize size = surface_host_->framebuffer_size();
        if (size.width < 0 || size.height < 0) {
            tc_log_error("[termin-gui-native] Viewport3D surface host returned a negative size");
            return {};
        }
        return size;
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D size lookup failed: %s", error.what());
    } catch (...) {
        log_host_failure("size lookup");
    }
    return {};
}

bool Viewport3D::sync_surface_size() {
    if (!surface_valid())
        return false;
    const ViewportSurfaceSize next = pixel_aligned_geometry(bounds()).surface_size;
    if (next.width <= 0 || next.height <= 0)
        return false;
    ViewportSurfaceSize previous{};
    try {
        previous = surface_host_->framebuffer_size();
        if (previous.width < 0 || previous.height < 0) {
            tc_log_error("[termin-gui-native] Viewport3D surface host returned a negative size");
            return false;
        }
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D size lookup failed during resize: %s",
                     error.what());
        return false;
    } catch (...) {
        log_host_failure("size lookup during resize");
        return false;
    }
    if (previous == next)
        return true;
    try {
        before_resize_.emit(*this, previous, next);
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D before-resize callback failed: %s",
                     error.what());
        return false;
    } catch (...) {
        tc_log_error("[termin-gui-native] Viewport3D before-resize callback failed");
        return false;
    }
    try {
        if (!surface_host_->resize(next.width, next.height)) {
            log_host_failure("resize");
            return false;
        }
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D resize failed: %s", error.what());
        return false;
    } catch (...) {
        log_host_failure("resize");
        return false;
    }
    mark_dirty(TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool Viewport3D::dispatch_external_drag(const ViewportExternalDragEvent& event) {
    if (!external_drag_handler_)
        return false;
    if (!std::isfinite(event.x) || !std::isfinite(event.y)) {
        tc_log_error("[termin-gui-native] Viewport3D rejected non-finite external drag position");
        return false;
    }
    try {
        return external_drag_handler_(event);
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D external drag handler failed: %s",
                     error.what());
    } catch (...) {
        tc_log_error("[termin-gui-native] Viewport3D external drag handler failed");
    }
    return false;
}

tc_ui_size Viewport3D::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return detail::clamp_size(preferred_size(), constraints);
}

void Viewport3D::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    sync_surface_size();
}

void Viewport3D::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    tc_ui_painter_fill_rect(context, bounds(), style.background);
    const uint32_t texture = texture_id();
    if (texture != 0) {
        const tc_ui_rect destination = pixel_aligned_geometry(bounds()).rect;
        if (destination.width > 0.0f && destination.height > 0.0f) {
            tc_ui_painter_draw_texture(context, texture, destination,
                                       tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_LINEAR, false);
        }
    }
}

bool Viewport3D::sync_pointer_position(const tc_ui_pointer_event& event) {
    if (!surface_valid())
        return false;
    const tc_ui_rect rect = pixel_aligned_geometry(bounds()).rect;
    try {
        return surface_host_->pointer_move(static_cast<double>(event.x - rect.x),
                                           static_cast<double>(event.y - rect.y));
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D pointer move failed: %s", error.what());
    } catch (...) {
        log_host_failure("pointer move");
    }
    return false;
}

tc_ui_event_result Viewport3D::pointer_event(tc_ui_document* document,
                                             const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_MOVE) {
        sync_pointer_position(*event);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_WHEEL) {
        sync_pointer_position(*event);
        if (surface_valid()) {
            try {
                surface_host_->scroll(event->wheel_x, event->wheel_y, event->modifiers);
            } catch (const std::exception& error) {
                tc_log_error("[termin-gui-native] Viewport3D scroll failed: %s", error.what());
            } catch (...) {
                log_host_failure("scroll");
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN || event->type == TC_UI_POINTER_UP) {
        tc_ui_document_set_focus(document, handle());
        sync_pointer_position(*event);
        if (surface_valid()) {
            try {
                const int action = event->type == TC_UI_POINTER_DOWN ? kInputPress : kInputRelease;
                surface_host_->pointer_button(event->button, action, event->modifiers,
                                              std::max(1u, event->click_count));
            } catch (const std::exception& error) {
                tc_log_error("[termin-gui-native] Viewport3D pointer button failed: %s",
                             error.what());
            } catch (...) {
                log_host_failure("pointer button");
            }
        }
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result Viewport3D::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    if (surface_valid()) {
        try {
            int action = event->type == TC_UI_KEY_UP ? kInputRelease : kInputPress;
            if (event->type == TC_UI_KEY_DOWN && event->repeat)
                action = kInputRepeat;
            surface_host_->key(event->key, event->scancode, action, event->modifiers);
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] Viewport3D key dispatch failed: %s", error.what());
        } catch (...) {
            log_host_failure("key dispatch");
        }
    }
    return TC_UI_EVENT_HANDLED;
}

tc_ui_event_result Viewport3D::text_event(tc_ui_document*, const tc_ui_text_event* event) {
    if (!event || !event->text)
        return TC_UI_EVENT_IGNORED;
    if (!surface_valid())
        return TC_UI_EVENT_HANDLED;
    std::string_view text{event->text};
    size_t offset = 0;
    try {
        while (offset < text.size()) {
            uint32_t codepoint = 0;
            if (!detail::decode_utf8(text, offset, codepoint)) {
                tc_log_error("[termin-gui-native] Viewport3D rejected invalid UTF-8 text input");
                return TC_UI_EVENT_HANDLED;
            }
            surface_host_->text(codepoint);
        }
    } catch (const std::exception& error) {
        tc_log_error("[termin-gui-native] Viewport3D text dispatch failed: %s", error.what());
    } catch (...) {
        log_host_failure("text dispatch");
    }
    return TC_UI_EVENT_HANDLED;
}

void Viewport3D::on_destroy(tc_ui_document*) {
    external_drag_handler_ = {};
    before_resize_ = {};
    detach_surface();
}

} // namespace termin::gui_native
