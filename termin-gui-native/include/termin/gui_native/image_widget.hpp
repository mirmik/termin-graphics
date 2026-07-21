#pragma once

#include <cstdint>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/widget_types.hpp>

namespace termin::gui_native {
class ImageWidget : public NativeWidget {
public:
private:
    uint32_t texture_id_ = 0;
    tc_ui_size intrinsic_size_ {64.0f, 64.0f};
    Color tint_ {1.0f, 1.0f, 1.0f, 1.0f};
    ImageFit fit_ = ImageFit::Contain;
public:
    ImageWidget();
    void set_texture(uint32_t texture_id, tc_ui_size intrinsic_size = {});
    // Remove the sampled image while retaining its intrinsic layout size.  This
    // is important for asynchronous previews: hiding the widget would make a
    // parent BoxLayout reflow and shift neighbouring controls.
    void clear_texture();
    uint32_t texture_id() const { return texture_id_; }
    tc_ui_size intrinsic_size() const { return intrinsic_size_; }
    void set_tint(Color tint);
    ImageFit fit() const { return fit_; }
    void set_fit(ImageFit fit);
    void set_preserve_aspect(bool preserve) {
        set_fit(preserve ? ImageFit::Contain : ImageFit::Stretch);
    }
    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
};
} // namespace termin::gui_native
