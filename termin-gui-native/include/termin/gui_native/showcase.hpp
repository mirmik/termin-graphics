#pragma once

#include <termin/gui_native/tc_document.hpp>

namespace termin::gui_native {

class Checkbox;
class FileGridWidget;
class ListWidget;
class MenuBar;
class MessageBox;
class ProgressBar;
class ScrollArea;
class Slider;
class StatusBar;
class TabView;
class TableWidget;
class ToolBar;
class TextArea;
class TextInput;
class TreeWidget;

struct ShowcaseRefs {
  ProgressBar *progress = nullptr;
  Slider *slider = nullptr;
  ToolBar *toolbar = nullptr;
  MenuBar *menu_bar = nullptr;
  MessageBox *message_box = nullptr;
  StatusBar *status_bar = nullptr;
  Checkbox *checkbox = nullptr;
  FileGridWidget *file_grid = nullptr;
  ListWidget *list = nullptr;
  TreeWidget *tree = nullptr;
  TableWidget *table = nullptr;
  ScrollArea *content_scroll = nullptr;
  TextArea *text_area = nullptr;
  TextInput *text_input = nullptr;
  TabView *tabs = nullptr;
};

ShowcaseRefs build_showcase(TcDocument document);

} // namespace termin::gui_native
