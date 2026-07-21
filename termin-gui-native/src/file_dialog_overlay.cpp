#include "widgets_internal.hpp"

#include <filesystem>
#include <stdexcept>

namespace termin::gui_native {

namespace {

template <typename T> T* resolve(tc_ui_document_handle document, tc_widget_handle handle) {
    tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
    return widget ? dynamic_cast<T*>(static_cast<Widget*>(widget->body)) : nullptr;
}

template <typename T>
T* adopt(tc_ui_document_handle document, std::unique_ptr<T> widget, tc_widget_handle& handle) {
    handle = tc_ui_document_adopt_widget(
        document, widget->c_widget(), &Widget::delete_owned_widget);
    if (tc_widget_handle_is_invalid(handle))
        return nullptr;
    return widget.release();
}

std::string accept_label(FileDialogMode mode) {
    switch (mode) {
    case FileDialogMode::OpenFile:
        return "Open";
    case FileDialogMode::SaveFile:
        return "Save";
    case FileDialogMode::OpenDirectory:
        return "Select";
    }
    return "Select";
}

std::string default_title(FileDialogMode mode) {
    switch (mode) {
    case FileDialogMode::OpenFile:
        return "Open file";
    case FileDialogMode::SaveFile:
        return "Save file";
    case FileDialogMode::OpenDirectory:
        return "Select directory";
    }
    return "Select path";
}

} // namespace

FileDialogOverlay::FileDialogOverlay(FileDialogMode mode,
                                     std::shared_ptr<FileDialogFileSystem> file_system)
    : Dialog(default_title(mode)), model_(mode, std::move(file_system)),
      entries_model_(std::make_shared<CollectionModel>()) {
    set_preferred_size(tc_ui_size{680.0f, 520.0f});
    set_actions({
        DialogAction{"accept", accept_label(mode), true, false},
        DialogAction{"cancel", "Cancel", false, true},
    });
    finished_connection_ = finished().connect([this](Dialog&, const DialogResult& result) {
        const std::optional<std::string> path =
            result.action_id == "accept" ? accepted_path_ : std::nullopt;
        path_finished_.emit(*this, path);
    });
}

void FileDialogOverlay::set_filters(std::vector<FileDialogFilter> filters) {
    if (open()) {
        tc_log_error("[termin-gui-native] FileDialogOverlay filters cannot change while open");
        throw std::logic_error("file dialog filters cannot change while open");
    }
    model_.set_filters(std::move(filters));
}

void FileDialogOverlay::set_initial_directory(std::string directory) {
    if (open()) {
        tc_log_error(
            "[termin-gui-native] FileDialogOverlay initial directory cannot change while open");
        throw std::logic_error("file dialog initial directory cannot change while open");
    }
    if (!detail::valid_utf8(directory)) {
        tc_log_error("[termin-gui-native] FileDialogOverlay rejected invalid UTF-8 directory");
        throw std::invalid_argument("file dialog directory must be valid UTF-8");
    }
    initial_directory_ = std::move(directory);
}

void FileDialogOverlay::set_file_name(std::string file_name) {
    model_.set_file_name(std::move(file_name));
    if (!tc_ui_document_handle_is_invalid(document())) {
        if (TextInput* input = resolve<TextInput>(document(), name_input_handle_))
            input->set_text(model_.file_name());
    }
}

bool FileDialogOverlay::ensure_content(tc_ui_document_handle document) {
    if (!tc_widget_handle_is_invalid(list_handle_) &&
        tc_ui_document_is_alive(document, list_handle_))
        return true;

    tc_widget_handle root_handle = tc_widget_handle_invalid();
    tc_widget_handle navigation_handle = tc_widget_handle_invalid();
    tc_widget_handle filter_row_handle = tc_widget_handle_invalid();
    tc_widget_handle name_row_handle = tc_widget_handle_invalid();
    tc_widget_handle filter_label_handle = tc_widget_handle_invalid();
    tc_widget_handle name_label_handle = tc_widget_handle_invalid();

    auto* root = adopt(document, std::make_unique<VStack>("file-dialog-content"), root_handle);
    auto* navigation =
        adopt(document, std::make_unique<HStack>("file-dialog-navigation"), navigation_handle);
    auto* back = adopt(document, std::make_unique<Button>("<"), back_button_handle_);
    auto* forward = adopt(document, std::make_unique<Button>(">"), forward_button_handle_);
    auto* up = adopt(document, std::make_unique<Button>("Up"), up_button_handle_);
    auto* path = adopt(document, std::make_unique<TextInput>(), path_input_handle_);
    auto* list = adopt(document, std::make_unique<ListWidget>(entries_model_), list_handle_);
    auto* filter_row =
        adopt(document, std::make_unique<HStack>("file-dialog-filter-row"), filter_row_handle);
    auto* filter_label =
        adopt(document, std::make_unique<Label>("Files of type:"), filter_label_handle);
    auto* filter = adopt(document, std::make_unique<ComboBox>(), filter_handle_);
    auto* name_row =
        adopt(document, std::make_unique<HStack>("file-dialog-name-row"), name_row_handle);
    auto* name_label = adopt(document, std::make_unique<Label>("File name:"), name_label_handle);
    auto* name =
        adopt(document, std::make_unique<TextInput>(model_.file_name()), name_input_handle_);
    auto* error = adopt(document, std::make_unique<Label>(""), error_label_handle_);

    if (!root || !navigation || !back || !forward || !up || !path || !list || !filter_row ||
        !filter_label || !filter || !name_row || !name_label || !name || !error) {
        tc_log_error("[termin-gui-native] FileDialogOverlay failed to adopt content widgets");
        const tc_widget_handle handles[] = {
            root_handle,         navigation_handle,   back_button_handle_, forward_button_handle_,
            up_button_handle_,   path_input_handle_,  list_handle_,        filter_row_handle,
            filter_label_handle, filter_handle_,      name_row_handle,     name_label_handle,
            name_input_handle_,  error_label_handle_,
        };
        for (tc_widget_handle handle : handles) {
            if (tc_ui_document_is_alive(document, handle))
                tc_ui_document_destroy_widget_recursive(document, handle);
        }
        return false;
    }

    root->set_spacing(8.0f);
    navigation->set_spacing(6.0f);
    filter_row->set_spacing(8.0f);
    name_row->set_spacing(8.0f);
    list->set_min_size(tc_ui_size{300.0f, 260.0f});
    error->set_color(Color{0.92f, 0.32f, 0.32f, 1.0f});

    navigation->add_fixed_child(*back, 38.0f);
    navigation->add_fixed_child(*forward, 38.0f);
    navigation->add_fixed_child(*up, 56.0f);
    navigation->add_flex_child(*path);
    filter_row->add_preferred_child(*filter_label);
    filter_row->add_flex_child(*filter);
    name_row->add_preferred_child(*name_label);
    name_row->add_flex_child(*name);
    root->add_preferred_child(*navigation);
    root->add_flex_child(*list);
    root->add_preferred_child(*filter_row);
    if (model_.mode() == FileDialogMode::SaveFile)
        root->add_preferred_child(*name_row);
    else
        tc_ui_document_destroy_widget_recursive(document, name_row_handle);
    root->add_preferred_child(*error);

    back->clicked().connect([this](Button&) {
        if (model_.go_back())
            sync_view();
        else if (!model_.error().empty())
            show_error(model_.error());
    });
    forward->clicked().connect([this](Button&) {
        if (model_.go_forward())
            sync_view();
        else if (!model_.error().empty())
            show_error(model_.error());
    });
    up->clicked().connect([this](Button&) {
        if (model_.go_up())
            sync_view();
        else if (!model_.error().empty())
            show_error(model_.error());
    });
    path->submitted().connect([this](TextInput&, const std::string&) { navigate_to_input(); });
    list->selection_changed().connect(
        [this](ListWidget&, const std::vector<size_t>& indices) { on_entry_selected(indices); });
    list->activated().connect(
        [this](ListWidget&, size_t index, const CollectionItem&) { on_entry_activated(index); });
    filter->changed().connect([this](ComboBox&, int index, const std::string&) {
        if (index >= 0 && model_.set_filter(static_cast<size_t>(index)))
            sync_entries();
        else if (!model_.error().empty())
            show_error(model_.error());
    });
    if (model_.mode() == FileDialogMode::SaveFile) {
        name->changed().connect(
            [this](TextInput&, const std::string& value) { model_.set_file_name(value); });
        name->submitted().connect(
            [this](TextInput&, const std::string&) { activate("accept", this->document()); });
    }

    set_content(*root);
    return true;
}

bool FileDialogOverlay::navigate_to_input() {
    TextInput* path = resolve<TextInput>(document(), path_input_handle_);
    if (!path)
        return false;
    if (!model_.navigate(path->text())) {
        show_error(model_.error());
        return false;
    }
    sync_view();
    return true;
}

void FileDialogOverlay::sync_entries() {
    std::vector<CollectionItem> items;
    items.reserve(model_.entries().size());
    for (const FileDialogEntry& entry : model_.entries()) {
        items.push_back(CollectionItem{
            entry.path, entry.is_directory ? "[Folder] " + entry.name : entry.name, {}, true, 0});
    }
    entries_model_->set_items(std::move(items));
}

void FileDialogOverlay::sync_view() {
    if (tc_ui_document_handle_is_invalid(document()))
        return;
    sync_entries();
    if (TextInput* path = resolve<TextInput>(document(), path_input_handle_))
        path->set_text(model_.current_directory());
    if (Button* back = resolve<Button>(document(), back_button_handle_))
        back->set_enabled(model_.can_go_back());
    if (Button* forward = resolve<Button>(document(), forward_button_handle_))
        forward->set_enabled(model_.can_go_forward());
    if (ComboBox* filter = resolve<ComboBox>(document(), filter_handle_)) {
        filter->clear_items();
        for (const FileDialogFilter& item : model_.filters())
            filter->add_item(item.label);
        filter->set_selected_index(static_cast<int>(model_.selected_filter()));
    }
    show_error(model_.error());
}

void FileDialogOverlay::show_error(std::string message) {
    if (tc_ui_document_handle_is_invalid(document()))
        return;
    if (Label* label = resolve<Label>(document(), error_label_handle_))
        label->set_text(std::move(message));
}

void FileDialogOverlay::on_entry_selected(const std::vector<size_t>& indices) {
    if (indices.empty())
        return;
    if (!model_.select(indices.front())) {
        show_error(model_.error());
        return;
    }
    if (model_.mode() == FileDialogMode::SaveFile) {
        if (TextInput* name = resolve<TextInput>(document(), name_input_handle_))
            name->set_text(model_.file_name());
    }
    show_error({});
}

void FileDialogOverlay::on_entry_activated(size_t index) {
    const bool was_directory =
        index < model_.entries().size() && model_.entries()[index].is_directory;
    FileDialogConfirmResult result;
    if (!model_.activate(index, result)) {
        show_error(result.error.empty() ? model_.error() : result.error);
        return;
    }
    if (was_directory) {
        sync_view();
        return;
    }
    if (result.path)
        accepted_path_ = std::move(result.path);
    activate("accept", document());
}

bool FileDialogOverlay::before_action(const DialogAction& action) {
    if (action.stable_id != "accept") {
        accepted_path_.reset();
        return true;
    }
    const FileDialogConfirmResult result = model_.confirm();
    if (!result.path) {
        show_error(result.error.empty() ? "select a path" : result.error);
        return false;
    }
    accepted_path_ = result.path;
    show_error({});
    return true;
}

bool FileDialogOverlay::show(tc_ui_document_handle document, tc_ui_rect viewport) {
    if (!ensure_content(document))
        return false;
    accepted_path_.reset();
    if (model_.current_directory().empty()) {
        const std::string start = initial_directory_.empty() ? "." : initial_directory_;
        if (!model_.navigate(start)) {
            show_error(model_.error());
            return false;
        }
    } else if (!model_.refresh()) {
        show_error(model_.error());
        return false;
    }
    sync_view();
    return Dialog::show(document, viewport);
}

} // namespace termin::gui_native
