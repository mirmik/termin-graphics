#pragma once

#include <memory>
#include <optional>
#include <string>

#include <termin/gui_native/dialog.hpp>
#include <termin/gui_native/file_dialog_model.hpp>

namespace termin::gui_native {

class CollectionModel;

class FileDialogOverlay final : public Dialog {
  private:
    FileDialogModel model_;
    std::shared_ptr<CollectionModel> entries_model_;
    std::string initial_directory_;
    std::optional<std::string> accepted_path_;
    tc_widget_handle path_input_handle_ = tc_widget_handle_invalid();
    tc_widget_handle back_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle forward_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle up_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle list_handle_ = tc_widget_handle_invalid();
    tc_widget_handle filter_handle_ = tc_widget_handle_invalid();
    tc_widget_handle name_input_handle_ = tc_widget_handle_invalid();
    tc_widget_handle error_label_handle_ = tc_widget_handle_invalid();
    size_t finished_connection_ = 0;
    Signal<FileDialogOverlay&, const std::optional<std::string>&> path_finished_;

  public:
    explicit FileDialogOverlay(FileDialogMode mode,
                               std::shared_ptr<FileDialogFileSystem> file_system = {});

    FileDialogModel& model() { return model_; }
    const FileDialogModel& model() const { return model_; }
    void set_filters(std::vector<FileDialogFilter> filters);
    void set_initial_directory(std::string directory);
    void set_file_name(std::string file_name);
    bool show(tc_ui_document* document, tc_ui_rect viewport);
    Signal<FileDialogOverlay&, const std::optional<std::string>&>& path_finished() {
        return path_finished_;
    }

  protected:
    bool before_action(const DialogAction& action) override;

  private:
    bool ensure_content(tc_ui_document* document);
    bool navigate_to_input();
    void sync_view();
    void sync_entries();
    void show_error(std::string message);
    void on_entry_selected(const std::vector<size_t>& indices);
    void on_entry_activated(size_t index);

};

} // namespace termin::gui_native
