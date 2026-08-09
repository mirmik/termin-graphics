#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <termin/gui_native/export.h>
#include <termin/gui_native/tc_ui_document.h>

namespace termin::gui_native {

    // Immutable catalog of backend-neutral UI icons. Icons are painted into the
    // logical draw list, so scaling and backend lowering remain presentation concerns.
    class TERMIN_GUI_NATIVE_API UiIconRegistry final {
    public:
        static const UiIconRegistry& builtin();

        bool contains(std::string_view icon_id) const noexcept;
        bool
        paint(tc_ui_paint_context* context, std::string_view icon_id, tc_ui_rect rect, tc_ui_srgb_color color) const;
        // Produces an exact-size white RGBA mask with supersampled alpha. The
        // renderer caches the uploaded result per device and physical size.
        std::vector<uint8_t> rasterize(std::string_view icon_id, uint32_t width, uint32_t height) const;

    private:
        UiIconRegistry() = default;
    };

} // namespace termin::gui_native
