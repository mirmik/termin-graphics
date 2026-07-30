#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <tcbase/tc_trent.hpp>
#include <termin/gui_native/export.h>
#include <termin/gui_native/tc_document.hpp>

namespace termin::gui_native {

inline constexpr std::uint32_t UISCRIPT_VERSION = 2;

class TERMIN_GUI_NATIVE_API UiScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct UiScriptNode {
    std::string type_name;
    std::string name;
    tc::trent properties = tc::trent::dict();
    std::vector<UiScriptNode> children;
    std::string source_path;
};

struct UiScriptDescription {
    std::uint32_t version = UISCRIPT_VERSION;
    UiScriptNode root;
    std::vector<std::string> type_dependencies;
};

class TERMIN_GUI_NATIVE_API UiScriptParser {
public:
    UiScriptDescription parse(const std::string& source) const;
};

struct MaterializedWidget {
    tc_widget_handle handle = tc_widget_handle_invalid();
    std::string type_name;
    tc::trent properties = tc::trent::dict();
};

class TERMIN_GUI_NATIVE_API LoadedUiScript {
public:
    LoadedUiScript() = default;
    LoadedUiScript(const LoadedUiScript&) = delete;
    LoadedUiScript& operator=(const LoadedUiScript&) = delete;
    LoadedUiScript(LoadedUiScript&& other) noexcept;
    LoadedUiScript& operator=(LoadedUiScript&& other) noexcept;
    ~LoadedUiScript();

    TcDocument document() const { return TcDocument(document_); }
    const UiScriptDescription& description() const { return description_; }
    const MaterializedWidget& root() const { return root_; }
    const std::unordered_map<std::string, MaterializedWidget>& widgets() const {
        return widgets_;
    }
    const MaterializedWidget& named(const std::string& name) const;
    bool closed() const { return closed_; }
    void close();

private:
    friend class UiScriptLoader;

    tc_ui_document_handle document_ = tc_ui_document_handle_invalid();
    UiScriptDescription description_;
    MaterializedWidget root_;
    std::unordered_map<std::string, MaterializedWidget> widgets_;
    bool owns_document_ = false;
    bool closed_ = true;
};

class TERMIN_GUI_NATIVE_API UiScriptLoader {
public:
    UiScriptParser parser;

    LoadedUiScript load(
        const std::string& path,
        TcDocument document = {}
    ) const;
    LoadedUiScript load_string(
        const std::string& source,
        TcDocument document = {},
        const std::string& source_name = "<string>"
    ) const;
    LoadedUiScript materialize(
        const UiScriptDescription& description,
        TcDocument document = {}
    ) const;
    LoadedUiScript reload(
        LoadedUiScript& loaded,
        const std::string& source,
        const std::string& source_name = "<reload>"
    ) const;
};

} // namespace termin::gui_native
