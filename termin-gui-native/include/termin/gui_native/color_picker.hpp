#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <termin/gui_native/color_picker_model.hpp>
#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

enum class ColorPickerSurfaceKind { SaturationValue, Hue, Alpha };

struct ColorPickerSurface {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t revision = 0;
    std::vector<uint8_t> rgba;
};

struct ColorPickerTextureIds {
    uint32_t saturation_value = 0;
    uint32_t hue = 0;
    uint32_t alpha = 0;
};

class ColorPicker : public NativeWidget {
  public:
    explicit ColorPicker(std::shared_ptr<ColorPickerModel> model = {});
    ~ColorPicker() override;

    const std::shared_ptr<ColorPickerModel>& model() const { return model_; }
    void set_model(std::shared_ptr<ColorPickerModel> model);
    ColorPickerTextureIds texture_ids() const { return texture_ids_; }
    void set_texture_ids(ColorPickerTextureIds texture_ids);
    const ColorPickerSurface& surface(ColorPickerSurfaceKind kind);
    Signal<ColorPicker&, uint32_t>& surfaces_invalidated() { return surfaces_invalidated_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    void on_destroy(tc_ui_document* document) override;

  private:
    enum class DragTarget { None, SaturationValue, Hue, Alpha };

    void connect_model();
    void disconnect_model();
    void on_model_changed(uint32_t flags);
    void generate_surface(ColorPickerSurfaceKind kind);
    void apply_pointer(float x, float y);
    tc_ui_rect sv_rect() const;
    tc_ui_rect hue_rect() const;
    tc_ui_rect alpha_rect() const;
    tc_ui_rect preview_rect() const;
    void paint_checker(tc_ui_paint_context* context, tc_ui_rect rect) const;
    void paint_fallback_surfaces(tc_ui_paint_context* context) const;

    std::shared_ptr<ColorPickerModel> model_;
    size_t model_connection_ = 0;
    ColorPickerSurface sv_surface_;
    ColorPickerSurface hue_surface_;
    ColorPickerSurface alpha_surface_;
    ColorPickerTextureIds texture_ids_;
    DragTarget dragging_ = DragTarget::None;
    tc_ui_rect content_rect_{};
    float surface_size_ = 180.0f;
    float bar_width_ = 20.0f;
    float gap_ = 10.0f;
    float preview_height_ = 26.0f;
    float label_height_ = 20.0f;
    Signal<ColorPicker&, uint32_t> surfaces_invalidated_;
};

} // namespace termin::gui_native
