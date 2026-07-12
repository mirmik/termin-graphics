#include <termin/gui_native/widgets.hpp>

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>

using namespace termin::gui_native;

namespace {

std::string native_path_string(const std::filesystem::path& path) {
#if defined(_WIN32)
    const std::u8string value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
#else
    return path.string();
#endif
}

bool measure_text(void*, const char*, size_t byte_length, float font_size,
                  tc_ui_text_metrics* metrics) {
    if (!metrics)
        return false;
    metrics->width = static_cast<float>(byte_length) * font_size * 0.5f;
    metrics->height = font_size;
    metrics->ascent = font_size * 0.8f;
    metrics->descent = font_size * 0.2f;
    metrics->line_height = font_size * 1.2f;
    return true;
}

class FakeFileSystem final : public FileDialogFileSystem {
  public:
    FakeFileSystem() {
        directories_ = {"/", "/root", "/root/folder", "/root/empty"};
        files_["/root/readme.txt"] = 12;
        files_["/root/IMAGE.PNG"] = 24;
        files_["/root/folder/inside.txt"] = 36;
    }

    std::string normalize(std::string_view path, std::string_view base,
                          std::string& error) const override {
        error.clear();
        std::string value(path);
        if (value.empty() || value == ".")
            return base.empty() ? "/root" : std::string(base);
        if (value == "..") {
            const size_t separator = std::string(base).find_last_of('/');
            return separator == 0 ? "/" : std::string(base).substr(0, separator);
        }
        if (value.front() != '/')
            value = std::string(base) + "/" + value;
        while (value.size() > 1 && value.back() == '/')
            value.pop_back();
        return value;
    }

    bool inspect(std::string_view path, FileDialogEntry& entry, std::string& error) const override {
        const std::string value(path);
        error.clear();
        if (directories_.contains(value)) {
            entry = FileDialogEntry{name(value), value, true, 0, 1};
            return true;
        }
        const auto file = files_.find(value);
        if (file != files_.end()) {
            entry = FileDialogEntry{name(value), value, false, file->second, 1};
            return true;
        }
        error = "missing path";
        return false;
    }

    std::vector<FileDialogEntry> list(std::string_view directory,
                                      std::string& error) const override {
        const std::string parent(directory);
        error.clear();
        if (failing_directories_.contains(parent)) {
            error = "directory is unavailable";
            return {};
        }
        if (!directories_.contains(parent)) {
            error = "missing directory";
            return {};
        }
        std::vector<FileDialogEntry> result;
        for (const std::string& path : directories_) {
            if (path != parent && parent_of(path) == parent)
                result.push_back(FileDialogEntry{name(path), path, true, 0, 1});
        }
        for (const auto& [path, size] : files_) {
            if (parent_of(path) == parent)
                result.push_back(FileDialogEntry{name(path), path, false, size, 1});
        }
        std::reverse(result.begin(), result.end());
        return result;
    }

    bool create_directory(std::string_view path, std::string& error) override {
        std::string value(path);
        std::replace(value.begin(), value.end(), '\\', '/');
        error.clear();
        if (directories_.contains(value) || files_.contains(value)) {
            error = "path already exists";
            return false;
        }
        if (!directories_.contains(parent_of(value))) {
            error = "missing parent";
            return false;
        }
        directories_.insert(value);
        return true;
    }

    void fail_listing(std::string path) { failing_directories_.insert(std::move(path)); }

  private:
    static std::string name(const std::string& path) {
        if (path == "/")
            return "/";
        return path.substr(path.find_last_of('/') + 1);
    }

    static std::string parent_of(const std::string& path) {
        const size_t separator = path.find_last_of('/');
        return separator == 0 ? "/" : path.substr(0, separator);
    }

