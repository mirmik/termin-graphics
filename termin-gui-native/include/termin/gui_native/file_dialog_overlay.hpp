#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <termin/gui_native/dialog.hpp>
#include <termin/gui_native/file_dialog_model.hpp>

namespace termin::gui_native {

class CollectionModel;

class FileDialogOverlay final : public Dialog {
  private:
    struct Place {
        std::string label;
        std::string path;
    };

    FileDialogModel model_;
    std::shared_ptr<CollectionModel> entries_model_;
    std::shared_ptr<CollectionModel> places_model_;
    std::vector<Place> places_;
    std::string initial_directory_;
    std::optional<std::string> accepted_path_;
    tc_widget_handle path_input_handle_ = tc_widget_handle_invalid();
    tc_widget_handle back_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle forward_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle up_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle home_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle go_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle new_folder_button_handle_ = tc_widget_handle_invalid();
    tc_widget_handle places_list_handle_ = tc_widget_handle_invalid();
    tc_widget_handle list_handle_ = tc_widget_handle_invalid();
    tc_widget_handle filter_handle_ = tc_widget_handle_invalid();
    tc_widget_handle name_input_handle_ = tc_widget_handle_invalid();
    tc_widget_handle selection_label_handle_ = tc_widget_handle_invalid();
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
    bool show(tc_ui_document_handle document, tc_ui_rect viewport);
    Signal<FileDialogOverlay&, const std::optional<std::string>&>& path_finished() {
        return path_finished_;
    }

  protected:
    bool before_action(const DialogAction& action) override;

  private:
    bool ensure_content(tc_ui_document_handle document);
    void build_places();
    void sync_places();
    bool navigate_to_input();
    bool navigate_home();
    void create_new_folder();
    void sync_view();
    void sync_entries();
    void show_error(std::string message);
    void on_place_selected(const std::vector<size_t>& indices);
    void on_entry_selected(const std::vector<size_t>& indices);
    void on_entry_activated(size_t index);

};

} // namespace termin::gui_native
