#pragma once

#include <termin/gui_native/widget.hpp>

namespace termin::gui_native {

class Checkbox;
class ListWidget;
class ProgressBar;
class ScrollArea;
class Slider;
class TabView;
class TextArea;
class TextInput;

struct ShowcaseRefs {
    ProgressBar* progress = nullptr;
    Slider* slider = nullptr;
    Checkbox* checkbox = nullptr;
    ListWidget* list = nullptr;
    ScrollArea* content_scroll = nullptr;
    TextArea* text_area = nullptr;
    TextInput* text_input = nullptr;
    TabView* tabs = nullptr;
};

ShowcaseRefs build_showcase(Document& document);

} // namespace termin::gui_native