    std::set<std::string> directories_;
    std::map<std::string, uint64_t> files_;
    std::set<std::string> failing_directories_;
};

size_t entry_index(const FileDialogModel& model, const std::string& name) {
    for (size_t index = 0; index < model.entries().size(); ++index) {
        if (model.entries()[index].name == name)
            return index;
    }
    return SIZE_MAX;
}

void test_filters_and_open_file_navigation() {
    const std::vector<FileDialogFilter> parsed =
        FileDialogModel::parse_filter_string("Images | *.png *.jpg;;Text | *.txt");
    assert(parsed.size() == 2);
    assert(parsed[0].label == "Images");
    assert(parsed[0].patterns.size() == 2);
    assert(FileDialogModel::matches_filter("PHOTO.PNG", parsed[0]));
    assert(!FileDialogModel::matches_filter("readme.txt", parsed[0]));

    auto file_system = std::make_shared<FakeFileSystem>();
    FileDialogModel model(FileDialogMode::OpenFile, file_system);
    model.set_filters(parsed);
    assert(model.navigate("/root"));
    assert(model.entries().size() == 3);
    assert(model.entries()[0].is_directory);
    assert(model.entries()[1].is_directory);
    assert(model.entries()[2].name == "IMAGE.PNG");

    assert(model.set_filter(1));
    assert(entry_index(model, "readme.txt") != SIZE_MAX);
    assert(entry_index(model, "IMAGE.PNG") == SIZE_MAX);
    const size_t folder = entry_index(model, "folder");
    FileDialogConfirmResult activated;
    assert(model.activate(folder, activated));
    assert(!activated.path);
    assert(model.current_directory() == "/root/folder");
    assert(model.can_go_back());
    assert(model.go_back());
    assert(model.current_directory() == "/root");
    assert(model.can_go_forward());

    const size_t readme = entry_index(model, "readme.txt");
    assert(model.select(readme));
    const FileDialogConfirmResult result = model.confirm();
    assert(result.path == "/root/readme.txt");
}

void test_modes_creation_and_failed_navigation_are_transactional() {
    auto file_system = std::make_shared<FakeFileSystem>();
    FileDialogModel save(FileDialogMode::SaveFile, file_system);
    assert(save.navigate("/root"));
    save.set_file_name(" output.bin ");
    assert(save.confirm().path == native_path_string(
        std::filesystem::path("/root") / "output.bin"));
    save.set_file_name("folder/output.bin");
    assert(!save.confirm().path);
    assert(save.create_directory("created"));
    assert(entry_index(save, "created") != SIZE_MAX);
    assert(!save.create_directory("created"));
    assert(!save.error().empty());

    FileDialogModel directory(FileDialogMode::OpenDirectory, file_system);
    assert(directory.navigate("/root"));
    assert(directory.confirm().path == "/root");
    assert(directory.select(entry_index(directory, "empty")));
    assert(directory.confirm().path == "/root/empty");

    const std::vector<FileDialogEntry> previous_entries = directory.entries();
    file_system->fail_listing("/root/folder");
    assert(!directory.navigate("/root/folder"));
    assert(directory.current_directory() == "/root");
    assert(directory.entries().size() == previous_entries.size());
}

void test_overlay_vetoes_invalid_accept_and_emits_one_typed_result() {
    Document document;
    document.set_text_measurer(&measure_text, nullptr);
    DocumentBuilder ui(document);
    auto file_system = std::make_shared<FakeFileSystem>();
    auto& dialog = ui.make<FileDialogOverlay>(FileDialogMode::OpenFile, file_system);
    dialog.set_initial_directory("/root");
    dialog.set_filters(FileDialogModel::parse_filter_string("Text | *.txt"));
    std::vector<std::optional<std::string>> results;
    dialog.path_finished().connect(
        [&results](FileDialogOverlay&, const std::optional<std::string>& result) {
            results.push_back(result);
        });

    assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 900.0f, 700.0f}));
    assert(dialog.open());
    assert(!dialog.activate("accept", document.get()));
    assert(dialog.open());
    assert(results.empty());
    assert(dialog.model().select(entry_index(dialog.model(), "readme.txt")));
    assert(dialog.activate("accept", document.get()));
    assert(!dialog.open());
    assert(results.size() == 1);
    assert(results[0] == "/root/readme.txt");

    assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 900.0f, 700.0f}));
    assert(dialog.activate("cancel", document.get()));
    assert(results.size() == 2);
    assert(!results[1]);
}

} // namespace

int main() {
    test_filters_and_open_file_navigation();
    test_modes_creation_and_failed_navigation_are_transactional();
    test_overlay_vetoes_invalid_accept_and_emits_one_typed_result();
    return EXIT_SUCCESS;
}
