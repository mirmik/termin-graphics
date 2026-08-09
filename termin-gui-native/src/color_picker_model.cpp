#include "widgets_internal.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

namespace termin::gui_native {

    namespace {

        bool same(float left, float right) {
            return std::fabs(left - right) <= 1.0e-6f;
        }

        uint8_t parse_hex_byte(std::string_view text, size_t offset) {
            auto digit = [](char value) -> int {
                if (value >= '0' && value <= '9')
                    return value - '0';
                if (value >= 'a' && value <= 'f')
                    return value - 'a' + 10;
                if (value >= 'A' && value <= 'F')
                    return value - 'A' + 10;
                return -1;
            };
            const int high = digit(text[offset]);
            const int low = digit(text[offset + 1]);
            if (high < 0 || low < 0)
                throw std::invalid_argument("color picker hex contains a non-hex digit");
            return static_cast<uint8_t>((high << 4) | low);
        }

        uint8_t quantize_srgb(float value) {
            return static_cast<uint8_t>(std::lround(value * 255.0f));
        }

    } // namespace

    ColorPickerModel::ColorPickerModel(SrgbColor initial, bool show_alpha)
        : initial_color_(initial),
          show_alpha_(show_alpha) {
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

    void ColorPickerModel::validate_color(SrgbColor color) {
        validate_unit(color.r, "red");
        validate_unit(color.g, "green");
        validate_unit(color.b, "blue");
        validate_unit(color.a, "alpha");
    }

    SrgbColor ColorPickerModel::hsv_to_rgb(float hue, float saturation, float value, float alpha) {
        validate_unit(hue, "hue");
        validate_unit(saturation, "saturation");
        validate_unit(value, "value");
        validate_unit(alpha, "alpha");
        if (same(saturation, 0.0f))
            return SrgbColor{value, value, value, alpha};
        const float scaled = (hue >= 1.0f ? 0.0f : hue) * 6.0f;
        const int sector = static_cast<int>(std::floor(scaled));
        const float fraction = scaled - static_cast<float>(sector);
        const float p = value * (1.0f - saturation);
        const float q = value * (1.0f - saturation * fraction);
        const float t = value * (1.0f - saturation * (1.0f - fraction));
        switch (sector) {
        case 0:
            return SrgbColor{value, t, p, alpha};
        case 1:
            return SrgbColor{q, value, p, alpha};
        case 2:
            return SrgbColor{p, value, t, alpha};
        case 3:
            return SrgbColor{p, q, value, alpha};
        case 4:
            return SrgbColor{t, p, value, alpha};
        default:
            return SrgbColor{value, p, q, alpha};
        }
    }

    SrgbColor ColorPickerModel::color() const {
        return hsv_to_rgb(hue_, saturation_, value_, show_alpha_ ? alpha_ : 1.0f);
    }

    termin::SrgbColor ColorPickerModel::srgb_color() const {
        const SrgbColor value = color();
        return termin::SrgbColor{value.r, value.g, value.b, value.a};
    }

    void ColorPickerModel::set_srgb_color(termin::SrgbColor color) {
        set_color(SrgbColor{color.r, color.g, color.b, color.a});
    }

    std::string ColorPickerModel::hex() const {
        return srgb_to_hex(srgb_color());
    }

    void ColorPickerModel::set_hex(std::string_view value) {
        const termin::SrgbColor color = srgb_from_hex(value);
        set_srgb_color(color);
    }

    termin::SrgbColor ColorPickerModel::srgb_from_hex(std::string_view value) {
        if (!value.empty() && value.front() == '#')
            value.remove_prefix(1);
        if (value.size() != 6 && value.size() != 8) {
            tc_log_error("[termin-gui-native] ColorPickerModel rejected hex color with invalid length");
            throw std::invalid_argument("color picker hex must contain 6 or 8 digits");
        }
        try {
            const uint8_t red = parse_hex_byte(value, 0);
            const uint8_t green = parse_hex_byte(value, 2);
            const uint8_t blue = parse_hex_byte(value, 4);
            const uint8_t alpha = value.size() == 8 ? parse_hex_byte(value, 6) : 255;
            return termin::SrgbColor{red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f};
        } catch (const std::invalid_argument&) {
            tc_log_error("[termin-gui-native] ColorPickerModel rejected invalid hex color");
            throw;
        }
    }

    std::string ColorPickerModel::srgb_to_hex(termin::SrgbColor color) {
        validate_unit(color.r, "sRGB red");
        validate_unit(color.g, "sRGB green");
        validate_unit(color.b, "sRGB blue");
        validate_unit(color.a, "sRGB alpha");
        const char digits[] = "0123456789ABCDEF";
        const std::array<uint8_t, 4> channels = {
            quantize_srgb(color.r), quantize_srgb(color.g), quantize_srgb(color.b), quantize_srgb(color.a)};
        std::string result = "#";
        result.reserve(9);
        for (size_t index = 0; index < (color.a < 1.0f ? 4 : 3); ++index) {
            result.push_back(digits[channels[index] >> 4]);
            result.push_back(digits[channels[index] & 0x0f]);
        }
        return result;
    }

    void ColorPickerModel::emit_change(uint32_t flags) {
        ++revision_;
        changed_.emit(*this, flags);
    }

    void ColorPickerModel::set_color(SrgbColor color) {
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
        if (same(hue_, hue) && same(saturation_, saturation) && same(value_, maximum) && same(alpha_, color.a))
            return;
        const bool hue_changed = !same(hue_, hue);
        const bool rgb_changed = hue_changed || !same(saturation_, saturation) || !same(value_, maximum);
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

    void ColorPickerModel::set_hue(float hue) {
        set_hsv(hue, saturation_, value_);
    }

    void ColorPickerModel::set_saturation(float saturation) {
        set_hsv(hue_, saturation, value_);
    }

    void ColorPickerModel::set_value(float value) {
        set_hsv(hue_, saturation_, value);
    }

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
