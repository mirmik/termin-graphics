#include "widgets_internal.hpp"

#include <cmath>
#include <stdexcept>

namespace termin::gui_native {

namespace {

bool same(float left, float right) { return std::fabs(left - right) <= 1.0e-6f; }

} // namespace

ColorPickerModel::ColorPickerModel(Color initial, bool show_alpha)
    : initial_color_(initial), show_alpha_(show_alpha) {
    validate_color(initial);
    set_color(initial);
    revision_ = 1;
}

void ColorPickerModel::validate_unit(float value, const char* field) {
    if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
        tc_log_error("[termin-gui-native] ColorPickerModel rejected invalid %s", field);
        throw std::invalid_argument(std::string("color picker ") + field + " must be in [0, 1]");
    }
}

void ColorPickerModel::validate_color(Color color) {
    validate_unit(color.r, "red");
    validate_unit(color.g, "green");
    validate_unit(color.b, "blue");
    validate_unit(color.a, "alpha");
}

Color ColorPickerModel::hsv_to_rgb(float hue, float saturation, float value, float alpha) {
    validate_unit(hue, "hue");
    validate_unit(saturation, "saturation");
    validate_unit(value, "value");
    validate_unit(alpha, "alpha");
    if (same(saturation, 0.0f))
        return Color{value, value, value, alpha};
    const float scaled = (hue >= 1.0f ? 0.0f : hue) * 6.0f;
    const int sector = static_cast<int>(std::floor(scaled));
    const float fraction = scaled - static_cast<float>(sector);
    const float p = value * (1.0f - saturation);
    const float q = value * (1.0f - saturation * fraction);
    const float t = value * (1.0f - saturation * (1.0f - fraction));
    switch (sector) {
    case 0:
        return Color{value, t, p, alpha};
    case 1:
        return Color{q, value, p, alpha};
    case 2:
        return Color{p, value, t, alpha};
    case 3:
        return Color{p, q, value, alpha};
    case 4:
        return Color{t, p, value, alpha};
    default:
        return Color{value, p, q, alpha};
    }
}

Color ColorPickerModel::color() const {
    return hsv_to_rgb(hue_, saturation_, value_, show_alpha_ ? alpha_ : 1.0f);
}

void ColorPickerModel::emit_change(uint32_t flags) {
    ++revision_;
    changed_.emit(*this, flags);
}

void ColorPickerModel::set_color(Color color) {
    validate_color(color);
    const float maximum = std::max(color.r, std::max(color.g, color.b));
    const float minimum = std::min(color.r, std::min(color.g, color.b));
    const float delta = maximum - minimum;
    float hue = 0.0f;
    if (!same(delta, 0.0f)) {
        if (same(maximum, color.r))
            hue = std::fmod((color.g - color.b) / delta, 6.0f);
        else if (same(maximum, color.g))
            hue = (color.b - color.r) / delta + 2.0f;
        else
            hue = (color.r - color.g) / delta + 4.0f;
        hue /= 6.0f;
        if (hue < 0.0f)
            hue += 1.0f;
    }
    const float saturation = same(maximum, 0.0f) ? 0.0f : delta / maximum;
    if (same(hue_, hue) && same(saturation_, saturation) && same(value_, maximum) &&
        same(alpha_, color.a))
        return;
    const bool hue_changed = !same(hue_, hue);
    const bool rgb_changed =
        hue_changed || !same(saturation_, saturation) || !same(value_, maximum);
    hue_ = hue;
    saturation_ = saturation;
    value_ = maximum;
    alpha_ = color.a;
    uint32_t flags = color_picker_change_mask(ColorPickerChange::Color);
    if (hue_changed)
        flags |= color_picker_change_mask(ColorPickerChange::SvSurface);
    if (rgb_changed)
        flags |= color_picker_change_mask(ColorPickerChange::AlphaSurface);
    emit_change(flags);
}

void ColorPickerModel::set_hsv(float hue, float saturation, float value) {
    validate_unit(hue, "hue");
    validate_unit(saturation, "saturation");
    validate_unit(value, "value");
    if (same(hue_, hue) && same(saturation_, saturation) && same(value_, value))
        return;
    uint32_t flags = color_picker_change_mask(ColorPickerChange::Color) |
                     color_picker_change_mask(ColorPickerChange::AlphaSurface);
    if (!same(hue_, hue))
        flags |= color_picker_change_mask(ColorPickerChange::SvSurface);
    hue_ = hue;
    saturation_ = saturation;
    value_ = value;
    emit_change(flags);
}

void ColorPickerModel::set_hue(float hue) { set_hsv(hue, saturation_, value_); }

void ColorPickerModel::set_saturation(float saturation) { set_hsv(hue_, saturation, value_); }

void ColorPickerModel::set_value(float value) { set_hsv(hue_, saturation_, value); }

void ColorPickerModel::set_alpha(float alpha) {
    validate_unit(alpha, "alpha");
    if (same(alpha_, alpha))
        return;
    alpha_ = alpha;
    emit_change(color_picker_change_mask(ColorPickerChange::Color));
}

void ColorPickerModel::set_show_alpha(bool show_alpha) {
    if (show_alpha_ == show_alpha)
        return;
    show_alpha_ = show_alpha;
    emit_change(color_picker_change_mask(ColorPickerChange::Color));
}

} // namespace termin::gui_native
