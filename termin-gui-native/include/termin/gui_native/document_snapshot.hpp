#pragma once

#include <span>
#include <stdexcept>

#include <termin/gui_native/document.hpp>
#include <termin/gui_native/tc_ui_snapshot.h>

namespace termin::gui_native {

class DocumentSnapshot {
  private:
    tc_ui_document_inspect_snapshot snapshot_{};

  public:
    explicit DocumentSnapshot(const tc_ui_document* document) {
        if (!tc_ui_document_capture_snapshot(document, &snapshot_)) {
            throw std::runtime_error("failed to capture native UI document snapshot");
        }
    }

    explicit DocumentSnapshot(const Document& document) : DocumentSnapshot(document.get()) {}

    ~DocumentSnapshot() { tc_ui_document_snapshot_destroy(&snapshot_); }

    DocumentSnapshot(const DocumentSnapshot&) = delete;
    DocumentSnapshot& operator=(const DocumentSnapshot&) = delete;

    DocumentSnapshot(DocumentSnapshot&& other) noexcept : snapshot_(other.snapshot_) {
        other.snapshot_ = {};
    }

    DocumentSnapshot& operator=(DocumentSnapshot&& other) noexcept {
        if (this != &other) {
            tc_ui_document_snapshot_destroy(&snapshot_);
            snapshot_ = other.snapshot_;
            other.snapshot_ = {};
        }
        return *this;
    }

    const tc_ui_document_inspect_snapshot& data() const { return snapshot_; }

    std::span<const tc_ui_widget_snapshot> widgets() const {
        return {snapshot_.widgets, snapshot_.widget_count};
    }

    std::span<const tc_widget_handle> children() const {
        return {snapshot_.children, snapshot_.child_count};
    }

    std::span<const tc_widget_handle> roots() const {
        return {snapshot_.roots, snapshot_.root_count};
    }

    std::span<const tc_ui_overlay_snapshot> overlays() const {
        return {snapshot_.overlays, snapshot_.overlay_count};
    }

    const tc_ui_widget_snapshot* find(tc_widget_handle handle) const {
        for (const tc_ui_widget_snapshot& widget : widgets()) {
            if (tc_widget_handle_eq(widget.handle, handle)) {
                return &widget;
            }
        }
        return nullptr;
    }

};

} // namespace termin::gui_native
