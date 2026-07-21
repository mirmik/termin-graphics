#pragma once

#include <vector>

#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

enum class OverlayAnchor {
    Fill,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

struct OverlayPlacement {
    tc_widget_handle handle = tc_widget_handle_invalid();
    OverlayAnchor anchor = OverlayAnchor::Fill;
    tc_ui_point offset{};
};

class OverlayLayout : public NativeWidget {
private:
    std::vector<OverlayPlacement> placements_;

public:
    explicit OverlayLayout(const char* debug_name = nullptr);

    bool add_child(
        tc_widget_handle handle,
        OverlayAnchor anchor = OverlayAnchor::Fill,
        tc_ui_point offset = {});
    bool remove_child(tc_widget_handle handle);
    bool set_placement(
        tc_widget_handle handle,
        OverlayAnchor anchor,
        tc_ui_point offset = {});
    const OverlayPlacement* placement(tc_widget_handle handle) const;

    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document_handle document, tc_ui_rect rect) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_widget_handle hit_test(tc_ui_document_handle document, float x, float y) override;
};

} // namespace termin::gui_native
