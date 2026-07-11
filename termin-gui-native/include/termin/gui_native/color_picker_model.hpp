#pragma once

#include <cstdint>

#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {

enum class ColorPickerChange : uint32_t {
    Color = 1u << 0u,
    SvSurface = 1u << 1u,
    AlphaSurface = 1u << 2u,
};

class ColorPickerModel {
  private:
    Color initial_color_;
    float hue_ = 0.0f;
    float saturation_ = 0.0f;
    float value_ = 1.0f;
    float alpha_ = 1.0f;
    bool show_alpha_ = true;
    uint64_t revision_ = 1;
    Signal<ColorPickerModel&, uint32_t> changed_;

  public:
    explicit ColorPickerModel(Color initial = Color{1.0f, 1.0f, 1.0f, 1.0f},
                              bool show_alpha = true);

    Color color() const;
    Color initial_color() const { return initial_color_; }
    float hue() const { return hue_; }
    float saturation() const { return saturation_; }
    float value() const { return value_; }
    float alpha() const { return alpha_; }
    bool show_alpha() const { return show_alpha_; }
    uint64_t revision() const { return revision_; }

    void set_color(Color color);
    void set_hsv(float hue, float saturation, float value);
    void set_hue(float hue);
    void set_saturation(float saturation);
    void set_value(float value);
    void set_alpha(float alpha);
    void set_show_alpha(bool show_alpha);

    Signal<ColorPickerModel&, uint32_t>& changed() { return changed_; }

    static Color hsv_to_rgb(float hue, float saturation, float value, float alpha = 1.0f);

  private:
    static void validate_unit(float value, const char* field);
    static void validate_color(Color color);
    void emit_change(uint32_t flags);

};

constexpr uint32_t color_picker_change_mask(ColorPickerChange change) {
    return static_cast<uint32_t>(change);
}

} // namespace termin::gui_native
