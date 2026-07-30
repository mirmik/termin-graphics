#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

#include <termin/gui_native/document_renderer_export.h>
#include <termin/gui_native/tc_document.hpp>

namespace tgfx {
class RenderContext2;
}

namespace termin::gui_native {

class ColorPicker;

struct UiDocumentSubmission {
    TcDocument document;
    int priority = 0;
    std::uint64_t stable_identity = 0;
    tc_ui_presentation_metrics presentation_metrics{};
};

struct NativeDocumentPainterConfig {
    std::string font_path;
    int font_size = 14;
};

// Presentation-neutral renderer for borrowed native UI documents. The caller
// owns the frame, active render pass, attachments and presentation policy.
class TERMIN_GUI_NATIVE_RENDERER_API NativeDocumentPainter {
public:
    NativeDocumentPainter();
    explicit NativeDocumentPainter(NativeDocumentPainterConfig config);
    ~NativeDocumentPainter();

    NativeDocumentPainter(const NativeDocumentPainter&) = delete;
    NativeDocumentPainter& operator=(const NativeDocumentPainter&) = delete;
    NativeDocumentPainter(NativeDocumentPainter&&) = delete;
    NativeDocumentPainter& operator=(NativeDocumentPainter&&) = delete;

    bool set_default_font_path(
        const std::string& path,
        int default_size_px = 14
    );

    // Appends all valid documents with explicit presentation metrics to one
    // draw list in ascending (priority, stable_identity) order and renders it
    // into the caller's already-open pass. A submission is rejected when its
    // physical extent differs from width/height. Returns the number painted.
    std::size_t paint_documents(
        tgfx::RenderContext2& context,
        int width,
        int height,
        std::span<const UiDocumentSubmission> documents
    );

    void sync_color_picker_surfaces(
        tgfx::RenderContext2& context,
        ColorPicker& picker
    );
    void release_color_picker_surfaces(ColorPicker& picker);

    // Must be called while the render device is still alive.
    void release_gpu();
    void close();
    bool is_open() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace termin::gui_native
