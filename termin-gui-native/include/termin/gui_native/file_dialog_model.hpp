#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace termin::gui_native {

enum class FileDialogMode { OpenFile, SaveFile, OpenDirectory };

struct FileDialogFilter {
    std::string label;
    std::vector<std::string> patterns;
};

struct FileDialogEntry {
    std::string name;
    std::string path;
    bool is_directory = false;
    uint64_t size = 0;
    int64_t modified_time = 0;
};

struct FileDialogConfirmResult {
    std::optional<std::string> path;
    std::string error;
};

class FileDialogFileSystem {
  public:
    virtual ~FileDialogFileSystem() = default;
    virtual std::string normalize(std::string_view path, std::string_view base,
                                  std::string& error) const = 0;
    virtual bool inspect(std::string_view path, FileDialogEntry& entry,
                         std::string& error) const = 0;
    virtual std::vector<FileDialogEntry> list(std::string_view directory,
                                              std::string& error) const = 0;
    virtual bool create_directory(std::string_view path, std::string& error) = 0;
};

class NativeFileDialogFileSystem final : public FileDialogFileSystem {
  public:
    std::string normalize(std::string_view path, std::string_view base,
                          std::string& error) const override;
    bool inspect(std::string_view path, FileDialogEntry& entry, std::string& error) const override;
    std::vector<FileDialogEntry> list(std::string_view directory,
                                      std::string& error) const override;
    bool create_directory(std::string_view path, std::string& error) override;
};

class FileDialogModel {
  private:
    FileDialogMode mode_;
    std::shared_ptr<FileDialogFileSystem> file_system_;
    std::string current_directory_;
    std::vector<FileDialogEntry> entries_;
    std::vector<FileDialogFilter> filters_{{"All files", {"*"}}};
    std::vector<std::string> back_stack_;
    std::vector<std::string> forward_stack_;
    size_t selected_filter_ = 0;
    size_t selected_index_ = SIZE_MAX;
    std::string file_name_;
    std::string error_;

  public:
    explicit FileDialogModel(FileDialogMode mode,
                             std::shared_ptr<FileDialogFileSystem> file_system = {});

    FileDialogMode mode() const { return mode_; }
    const std::string& current_directory() const { return current_directory_; }
    const std::vector<FileDialogEntry>& entries() const { return entries_; }
    const std::vector<FileDialogFilter>& filters() const { return filters_; }
    size_t selected_filter() const { return selected_filter_; }
    size_t selected_index() const { return selected_index_; }
    const std::string& file_name() const { return file_name_; }
    const std::string& error() const { return error_; }
    bool can_go_back() const { return !back_stack_.empty(); }
    bool can_go_forward() const { return !forward_stack_.empty(); }

    static std::vector<FileDialogFilter> parse_filter_string(std::string_view text);
    static bool matches_filter(std::string_view name, const FileDialogFilter& filter);
    void set_filters(std::vector<FileDialogFilter> filters);
    bool set_filter(size_t index);
    bool navigate(std::string_view path, bool push_history = true);
    bool go_back();
    bool go_forward();
    bool go_up();
    bool refresh();
    bool select(size_t index);
    bool activate(size_t index, FileDialogConfirmResult& result);
    void set_file_name(std::string file_name);
    FileDialogConfirmResult confirm() const;
    bool create_directory(std::string_view name);

  private:
    bool navigate_normalized(std::string normalized, bool push_history);
    bool accepts_file(std::string_view name) const;
    void set_error(std::string error);

};

} // namespace termin::gui_native
