#include <termin/gui_native/document_snapshot.hpp>

#include <stdexcept>
#include <utility>

namespace termin::gui_native {

DocumentSnapshot::DocumentSnapshot(tc_ui_document_handle document) {
    if (!tc_ui_document_capture_snapshot(document, &snapshot_)) {
        throw std::runtime_error("failed to capture native UI document snapshot");
    }
}

DocumentSnapshot::DocumentSnapshot(TcDocument document)
    : DocumentSnapshot(document.handle()) {}

DocumentSnapshot::~DocumentSnapshot() {
    tc_ui_document_snapshot_destroy(&snapshot_);
}

DocumentSnapshot::DocumentSnapshot(DocumentSnapshot&& other) noexcept : snapshot_(other.snapshot_) {
    other.snapshot_ = {};
}

DocumentSnapshot& DocumentSnapshot::operator=(DocumentSnapshot&& other) noexcept {
    if (this != &other) {
        tc_ui_document_snapshot_destroy(&snapshot_);
        snapshot_ = other.snapshot_;
        other.snapshot_ = {};
    }
    return *this;
}

const tc_ui_widget_snapshot* DocumentSnapshot::find(tc_widget_handle handle) const {
    for (const tc_ui_widget_snapshot& widget : widgets()) {
        if (tc_widget_handle_eq(widget.handle, handle)) {
            return &widget;
        }
    }
    return nullptr;
}

} // namespace termin::gui_native
