#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <termin/gui_native/file_dialog_model.hpp>

namespace termin::gui_native {

struct FileDialogRequest {
    FileDialogMode mode = FileDialogMode::OpenFile;
    std::string title;
    std::string initial_directory;
    std::string initial_file_name;
    std::vector<FileDialogFilter> filters{{"All files", {"*"}}};
};

using FileDialogServiceCallback = std::function<void(std::optional<std::string>)>;

// Host applications may implement this boundary with a platform-native dialog.
// A false return means that no dialog was started; callers choose explicitly
// whether to use FileDialogOverlay instead.
class FileDialogService {
  public:
    virtual ~FileDialogService() = default;
    virtual bool available() const = 0;
    virtual bool show(const FileDialogRequest& request, FileDialogServiceCallback callback,
                      std::string& error) = 0;
};

} // namespace termin::gui_native
