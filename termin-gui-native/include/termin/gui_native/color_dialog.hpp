#pragma once

#include <memory>
#include <optional>

#include <termin/gui_native/color_picker.hpp>
#include <termin/gui_native/dialog.hpp>

namespace termin::gui_native {

class ColorDialog final : public Dialog {
  public:
    explicit ColorDialog(Color initial = Color{1.0f, 1.0f, 1.0f, 1.0f}, bool show_alpha = true,
                         std::string title = "Color Picker");

    const std::shared_ptr<ColorPickerModel>& model() const { return model_; }
    Color color() const { return model_->color(); }
    void set_color(Color color) { model_->set_color(color); }
    tc_widget_handle picker_handle() const { return picker_handle_; }
    bool show(tc_ui_document* document, tc_ui_rect viewport);
    Signal<ColorDialog&, const std::optional<Color>&>& color_finished() { return color_finished_; }

  protected:
    bool before_action(const DialogAction& action) override;

  private:
    bool ensure_content(tc_ui_document* document);

    std::shared_ptr<ColorPickerModel> model_;
    tc_widget_handle picker_handle_ = tc_widget_handle_invalid();
    std::optional<Color> accepted_color_;
    size_t finished_connection_ = 0;
    Signal<ColorDialog&, const std::optional<Color>&> color_finished_;
};

} // namespace termin::gui_native
