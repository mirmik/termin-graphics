#pragma once

#include <span>
#include <termin/gui_native/document.hpp>
#include <termin/gui_native/tc_ui_snapshot.h>

namespace termin::gui_native {

class DocumentSnapshot {
  private:
    tc_ui_document_inspect_snapshot snapshot_{};

  public:
    TERMIN_GUI_NATIVE_API explicit DocumentSnapshot(tc_ui_document_handle document);

    TERMIN_GUI_NATIVE_API explicit DocumentSnapshot(const Document& document);

    TERMIN_GUI_NATIVE_API ~DocumentSnapshot();

    DocumentSnapshot(const DocumentSnapshot&) = delete;
    DocumentSnapshot& operator=(const DocumentSnapshot&) = delete;

    TERMIN_GUI_NATIVE_API DocumentSnapshot(DocumentSnapshot&& other) noexcept;
    TERMIN_GUI_NATIVE_API DocumentSnapshot& operator=(DocumentSnapshot&& other) noexcept;

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

    TERMIN_GUI_NATIVE_API const tc_ui_widget_snapshot* find(tc_widget_handle handle) const;

};

} // namespace termin::gui_native
