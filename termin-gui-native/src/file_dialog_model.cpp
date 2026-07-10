#include "widgets_internal.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <stdexcept>

#include <tcbase/tc_log.h>

namespace termin::gui_native {
namespace fs = std::filesystem;

namespace {

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

std::string lower_ascii(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (char character : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return result;
}

bool glob_matches(std::string_view text, std::string_view pattern) {
    const std::string value = lower_ascii(text);
    const std::string glob = lower_ascii(pattern);
    size_t value_index = 0;
    size_t pattern_index = 0;
    size_t star_index = std::string::npos;
    size_t retry_index = 0;
    while (value_index < value.size()) {
        if (pattern_index < glob.size() &&
            (glob[pattern_index] == '?' || glob[pattern_index] == value[value_index])) {
            ++value_index;
            ++pattern_index;
        } else if (pattern_index < glob.size() && glob[pattern_index] == '*') {
            star_index = pattern_index++;
            retry_index = value_index;
        } else if (star_index != std::string::npos) {
            pattern_index = star_index + 1;
            value_index = ++retry_index;
        } else {
            return false;
        }
    }
    while (pattern_index < glob.size() && glob[pattern_index] == '*')
        ++pattern_index;
    return pattern_index == glob.size();
}

int64_t modified_seconds(const fs::file_time_type& time) {
    return std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count();
}

} // namespace

std::string NativeFileDialogFileSystem::normalize(std::string_view path, std::string_view base,
                                                  std::string& error) const {
    error.clear();
    if (!detail::valid_utf8(path) || !detail::valid_utf8(base)) {
        error = "path is not valid UTF-8";
        tc_log_error("[termin-gui-native] file dialog rejected invalid UTF-8 path");
        return {};
    }
    try {
        fs::path value = make_path(path);
        if (value.empty())
            value = make_path(base);
        if (value.is_relative())
            value = make_path(base) / value;
        return path_string(fs::weakly_canonical(value).lexically_normal());
    } catch (const std::exception& exception) {
        error = exception.what();
        tc_log_error("[termin-gui-native] file dialog failed to normalize path: %s",
                     exception.what());
        return {};
    }
}

bool NativeFileDialogFileSystem::inspect(std::string_view path, FileDialogEntry& entry,
                                         std::string& error) const {
    error.clear();
    try {
        const fs::path value = make_path(path);
        const fs::file_status status = fs::status(value);
        if (!fs::exists(status)) {
            error = "path does not exist";
            return false;
        }
        if (!fs::is_directory(status) && !fs::is_regular_file(status)) {
            error = "path is not a regular file or directory";
            return false;
        }
        entry.name = path_string(value.filename());
        entry.path = path_string(value);
        if (!detail::valid_utf8(entry.name) || !detail::valid_utf8(entry.path)) {
            error = "path is not valid UTF-8";
            tc_log_error("[termin-gui-native] file dialog cannot expose invalid UTF-8 path");
            return false;
        }
        entry.is_directory = fs::is_directory(status);
        entry.size = entry.is_directory ? 0 : static_cast<uint64_t>(fs::file_size(value));
        entry.modified_time = modified_seconds(fs::last_write_time(value));
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        tc_log_error("[termin-gui-native] file dialog failed to inspect path: %s",
                     exception.what());
        return false;
    }
}

std::vector<FileDialogEntry> NativeFileDialogFileSystem::list(std::string_view directory,
                                                              std::string& error) const {
    error.clear();
    std::vector<FileDialogEntry> result;
    try {
        for (const fs::directory_entry& item : fs::directory_iterator(make_path(directory))) {
            std::error_code status_error;
            const fs::file_status status = item.symlink_status(status_error);
            if (status_error || (!fs::is_directory(status) && !fs::is_regular_file(status)))
                continue;
            FileDialogEntry entry;
            entry.name = path_string(item.path().filename());
            entry.path = path_string(item.path());
            if (!detail::valid_utf8(entry.name) || !detail::valid_utf8(entry.path)) {
                tc_log_error(
                    "[termin-gui-native] file dialog skipped invalid UTF-8 directory entry");
                continue;
            }
            entry.is_directory = fs::is_directory(status);
            if (!entry.is_directory) {
                std::error_code size_error;
                entry.size = item.file_size(size_error);
                if (size_error)
                    entry.size = 0;
            }
            std::error_code time_error;
            const fs::file_time_type time = item.last_write_time(time_error);
            if (!time_error)
                entry.modified_time = modified_seconds(time);
            result.push_back(std::move(entry));
        }
    } catch (const std::exception& exception) {
        error = exception.what();
        tc_log_error("[termin-gui-native] file dialog failed to list directory: %s",
                     exception.what());
        result.clear();
    }
    return result;
}

bool NativeFileDialogFileSystem::create_directory(std::string_view path, std::string& error) {
    error.clear();
    try {
        if (!fs::create_directory(make_path(path))) {
            error = "directory already exists or could not be created";
            return false;
        }
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        tc_log_error("[termin-gui-native] file dialog failed to create directory: %s",
                     exception.what());
        return false;
    }
}

FileDialogModel::FileDialogModel(FileDialogMode mode,
                                 std::shared_ptr<FileDialogFileSystem> file_system)
    : mode_(mode), file_system_(file_system ? std::move(file_system)
                                            : std::make_shared<NativeFileDialogFileSystem>()) {}

std::vector<FileDialogFilter> FileDialogModel::parse_filter_string(std::string_view text) {
    if (!detail::valid_utf8(text)) {
        tc_log_error("[termin-gui-native] file dialog rejected invalid UTF-8 filter string");
        throw std::invalid_argument("file dialog filter string must be valid UTF-8");
    }
    std::vector<FileDialogFilter> result;
    size_t part_start = 0;
    while (part_start <= text.size()) {
        const size_t part_end = text.find(";;", part_start);
        const std::string_view raw =
            text.substr(part_start, part_end == std::string_view::npos ? text.size() - part_start
                                                                       : part_end - part_start);
        const size_t first = raw.find_first_not_of(" \t\r\n");
        if (first != std::string_view::npos) {
            const size_t last = raw.find_last_not_of(" \t\r\n");
            const std::string_view part = raw.substr(first, last - first + 1);
            const size_t separator = part.find('|');
            FileDialogFilter filter;
            const std::string_view label =
                separator == std::string_view::npos ? part : part.substr(0, separator);
            const size_t label_first = label.find_first_not_of(" \t");
            const size_t label_last = label.find_last_not_of(" \t");
            filter.label =
                label_first == std::string_view::npos
                    ? "Files"
                    : std::string(label.substr(label_first, label_last - label_first + 1));
            if (separator == std::string_view::npos) {
                filter.patterns.push_back("*");
            } else {
                const std::string_view patterns = part.substr(separator + 1);
                size_t offset = 0;
                while (offset < patterns.size()) {
                    offset = patterns.find_first_not_of(" \t", offset);
                    if (offset == std::string_view::npos)
                        break;
                    const size_t end = patterns.find_first_of(" \t", offset);
                    filter.patterns.emplace_back(patterns.substr(
                        offset,
                        end == std::string_view::npos ? patterns.size() - offset : end - offset));
                    offset = end == std::string_view::npos ? patterns.size() : end;
                }
                if (filter.patterns.empty())
                    filter.patterns.push_back("*");
            }
            result.push_back(std::move(filter));
        }
        if (part_end == std::string_view::npos)
            break;
        part_start = part_end + 2;
    }
    if (result.empty())
        result.push_back(FileDialogFilter{"All files", {"*"}});
    return result;
}

bool FileDialogModel::matches_filter(std::string_view name, const FileDialogFilter& filter) {
    for (const std::string& pattern : filter.patterns) {
        if (pattern == "*" || pattern == "*.*" || glob_matches(name, pattern))
            return true;
    }
    return false;
}

void FileDialogModel::set_filters(std::vector<FileDialogFilter> filters) {
    for (const FileDialogFilter& filter : filters) {
        const bool invalid_pattern = std::any_of(
            filter.patterns.begin(), filter.patterns.end(),
            [](const std::string& item) { return item.empty() || !detail::valid_utf8(item); });
        if (filter.label.empty() || !detail::valid_utf8(filter.label) || filter.patterns.empty() ||
            invalid_pattern) {
            tc_log_error("[termin-gui-native] file dialog rejected incomplete filter");
            throw std::invalid_argument("file dialog filters require label and patterns");
        }
    }
    if (filters.empty())
        filters_ = {{"All files", {"*"}}};
    else
        filters_ = std::move(filters);
    selected_filter_ = 0;
    if (!current_directory_.empty())
        refresh();
}

bool FileDialogModel::set_filter(size_t index) {
    if (index >= filters_.size()) {
        set_error("filter index out of range");
        return false;
    }
    selected_filter_ = index;
    return current_directory_.empty() || refresh();
}

bool FileDialogModel::navigate(std::string_view path, bool push_history) {
    if (!detail::valid_utf8(path)) {
        set_error("path is not valid UTF-8");
        return false;
    }
    std::string error;
    const std::string normalized = file_system_->normalize(path, current_directory_, error);
    if (normalized.empty()) {
        set_error(error.empty() ? "failed to normalize path" : std::move(error));
        return false;
    }
    return navigate_normalized(normalized, push_history);
}

bool FileDialogModel::navigate_normalized(std::string normalized, bool push_history) {
    FileDialogEntry entry;
    std::string error;
    if (!file_system_->inspect(normalized, entry, error) || !entry.is_directory) {
        set_error(error.empty() ? "path is not an available directory" : std::move(error));
        return false;
    }
    const std::string previous = current_directory_;
    std::vector<FileDialogEntry> previous_entries = entries_;
    const size_t previous_selection = selected_index_;
    current_directory_ = std::move(normalized);
    if (!refresh()) {
        current_directory_ = previous;
        entries_ = std::move(previous_entries);
        selected_index_ = previous_selection;
        return false;
    }
    if (push_history && !previous.empty() && previous != current_directory_) {
        back_stack_.push_back(previous);
        forward_stack_.clear();
    }
    return true;
}

bool FileDialogModel::go_back() {
    if (back_stack_.empty())
        return false;
    const std::string target = back_stack_.back();
    back_stack_.pop_back();
    const std::string previous = current_directory_;
    if (!navigate_normalized(target, false)) {
        back_stack_.push_back(target);
        return false;
    }
    forward_stack_.push_back(previous);
    return true;
}

bool FileDialogModel::go_forward() {
    if (forward_stack_.empty())
        return false;
    const std::string target = forward_stack_.back();
    forward_stack_.pop_back();
    const std::string previous = current_directory_;
    if (!navigate_normalized(target, false)) {
        forward_stack_.push_back(target);
        return false;
    }
    back_stack_.push_back(previous);
    return true;
}

bool FileDialogModel::go_up() {
    if (current_directory_.empty())
        return false;
    const std::string parent = path_string(make_path(current_directory_).parent_path());
    return parent != current_directory_ && navigate(parent);
}

bool FileDialogModel::accepts_file(std::string_view name) const {
    return selected_filter_ < filters_.size() && matches_filter(name, filters_[selected_filter_]);
}

bool FileDialogModel::refresh() {
    std::string error;
    std::vector<FileDialogEntry> listed = file_system_->list(current_directory_, error);
    if (!error.empty()) {
        set_error(std::move(error));
        return false;
    }
    entries_.clear();
    for (FileDialogEntry& entry : listed) {
        if (entry.is_directory ||
            (mode_ != FileDialogMode::OpenDirectory && accepts_file(entry.name)))
            entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const FileDialogEntry& left, const FileDialogEntry& right) {
                  if (left.is_directory != right.is_directory)
                      return left.is_directory;
                  return lower_ascii(left.name) < lower_ascii(right.name);
              });
    selected_index_ = SIZE_MAX;
    set_error({});
    return true;
}

bool FileDialogModel::select(size_t index) {
    if (index >= entries_.size()) {
        set_error("file selection index out of range");
        return false;
    }
    selected_index_ = index;
    if (mode_ == FileDialogMode::SaveFile && !entries_[index].is_directory)
        file_name_ = entries_[index].name;
    set_error({});
    return true;
}

bool FileDialogModel::activate(size_t index, FileDialogConfirmResult& result) {
    if (!select(index)) {
        result = FileDialogConfirmResult{{}, error_};
        return false;
    }
    if (entries_[index].is_directory)
        return navigate(entries_[index].path);
    if (mode_ == FileDialogMode::OpenFile) {
        result = FileDialogConfirmResult{entries_[index].path, {}};
        return true;
    }
    result = FileDialogConfirmResult{};
    return true;
}

void FileDialogModel::set_file_name(std::string file_name) {
    if (!detail::valid_utf8(file_name)) {
        tc_log_error("[termin-gui-native] file dialog rejected invalid UTF-8 file name");
        throw std::invalid_argument("file dialog file name must be valid UTF-8");
    }
    file_name_ = std::move(file_name);
}

FileDialogConfirmResult FileDialogModel::confirm() const {
    if (current_directory_.empty())
        return FileDialogConfirmResult{{}, "no directory is open"};
    if (mode_ == FileDialogMode::OpenDirectory) {
        if (selected_index_ < entries_.size() && entries_[selected_index_].is_directory)
            return FileDialogConfirmResult{entries_[selected_index_].path, {}};
        return FileDialogConfirmResult{current_directory_, {}};
    }
    if (mode_ == FileDialogMode::OpenFile) {
        if (selected_index_ >= entries_.size() || entries_[selected_index_].is_directory)
            return FileDialogConfirmResult{{}, "select a file to open"};
        return FileDialogConfirmResult{entries_[selected_index_].path, {}};
    }
    std::string name = file_name_;
    const size_t first = name.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return FileDialogConfirmResult{{}, "enter a file name"};
    const size_t last = name.find_last_not_of(" \t\r\n");
    name = name.substr(first, last - first + 1);
    if (make_path(name).is_absolute() || make_path(name).has_parent_path())
        return FileDialogConfirmResult{{}, "file name must not contain a directory path"};
    return FileDialogConfirmResult{path_string(make_path(current_directory_) / make_path(name)),
                                   {}};
}

bool FileDialogModel::create_directory(std::string_view name) {
    if (!detail::valid_utf8(name) || name.empty() || make_path(name).is_absolute() ||
        make_path(name).has_parent_path()) {
        set_error("directory name must be a single path component");
        return false;
    }
    const std::string path = path_string(make_path(current_directory_) / make_path(name));
    std::string error;
    if (!file_system_->create_directory(path, error)) {
        set_error(error.empty() ? "failed to create directory" : std::move(error));
        return false;
    }
    return refresh();
}

void FileDialogModel::set_error(std::string error) {
    error_ = std::move(error);
    if (!error_.empty())
        tc_log_error("[termin-gui-native] file dialog: %s", error_.c_str());
}

} // namespace termin::gui_native
