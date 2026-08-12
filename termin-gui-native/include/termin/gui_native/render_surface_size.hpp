#pragma once

namespace termin::gui_native {

    struct ViewportSurfaceSize {
        int width = 0;
        int height = 0;

        friend bool operator==(const ViewportSurfaceSize&, const ViewportSurfaceSize&) = default;
    };

} // namespace termin::gui_native
