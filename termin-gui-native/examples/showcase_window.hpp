#pragma once

#include <functional>

#include <termin/gui_native/tc_document.hpp>

namespace termin::gui_native::examples {

using DocumentBuildCallback = std::function<void(TcDocument)>;
using ExampleTickCallback = std::function<void(double)>;

int run_document_window(
    const char* title,
    DocumentBuildCallback build,
    ExampleTickCallback tick = {});
int run_showcase_window(const char* title);

} // namespace termin::gui_native::examples
