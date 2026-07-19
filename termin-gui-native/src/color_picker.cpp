#include "widgets_internal.hpp"

#include <array>
#include <cstdio>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

namespace {

constexpr uint32_t kSurfaceResolution = 64;
constexpr uint32_t kSvInvalidated = 1u << 0u;
constexpr uint32_t kHueInvalidated = 1u << 1u;
constexpr uint32_t kAlphaInvalidated = 1u << 2u;

uint8_t channel(float value) {
    return static_cast<uint8_t>(clamp_float(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

bool contains(tc_ui_rect rect, float x, float y) {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

} // namespace

ColorPicker::ColorPicker(std::shared_ptr<ColorPickerModel> model)
    : NativeWidget("ColorPicker"),
      model_(model ? std::move(model) : std::make_shared<ColorPickerModel>()) {
    set_focusable(true);
    set_preferred_size(tc_ui_size{250.0f, 244.0f});
    connect_model();
    generate_surface(ColorPickerSurfaceKind::SaturationValue);
    generate_surface(ColorPickerSurfaceKind::Hue);
    generate_surface(ColorPickerSurfaceKind::Alpha);
}

ColorPicker::~ColorPicker() { disconnect_model(); }

void ColorPicker::connect_model() {
    if (!model_ || model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect(
        [this](ColorPickerModel&, uint32_t flags) { on_model_changed(flags); });
}

void ColorPicker::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void ColorPicker::set_model(std::shared_ptr<ColorPickerModel> model) {
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<ColorPickerModel>();
    connect_model();
    sv_surface_.revision = 0;
    hue_surface_.revision = 0;
    alpha_surface_.revision = 0;
    texture_ids_ = {};
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    surfaces_invalidated_.emit(*this, kSvInvalidated | kHueInvalidated | kAlphaInvalidated);
}

void ColorPicker::set_texture_ids(ColorPickerTextureIds texture_ids) {
    texture_ids_ = texture_ids;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void ColorPicker::on_model_changed(uint32_t flags) {
    uint32_t invalidated = 0;
    if ((flags & color_picker_change_mask(ColorPickerChange::SvSurface)) != 0) {
        sv_surface_.revision = 0;
        invalidated |= kSvInvalidated;
    }
    if ((flags & color_picker_change_mask(ColorPickerChange::AlphaSurface)) != 0) {
        alpha_surface_.revision = 0;
        invalidated |= kAlphaInvalidated;
    }
    mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    if (invalidated != 0)
        surfaces_invalidated_.emit(*this, invalidated);
}

void ColorPicker::generate_surface(ColorPickerSurfaceKind kind) {
    ColorPickerSurface* output = nullptr;
    uint32_t width = kSurfaceResolution;
    uint32_t height = kSurfaceResolution;
    if (kind == ColorPickerSurfaceKind::SaturationValue) {
        output = &sv_surface_;
    } else if (kind == ColorPickerSurfaceKind::Hue) {
        output = &hue_surface_;
        width = 1;
    } else {
        output = &alpha_surface_;
        width = 1;
    }
    if (output->revision != 0)
        return;
    output->width = width;
    output->height = height;
    output->rgba.assign(static_cast<size_t>(width) * height * 4, 255);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            Color color;
            if (kind == ColorPickerSurfaceKind::SaturationValue) {
                const float saturation = static_cast<float>(x) / static_cast<float>(width - 1);
                const float value = 1.0f - static_cast<float>(y) / static_cast<float>(height - 1);
                color = ColorPickerModel::hsv_to_rgb(model_->hue(), saturation, value);
            } else if (kind == ColorPickerSurfaceKind::Hue) {
                const float hue = static_cast<float>(y) / static_cast<float>(height - 1);
                color = ColorPickerModel::hsv_to_rgb(hue, 1.0f, 1.0f);
            } else {
                const float alpha = 1.0f - static_cast<float>(y) / static_cast<float>(height - 1);
                const Color current = model_->color();
                const float checker = ((y / 6u) % 2u) == 0u ? 0.32f : 0.52f;
                color = Color{current.r * alpha + checker * (1.0f - alpha),
                              current.g * alpha + checker * (1.0f - alpha),
                              current.b * alpha + checker * (1.0f - alpha), 1.0f};
            }
            const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
            output->rgba[offset] = channel(color.r);
            output->rgba[offset + 1] = channel(color.g);
            output->rgba[offset + 2] = channel(color.b);
            output->rgba[offset + 3] = channel(color.a);
        }
    }
    output->revision = model_->revision();
}

const ColorPickerSurface& ColorPicker::surface(ColorPickerSurfaceKind kind) {
    generate_surface(kind);
    if (kind == ColorPickerSurfaceKind::SaturationValue)
        return sv_surface_;
    if (kind == ColorPickerSurfaceKind::Hue)
        return hue_surface_;
    return alpha_surface_;
}

tc_ui_size ColorPicker::measure(tc_ui_document*, tc_ui_constraints constraints) {
    const float width =
        surface_size_ + gap_ + bar_width_ + (model_->show_alpha() ? gap_ + bar_width_ : 0.0f);
    return clamp_size(tc_ui_size{width, surface_size_ + gap_ + preview_height_ + label_height_},
                      constraints);
}

void ColorPicker::layout(tc_ui_document* document, tc_ui_rect rect) {
    NativeWidget::layout(document, rect);
    content_rect_ = rect;
    const float bars = gap_ + bar_width_ + (model_->show_alpha() ? gap_ + bar_width_ : 0.0f);
    surface_size_ = std::max(
        1.0f, std::min(180.0f, std::min(rect.width - bars,
                                        rect.height - gap_ - preview_height_ - label_height_)));
}

tc_ui_rect ColorPicker::sv_rect() const {
    return tc_ui_rect{content_rect_.x, content_rect_.y, surface_size_, surface_size_};
}

tc_ui_rect ColorPicker::hue_rect() const {
    return tc_ui_rect{content_rect_.x + surface_size_ + gap_, content_rect_.y, bar_width_,
                      surface_size_};
}

tc_ui_rect ColorPicker::alpha_rect() const {
    return tc_ui_rect{hue_rect().x + bar_width_ + gap_, content_rect_.y, bar_width_, surface_size_};
}

tc_ui_rect ColorPicker::preview_rect() const {
    return tc_ui_rect{content_rect_.x, content_rect_.y + surface_size_ + gap_, surface_size_,
                      preview_height_};
}

void ColorPicker::paint_checker(tc_ui_paint_context* context, tc_ui_rect rect) const {
    constexpr float cell = 8.0f;
    const int rows = static_cast<int>(std::ceil(rect.height / cell));
    const int columns = static_cast<int>(std::ceil(rect.width / cell));
    for (int row = 0; row < rows; ++row) {
        const float y = rect.y + static_cast<float>(row) * cell;
        for (int column = 0; column < columns; ++column) {
            const float x = rect.x + static_cast<float>(column) * cell;
            const float shade = ((column + row) & 1) == 0 ? 0.32f : 0.52f;
            tc_ui_painter_fill_rect(context,
                                    tc_ui_rect{x, y, std::min(cell, rect.x + rect.width - x),
                                               std::min(cell, rect.y + rect.height - y)},
                                    tc_ui_color{shade, shade, shade, 1.0f});
        }
    }
}

void ColorPicker::paint_fallback_surfaces(tc_ui_paint_context* context) const {
    const tc_ui_rect sv = sv_rect();
    // The fallback renderer emits one UI draw per cell. Keep it bounded until
    // ColorPickerSurface is uploaded and drawn as a texture by the UI host.
    constexpr int divisions = 24;
    for (int row = 0; row < divisions; ++row) {
        for (int column = 0; column < divisions; ++column) {
            const float saturation = static_cast<float>(column) / (divisions - 1);
            const float value = 1.0f - static_cast<float>(row) / (divisions - 1);
            const Color color = ColorPickerModel::hsv_to_rgb(model_->hue(), saturation, value);
            const float x0 = sv.x + sv.width * static_cast<float>(column) / divisions;
            const float y0 = sv.y + sv.height * static_cast<float>(row) / divisions;
            const float x1 = sv.x + sv.width * static_cast<float>(column + 1) / divisions;
            const float y1 = sv.y + sv.height * static_cast<float>(row + 1) / divisions;
            tc_ui_painter_fill_rect(context, tc_ui_rect{x0, y0, x1 - x0 + 0.5f, y1 - y0 + 0.5f},
                                    color.c_color());
        }
    }
    const tc_ui_rect hue = hue_rect();
    constexpr int strips = 48;
    for (int row = 0; row < strips; ++row) {
        const float ratio = static_cast<float>(row) / (strips - 1);
        const Color color = ColorPickerModel::hsv_to_rgb(ratio, 1.0f, 1.0f);
        const float y0 = hue.y + hue.height * static_cast<float>(row) / strips;
        const float y1 = hue.y + hue.height * static_cast<float>(row + 1) / strips;
        tc_ui_painter_fill_rect(context, tc_ui_rect{hue.x, y0, hue.width, y1 - y0 + 0.5f},
                                color.c_color());
    }
    if (model_->show_alpha()) {
        const tc_ui_rect alpha = alpha_rect();
        paint_checker(context, alpha);
        const Color current = model_->color();
        for (int row = 0; row < strips; ++row) {
            const float opacity = 1.0f - static_cast<float>(row) / (strips - 1);
            const float checker = ((row / 4) & 1) == 0 ? 0.32f : 0.52f;
            const Color blended{current.r * opacity + checker * (1.0f - opacity),
                                current.g * opacity + checker * (1.0f - opacity),
                                current.b * opacity + checker * (1.0f - opacity), 1.0f};
            const float y0 = alpha.y + alpha.height * static_cast<float>(row) / strips;
            const float y1 = alpha.y + alpha.height * static_cast<float>(row + 1) / strips;
            tc_ui_painter_fill_rect(context, tc_ui_rect{alpha.x, y0, alpha.width, y1 - y0 + 0.5f},
                                    blended.c_color());
        }
    }
}

void ColorPicker::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect sv = sv_rect();
    const tc_ui_rect hue = hue_rect();
    const tc_ui_rect alpha = alpha_rect();
    const bool textures_ready = texture_ids_.saturation_value != 0 && texture_ids_.hue != 0 &&
                                (!model_->show_alpha() || texture_ids_.alpha != 0);
    if (textures_ready) {
        tc_ui_painter_draw_texture(context, texture_ids_.saturation_value, sv,
                                   tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
                                   TC_UI_TEXTURE_SAMPLING_LINEAR, false);
        tc_ui_painter_draw_texture(context, texture_ids_.hue, hue,
                                   tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
                                   TC_UI_TEXTURE_SAMPLING_LINEAR, false);
        if (model_->show_alpha())
            tc_ui_painter_draw_texture(context, texture_ids_.alpha, alpha,
                                       tc_ui_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_LINEAR, false);
    } else {
        paint_fallback_surfaces(context);
    }

    const float marker_x = sv.x + model_->saturation() * sv.width;
    const float marker_y = sv.y + (1.0f - model_->value()) * sv.height;
    tc_ui_painter_stroke_rect(context, tc_ui_rect{marker_x - 5.0f, marker_y - 5.0f, 10.0f, 10.0f},
                              tc_ui_color{1.0f, 1.0f, 1.0f, 0.95f}, 2.0f);
    tc_ui_painter_stroke_rect(context, tc_ui_rect{marker_x - 3.5f, marker_y - 3.5f, 7.0f, 7.0f},
                              tc_ui_color{0.0f, 0.0f, 0.0f, 0.65f}, 1.0f);
    const float hue_y = hue.y + model_->hue() * hue.height;
    tc_ui_painter_stroke_rect(context,
                              tc_ui_rect{hue.x - 1.0f, hue_y - 2.0f, hue.width + 2.0f, 4.0f},
                              style.foreground, 2.0f);
    if (model_->show_alpha()) {
        const float alpha_y = alpha.y + (1.0f - model_->alpha()) * alpha.height;
        tc_ui_painter_stroke_rect(
            context, tc_ui_rect{alpha.x - 1.0f, alpha_y - 2.0f, alpha.width + 2.0f, 4.0f},
            style.foreground, 2.0f);
    }

    const tc_ui_rect preview = preview_rect();
    const float half = (preview.width - gap_) * 0.5f;
    const tc_ui_rect old_rect{preview.x, preview.y, half, preview.height};
    const tc_ui_rect new_rect{preview.x + half + gap_, preview.y, half, preview.height};
    paint_checker(context, old_rect);
    paint_checker(context, new_rect);
    tc_ui_painter_fill_rect(context, old_rect, model_->initial_color().c_color());
    tc_ui_painter_fill_rect(context, new_rect, model_->color().c_color());
    tc_ui_painter_stroke_rect(context, old_rect, style.border, 1.0f);
    tc_ui_painter_stroke_rect(context, new_rect, style.border, 1.0f);

    const Color current = model_->color();
    std::array<char, 16> text{};
    if (model_->show_alpha()) {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X%02X", channel(current.r),
                      channel(current.g), channel(current.b), channel(current.a));
    } else {
        std::snprintf(text.data(), text.size(), "#%02X%02X%02X", channel(current.r),
                      channel(current.g), channel(current.b));
    }
    tc_ui_painter_draw_text(context, text.data(),
                            tc_ui_point{preview.x, preview.y + preview.height + 15.0f},
                            style.font_size, style.foreground);
}

void ColorPicker::apply_pointer(float x, float y) {
    if (dragging_ == DragTarget::SaturationValue) {
        const tc_ui_rect rect = sv_rect();
        model_->set_hsv(model_->hue(), clamp_float((x - rect.x) / rect.width, 0.0f, 1.0f),
                        clamp_float(1.0f - (y - rect.y) / rect.height, 0.0f, 1.0f));
    } else if (dragging_ == DragTarget::Hue) {
        const tc_ui_rect rect = hue_rect();
        model_->set_hue(clamp_float((y - rect.y) / rect.height, 0.0f, 1.0f));
    } else if (dragging_ == DragTarget::Alpha) {
        const tc_ui_rect rect = alpha_rect();
        model_->set_alpha(clamp_float(1.0f - (y - rect.y) / rect.height, 0.0f, 1.0f));
    }
}

tc_ui_event_result ColorPicker::pointer_event(tc_ui_document* document,
                                              const tc_ui_pointer_event* event) {
    if (!event)
        return TC_UI_EVENT_IGNORED;
    const bool captured = tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle());
    if (event->type == TC_UI_POINTER_DOWN) {
        if (contains(sv_rect(), event->x, event->y))
            dragging_ = DragTarget::SaturationValue;
        else if (contains(hue_rect(), event->x, event->y))
            dragging_ = DragTarget::Hue;
        else if (model_->show_alpha() && contains(alpha_rect(), event->x, event->y))
            dragging_ = DragTarget::Alpha;
        else
            return TC_UI_EVENT_IGNORED;
        tc_ui_document_set_focus(document, handle());
        tc_ui_document_set_pointer_capture(document, handle());
        apply_pointer(event->x, event->y);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE && (dragging_ != DragTarget::None || captured)) {
        apply_pointer(event->x, event->y);
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_UP && (dragging_ != DragTarget::None || captured)) {
        dragging_ = DragTarget::None;
        if (captured)
            tc_ui_document_release_pointer_capture(document, handle());
        return TC_UI_EVENT_HANDLED;
    }
    return TC_UI_EVENT_IGNORED;
}

void ColorPicker::on_destroy(tc_ui_document* document) {
    if (tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle()))
        tc_ui_document_release_pointer_capture(document, handle());
    dragging_ = DragTarget::None;
}

} // namespace termin::gui_native
