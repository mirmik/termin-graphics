#include "widgets_internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace termin::gui_native {

    namespace {

        namespace fs = std::filesystem;

        template <typename T> T* resolve(tc_ui_document_handle document, tc_widget_handle handle) {
            tc_widget* widget = tc_ui_document_resolve_widget(document, handle);
            return widget ? dynamic_cast<T*>(static_cast<Widget*>(widget->body)) : nullptr;
        }

        template <typename T>
        T* adopt(tc_ui_document_handle document, std::unique_ptr<T> widget, tc_widget_handle& handle) {
            handle = tc_ui_document_adopt_widget(document, widget->c_widget(), &Widget::delete_owned_widget);
            if (tc_widget_handle_is_invalid(handle))
                return nullptr;
            return widget.release();
        }

        std::string path_string(const fs::path& path) {
#if defined(_WIN32)
            const std::u8string value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
            return path.string();
#endif
        }

        fs::path make_path(std::string_view value) {
#if defined(_WIN32)
            std::u8string utf8;
            utf8.reserve(value.size());
            for (char character : value)
                utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
            return fs::path(utf8);
#else
            return fs::path(value);
#endif
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

        std::string join_patterns(const FileDialogFilter& filter) {
            std::string result;
            for (const std::string& pattern : filter.patterns) {
                if (!result.empty())
                    result += " ";
                result += pattern;
            }
            return result;
        }

        std::string format_size(uint64_t bytes) {
            static constexpr const char* units[] = {"B", "KB", "MB", "GB", "TB"};
            double value = static_cast<double>(bytes);
            size_t unit = 0;
            while (value >= 1024.0 && unit + 1 < std::size(units)) {
                value /= 1024.0;
                ++unit;
            }
            std::ostringstream stream;
            if (unit == 0)
                stream << bytes;
            else
                stream << std::fixed << std::setprecision(1) << value;
            stream << " " << units[unit];
            return stream.str();
        }

        std::string format_modified_time(int64_t seconds) {
            if (seconds <= 0)
                return {};
            const std::time_t value = static_cast<std::time_t>(seconds);
            std::tm local{};
#if defined(_WIN32)
            if (localtime_s(&local, &value) != 0)
                return {};
#else
            if (!localtime_r(&value, &local))
                return {};
#endif
            std::ostringstream stream;
            stream << std::put_time(&local, "%Y-%m-%d %H:%M");
            return stream.str();
        }

        std::string entry_subtitle(const FileDialogEntry& entry) {
            std::string result = entry.is_directory ? "Folder" : format_size(entry.size);
            const std::string modified = format_modified_time(entry.modified_time);
            if (!modified.empty())
                result += "   Modified: " + modified;
            return result;
        }

        std::string file_icon(std::string_view name) {
            std::string extension = path_string(make_path(name).extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
                extension == ".gif" || extension == ".webp" || extension == ".svg")
                return "image";
            if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac")
                return "audio";
            if (extension == ".mp4" || extension == ".mov" || extension == ".avi" || extension == ".mkv" ||
                extension == ".webm")
                return "video";
            if (extension == ".zip" || extension == ".tar" || extension == ".gz" || extension == ".7z" ||
                extension == ".rar")
                return "archive";
            if (extension == ".cpp" || extension == ".c" || extension == ".h" || extension == ".hpp" ||
                extension == ".py" || extension == ".js" || extension == ".ts" || extension == ".json" ||
                extension == ".toml" || extension == ".yaml" || extension == ".yml" || extension == ".glsl")
                return "code";
            if (extension == ".pdf")
                return "pdf";
            if (extension == ".csv" || extension == ".xls" || extension == ".xlsx")
                return "spreadsheet";
            if (extension == ".exe" || extension == ".bat" || extension == ".cmd" || extension == ".sh")
                return "exec";
            return "file";
        }

    } // namespace

    FileDialogOverlay::FileDialogOverlay(FileDialogMode mode, std::shared_ptr<FileDialogFileSystem> file_system)
        : Dialog(default_title(mode)),
          model_(mode, std::move(file_system)),
          entries_model_(std::make_shared<CollectionModel>()),
          places_model_(std::make_shared<CollectionModel>()) {
        set_preferred_size(tc_ui_size{1020.0f, 650.0f});
        set_actions({
            DialogAction{"accept", accept_label(mode), true, false},
            DialogAction{"cancel", "Cancel", false, true},
        });
        finished_connection_ = finished().connect([this](Dialog&, const DialogResult& result) {
            const std::optional<std::string> path = result.action_id == "accept" ? accepted_path_ : std::nullopt;
            path_finished_.emit(*this, path);
        });
    }

    void FileDialogOverlay::set_filters(std::vector<FileDialogFilter> filters) {
        if (open()) {
            tc_log_error("[termin-gui-native] FileDialogOverlay filters cannot change "
                         "while open");
            throw std::logic_error("file dialog filters cannot change while open");
        }
        model_.set_filters(std::move(filters));
    }

    void FileDialogOverlay::set_initial_directory(std::string directory) {
        if (open()) {
            tc_log_error("[termin-gui-native] FileDialogOverlay initial directory "
                         "cannot change while open");
            throw std::logic_error("file dialog initial directory cannot change while open");
        }
        if (!detail::valid_utf8(directory)) {
            tc_log_error("[termin-gui-native] FileDialogOverlay rejected invalid UTF-8 "
                         "directory");
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
        if (!tc_widget_handle_is_invalid(list_handle_) && tc_ui_document_is_alive(document, list_handle_))
            return true;

        tc_widget_handle root_handle = tc_widget_handle_invalid();
        tc_widget_handle navigation_handle = tc_widget_handle_invalid();
        tc_widget_handle body_handle = tc_widget_handle_invalid();
        tc_widget_handle places_panel_handle = tc_widget_handle_invalid();
        tc_widget_handle files_panel_handle = tc_widget_handle_invalid();
        tc_widget_handle places_title_handle = tc_widget_handle_invalid();
        tc_widget_handle files_title_handle = tc_widget_handle_invalid();
        tc_widget_handle filter_row_handle = tc_widget_handle_invalid();
        tc_widget_handle name_row_handle = tc_widget_handle_invalid();
        tc_widget_handle filter_label_handle = tc_widget_handle_invalid();
        tc_widget_handle name_label_handle = tc_widget_handle_invalid();

        auto* root = adopt(document, std::make_unique<VStack>("file-dialog-content"), root_handle);
        auto* navigation = adopt(document, std::make_unique<HStack>("file-dialog-navigation"), navigation_handle);
        auto* body = adopt(document, std::make_unique<HStack>("file-dialog-body"), body_handle);
        auto* places_panel = adopt(document, std::make_unique<VStack>("file-dialog-places"), places_panel_handle);
        auto* files_panel = adopt(document, std::make_unique<VStack>("file-dialog-files"), files_panel_handle);
        auto* places_title = adopt(document, std::make_unique<Label>("Places"), places_title_handle);
        auto* files_title = adopt(document, std::make_unique<Label>("Directory contents"), files_title_handle);
        auto* back = adopt(document, std::make_unique<Button>("<"), back_button_handle_);
        auto* forward = adopt(document, std::make_unique<Button>(">"), forward_button_handle_);
        auto* up = adopt(document, std::make_unique<Button>("^"), up_button_handle_);
        auto* home = adopt(document, std::make_unique<Button>("Home"), home_button_handle_);
        auto* path = adopt(document, std::make_unique<TextInput>(), path_input_handle_);
        auto* go = adopt(document, std::make_unique<Button>("Go"), go_button_handle_);
        auto* new_folder = adopt(document, std::make_unique<Button>("New Folder"), new_folder_button_handle_);
        auto* places = adopt(document, std::make_unique<ListWidget>(places_model_), places_list_handle_);
        auto* list = adopt(document, std::make_unique<ListWidget>(entries_model_), list_handle_);
        auto* selection = adopt(document, std::make_unique<Label>("Selection: none"), selection_label_handle_);
        auto* filter_row = adopt(document, std::make_unique<HStack>("file-dialog-filter-row"), filter_row_handle);
        auto* filter_label = adopt(document, std::make_unique<Label>("File type:"), filter_label_handle);
        auto* filter = adopt(document, std::make_unique<ComboBox>(), filter_handle_);
        auto* name_row = adopt(document, std::make_unique<HStack>("file-dialog-name-row"), name_row_handle);
        auto* name_label = adopt(document, std::make_unique<Label>("File name:"), name_label_handle);
        auto* name = adopt(document, std::make_unique<TextInput>(model_.file_name()), name_input_handle_);
        auto* error = adopt(document, std::make_unique<Label>(""), error_label_handle_);

        if (!root || !navigation || !body || !places_panel || !files_panel || !places_title || !files_title || !back ||
            !forward || !up || !home || !path || !go || !new_folder || !places || !list || !selection || !filter_row ||
            !filter_label || !filter || !name_row || !name_label || !name || !error) {
            tc_log_error("[termin-gui-native] FileDialogOverlay failed to adopt "
                         "content widgets");
            const tc_widget_handle handles[] = {
                root_handle,
                navigation_handle,
                body_handle,
                places_panel_handle,
                files_panel_handle,
                places_title_handle,
                files_title_handle,
                back_button_handle_,
                forward_button_handle_,
                up_button_handle_,
                home_button_handle_,
                path_input_handle_,
                go_button_handle_,
                new_folder_button_handle_,
                places_list_handle_,
                list_handle_,
                selection_label_handle_,
                filter_row_handle,
                filter_label_handle,
                filter_handle_,
                name_row_handle,
                name_label_handle,
                name_input_handle_,
                error_label_handle_,
            };
            for (tc_widget_handle handle : handles) {
                if (tc_ui_document_is_alive(document, handle))
                    tc_ui_document_destroy_widget_recursive(document, handle);
            }
            return false;
        }

        root->set_min_size(tc_ui_size{960.0f, 480.0f});
        root->set_spacing(10.0f);
        navigation->set_spacing(6.0f);
        body->set_spacing(10.0f);
        places_panel->set_padding(EdgeInsets{10.0f, 10.0f, 10.0f, 10.0f})
            .set_spacing(8.0f)
            .set_background(SrgbColor{0.10f, 0.11f, 0.13f, 0.96f})
            .set_border(SrgbColor{0.25f, 0.27f, 0.31f, 1.0f}, 1.0f)
            .set_corner_radius(6.0f);
        files_panel->set_padding(EdgeInsets{10.0f, 10.0f, 10.0f, 10.0f})
            .set_spacing(8.0f)
            .set_background(SrgbColor{0.12f, 0.13f, 0.16f, 0.96f})
            .set_border(SrgbColor{0.28f, 0.30f, 0.35f, 1.0f}, 1.0f)
            .set_corner_radius(6.0f);
        filter_row->set_spacing(8.0f);
        name_row->set_spacing(8.0f);
        places->set_row_height(42.0f);
        list->set_row_height(44.0f);
        places->set_min_size(tc_ui_size{210.0f, 300.0f});
        list->set_min_size(tc_ui_size{480.0f, 300.0f});
        selection->set_color(SrgbColor{0.68f, 0.72f, 0.79f, 1.0f});
        error->set_color(SrgbColor{0.92f, 0.32f, 0.32f, 1.0f});

        navigation->add_fixed_child(*back, 34.0f);
        navigation->add_fixed_child(*forward, 34.0f);
        navigation->add_fixed_child(*up, 34.0f);
        navigation->add_fixed_child(*home, 62.0f);
        navigation->add_flex_child(*path);
        navigation->add_fixed_child(*go, 52.0f);
        navigation->add_fixed_child(*new_folder, 112.0f);
        places_panel->add_preferred_child(*places_title);
        places_panel->add_flex_child(*places);
        files_panel->add_preferred_child(*files_title);
        files_panel->add_flex_child(*list);
        files_panel->add_preferred_child(*selection);
        body->add_fixed_child(*places_panel, 245.0f);
        body->add_flex_child(*files_panel);
        filter_row->add_fixed_child(*filter_label, 82.0f);
        filter_row->add_flex_child(*filter);
        name_row->add_fixed_child(*name_label, 82.0f);
        name_row->add_flex_child(*name);
        root->add_preferred_child(*navigation);
        root->add_flex_child(*body);
        if (model_.mode() == FileDialogMode::SaveFile)
            root->add_preferred_child(*name_row);
        else
            tc_ui_document_destroy_widget_recursive(document, name_row_handle);
        if (model_.mode() != FileDialogMode::OpenDirectory)
            root->add_preferred_child(*filter_row);
        else
            tc_ui_document_destroy_widget_recursive(document, filter_row_handle);
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
        home->clicked().connect([this](Button&) { navigate_home(); });
        go->clicked().connect([this](Button&) { navigate_to_input(); });
        new_folder->clicked().connect([this](Button&) { create_new_folder(); });
        path->submitted().connect([this](TextInput&, const std::string&) { navigate_to_input(); });
        places->selection_changed().connect(
            [this](ListWidget&, const std::vector<size_t>& indices) { on_place_selected(indices); });
        places->activated().connect([this](ListWidget&, size_t index, const CollectionItem&) {
            on_place_selected(std::vector<size_t>{index});
        });
        list->selection_changed().connect(
            [this](ListWidget&, const std::vector<size_t>& indices) { on_entry_selected(indices); });
        list->activated().connect(
            [this](ListWidget&, size_t index, const CollectionItem&) { on_entry_activated(index); });
        if (model_.mode() != FileDialogMode::OpenDirectory) {
            filter->changed().connect([this](ComboBox&, int index, const std::string&) {
                if (index >= 0 && model_.set_filter(static_cast<size_t>(index))) {
                    sync_entries();
                    if (Label* selection = resolve<Label>(this->document(), selection_label_handle_))
                        selection->set_text("Selection: none");
                    show_error({});
                } else if (!model_.error().empty())
                    show_error(model_.error());
            });
        }
        if (model_.mode() == FileDialogMode::SaveFile) {
            name->changed().connect([this](TextInput&, const std::string& value) { model_.set_file_name(value); });
            name->submitted().connect([this](TextInput&, const std::string&) { activate("accept", this->document()); });
        }

        set_content(*root);
        return true;
    }

    void FileDialogOverlay::build_places() {
        places_.clear();
        const auto add_place = [this](std::string label, const fs::path& path) {
            std::error_code error;
            fs::path normalized = fs::weakly_canonical(path, error);
            if (error)
                normalized = path.lexically_normal();
            if (!fs::is_directory(normalized, error) || error)
                return;
            const std::string value = path_string(normalized);
            for (const Place& place : places_) {
                if (place.path == value)
                    return;
            }
            places_.push_back(Place{std::move(label), value});
        };

        std::error_code error;
        const fs::path home =
#if defined(_WIN32)
            std::getenv("USERPROFILE") ? fs::path(std::getenv("USERPROFILE")) : fs::path{};
#else
            std::getenv("HOME") ? fs::path(std::getenv("HOME")) : fs::path{};
#endif
        if (!home.empty())
            add_place("Home", home);
        if (!initial_directory_.empty())
            add_place("Project", make_path(initial_directory_));
        else
            add_place("Project", fs::current_path(error));
        if (!home.empty()) {
            for (const char* folder : {"Desktop", "Documents", "Downloads", "Pictures"})
                add_place(folder, home / folder);
        }
        add_place("Root", fs::current_path(error).root_path());
        add_place("Temp", fs::temp_directory_path(error));
#if !defined(_WIN32)
        for (const fs::path& mount_root : {fs::path("/media"), fs::path("/mnt")}) {
            error.clear();
            for (fs::directory_iterator iterator(mount_root, error), end; !error && iterator != end;
                 iterator.increment(error)) {
                if (iterator->is_directory(error) && !error)
                    add_place(path_string(iterator->path().filename()), iterator->path());
            }
        }
#endif
        sync_places();
    }

    void FileDialogOverlay::sync_places() {
        std::vector<CollectionItem> items;
        items.reserve(places_.size());
        for (const Place& place : places_)
            items.emplace_back(place.path, place.label, place.path, true, 0, "folder");
        places_model_->set_items(std::move(items));
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

    bool FileDialogOverlay::navigate_home() {
        for (const Place& place : places_) {
            if (place.label != "Home")
                continue;
            if (model_.navigate(place.path)) {
                sync_view();
                return true;
            }
            show_error(model_.error());
            return false;
        }
        show_error("Home directory is not available");
        return false;
    }

    void FileDialogOverlay::create_new_folder() {
        if (!model_.create_unique_directory()) {
            show_error(model_.error());
            return;
        }
        sync_view();
    }

    void FileDialogOverlay::sync_entries() {
        std::vector<CollectionItem> items;
        items.reserve(model_.entries().size());
        for (const FileDialogEntry& entry : model_.entries()) {
            items.emplace_back(entry.path,
                               entry.name,
                               entry_subtitle(entry),
                               true,
                               0,
                               entry.is_directory ? "folder" : file_icon(entry.name));
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
        if (Button* up = resolve<Button>(document(), up_button_handle_)) {
            const fs::path current = make_path(model_.current_directory());
            up->set_enabled(!current.empty() && current.parent_path() != current);
        }
        if (Label* selection = resolve<Label>(document(), selection_label_handle_))
            selection->set_text("Selection: none");
        if (ComboBox* filter = resolve<ComboBox>(document(), filter_handle_)) {
            filter->clear_items();
            for (const FileDialogFilter& item : model_.filters())
                filter->add_item(item.label + " (" + join_patterns(item) + ")");
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

    void FileDialogOverlay::on_place_selected(const std::vector<size_t>& indices) {
        if (indices.empty() || indices.front() >= places_.size())
            return;
        if (!model_.navigate(places_[indices.front()].path)) {
            show_error(model_.error());
            return;
        }
        sync_view();
    }

    void FileDialogOverlay::on_entry_selected(const std::vector<size_t>& indices) {
        if (indices.empty()) {
            if (Label* selection = resolve<Label>(document(), selection_label_handle_))
                selection->set_text("Selection: none");
            return;
        }
        if (!model_.select(indices.front())) {
            show_error(model_.error());
            return;
        }
        const FileDialogEntry& entry = model_.entries()[indices.front()];
        if (Label* selection = resolve<Label>(document(), selection_label_handle_))
            selection->set_text("Selection: " + entry.path);
        if (model_.mode() == FileDialogMode::SaveFile && !entry.is_directory) {
            if (TextInput* name = resolve<TextInput>(document(), name_input_handle_))
                name->set_text(model_.file_name());
        }
        show_error({});
    }

    void FileDialogOverlay::on_entry_activated(size_t index) {
        const bool was_directory = index < model_.entries().size() && model_.entries()[index].is_directory;
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
        build_places();
        sync_view();
        return Dialog::show(document, viewport);
    }

} // namespace termin::gui_native
