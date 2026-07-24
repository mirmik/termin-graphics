#include <termin/gui_native/tc_document.hpp>

#include "widgets_test_support.hpp"

namespace termin_gui_native_test {
void test_containers_register_and_replace_canonical_children() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);

  auto &first_box = ui.make_root<HStack>("first-box");
  auto &second_box = ui.make_root<HStack>("second-box");
  auto &moving_panel = ui.make<Panel>("moving-panel");
  first_box.add_child(moving_panel);
  assert(first_box.child_count() == 1);
  assert(first_box.child_at(0) == moving_panel.c_widget());
  assert(moving_panel.parent_widget() == first_box.c_widget());

  second_box.add_child(moving_panel);
  assert(first_box.child_count() == 0);
  assert(second_box.child_count() == 1);
  assert(moving_panel.parent_widget() == second_box.c_widget());
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 60.0f});
  assert(near(moving_panel.bounds().width, 200.0f));

  auto &grid = ui.make_root<GridLayout>("grid");
  auto &grid_child = ui.make<Panel>("grid-child");
  grid.add_child(grid_child, 0, 0);
  assert(grid.child_count() == 1);
  assert(grid_child.parent_widget() == grid.c_widget());

  auto &group = ui.make_root<GroupBox>("group");
  auto &group_first = ui.make<Panel>("group-first");
  auto &group_second = ui.make<Panel>("group-second");
  group.set_content(group_first);
  group.set_content(group_second);
  assert(group.child_count() == 1);
  assert(group.child_at(0) == group_second.c_widget());
  assert(group_first.parent_widget() == nullptr);
  assert(group_second.parent_widget() == group.c_widget());

  auto &splitter = ui.make_root<Splitter>(Orientation::Horizontal, "splitter");
  auto &split_first = ui.make<Panel>("split-first");
  auto &split_second = ui.make<Panel>("split-second");
  auto &split_replacement = ui.make<Panel>("split-replacement");
  splitter.set_first(split_first);
  splitter.set_second(split_second);
  splitter.set_first(split_replacement);
  assert(splitter.child_count() == 2);
  assert(splitter.child_at(0) == split_replacement.c_widget());
  assert(splitter.child_at(1) == split_second.c_widget());
  assert(split_first.parent_widget() == nullptr);

  auto &scroll = ui.make_root<ScrollArea>("scroll");
  auto &scroll_first = ui.make<Panel>("scroll-first");
  auto &scroll_second = ui.make<Panel>("scroll-second");
  scroll.set_content(scroll_first);
  scroll.set_content(scroll_second);
  assert(scroll.child_count() == 1);
  assert(scroll.child_at(0) == scroll_second.c_widget());
  assert(scroll_first.parent_widget() == nullptr);

  auto &tabs = ui.make_root<TabView>("tabs");
  auto &tab_first = ui.make<Panel>("tab-first");
  auto &tab_second = ui.make<Panel>("tab-second");
  tabs.add_page("First", tab_first);
  tabs.add_page("Second", tab_second);
  assert(tabs.child_count() == 2);
  assert(tabs.child_at(0) == tab_first.c_widget());
  assert(tabs.child_at(1) == tab_second.c_widget());

  tc_ui_document_destroy(document_handle);
}

void test_common_visibility_enabled_and_mouse_transparent_state() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<HStack>("root");
  auto &hidden = ui.make<Panel>("hidden");
  auto &button = ui.make<Button>("button");
  hidden.set_preferred_size(tc_ui_size{40.0f, 30.0f});
  root.add_preferred_child(hidden);
  root.add_stretch_child(button);

  hidden.set_visible(false);
  button.set_enabled(false);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 160.0f, 40.0f});
  assert(near(button.bounds().x, 0.0f));
  assert(near(button.bounds().width, 160.0f));

  tc_widget_handle hit = document.hit_test(20.0f, 20.0f);
  assert(tc_widget_handle_eq(hit, root.handle()));
  tc_ui_pointer_event event{};
  event.type = TC_UI_POINTER_DOWN;
  event.x = 20.0f;
  event.y = 20.0f;
  assert(document.dispatch_pointer_event(event) == TC_UI_EVENT_IGNORED);
  assert(tc_widget_handle_is_invalid(document.focused_widget()));

  button.set_mouse_transparent(true);
  hit = document.hit_test(20.0f, 20.0f);
  assert(tc_widget_handle_eq(hit, root.handle()));
  root.set_mouse_transparent(true);
  assert(tc_widget_handle_is_invalid(document.hit_test(20.0f, 20.0f)));

  tc_ui_document_destroy(document_handle);
}

void test_cpp_theme_style_facade_inheritance_and_state() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto &root = ui.make_root<HStack>("style-root");
  auto &button = ui.make<Button>("Styled");
  root.add_child(button);
  assert(button.style_role() == TC_UI_STYLE_BUTTON);

  tc_ui_style_override inherited{};
  inherited.fields = TC_UI_STYLE_FONT_SIZE | TC_UI_STYLE_FOREGROUND;
  inherited.flags = TC_UI_STYLE_OVERRIDE_INHERIT;
  inherited.value.font_size = 18.0f;
  inherited.value.foreground = tc_ui_color{0.8f, 0.7f, 0.6f, 1.0f};
  assert(root.set_style_override(inherited));
  tc_ui_style style = document.resolve_style(button);
  assert(near(style.font_size, 18.0f));
  assert(near(style.foreground.g, 0.7f));

  button.set_enabled(false);
  style = document.resolve_style(button);
  assert(near(
      style.background.r,
      document.theme().roles[TC_UI_STYLE_BUTTON].disabled.value.background.r));

  tc_ui_theme theme = document.theme();
  theme.roles[TC_UI_STYLE_BUTTON].base.accent =
      tc_ui_color{0.91f, 0.2f, 0.1f, 1.0f};
  const uint64_t revision = document.theme_revision();
  button.clear_dirty(TC_WIDGET_DIRTY_MASK);
  assert(document.set_theme(theme));
  assert(document.theme_revision() == revision + 1);
  assert(button.has_dirty_flags(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT |
                                TC_WIDGET_DIRTY_STATE));
  assert(near(document.resolve_style(button).accent.r, 0.91f));

  tc_ui_document_destroy(document_handle);
}

void test_collection_and_selection_models_are_reusable() {
  CollectionModel model;
  model.set_items({
      CollectionItem{"a", "Alpha", "First", true},
      CollectionItem{"b", "Beta", "Second", true},
      CollectionItem{"c", "Gamma", "Third", true},
      CollectionItem{"d", "Delta", "Fourth", true},
  });
  const uint64_t revision = model.revision();
  model.update(1, CollectionItem{"b", "Beta 2", "Updated", true});
  assert(model.revision() == revision + 1);
  assert(model.item(1).text == "Beta 2");

  SelectionModel selection(SelectionMode::Multiple);
  assert(selection.select_only(1));
  assert(selection.extend_to(3));
  assert((selection.selected_indices() == std::vector<size_t>{1, 2, 3}));
  assert(selection.toggle(2));
  assert((selection.selected_indices() == std::vector<size_t>{1, 3}));
  assert(selection.select_all(model.size()));
  assert(selection.selected_indices().size() == 4);
  model.erase(3);
  assert(selection.items_erased(3, 1, model.size()));
  assert((selection.selected_indices() == std::vector<size_t>{0, 1, 2}));

  assert(selection.select_only(2));
  assert(selection.items_inserted(1, 2));
  assert((selection.selected_indices() == std::vector<size_t>{4}));

  bool rejected = false;
  try {
    model.append(CollectionItem{"bad", std::string("\xff", 1), {}, true});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void test_list_widget_virtualizes_large_models_and_reconciles_selection() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  std::vector<CollectionItem> items;
  items.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    items.push_back(CollectionItem{
        "item-" + std::to_string(index),
        "Item " + std::to_string(index),
        index % 2 == 0 ? "Even" : "Odd",
        true,
    });
  }
  model->set_items(std::move(items));
  auto &list = ui.make_root<ListWidget>(model);
  list.set_row_height(40.0f);
  list.set_row_spacing(2.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 126.0f});

  const auto [first, last] = list.visible_range();
  assert(first == 0);
  assert(last <= 5);
  assert(list.content_height() > 400000.0f);
  assert(list.child_count() == 0);

  tc_ui_pointer_event wheel{};
  wheel.type = TC_UI_POINTER_WHEEL;
  wheel.x = 10.0f;
  wheel.y = 10.0f;
  wheel.wheel_y = 1.0f;
  assert(list.pointer_event(document.get(), &wheel) == TC_UI_EVENT_IGNORED);
  wheel.wheel_y = -1.0f;
  assert(list.pointer_event(document.get(), &wheel) == TC_UI_EVENT_HANDLED);
  assert(list.scroll_y() > 0.0f);
  list.set_scroll_y(0.0f);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 10);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) >= 6);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  list.selection().select_only(9999);
  list.ensure_visible(9999);
  assert(list.scroll_y() > 400000.0f);
  const auto [scrolled_first, scrolled_last] = list.visible_range();
  assert(scrolled_first > 9990);
  assert(scrolled_last == 10000);

  model->set_items({CollectionItem{"only", "Only", {}, true}});
  list.layout(document.get(), list.bounds());
  assert(list.selection().selected_indices().empty());
  assert(near(list.scroll_y(), 0.0f));

  tc_ui_document_destroy(document_handle);
}

void test_list_widget_pointer_keyboard_and_multi_selection() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
      CollectionItem{"2", "Disabled", {}, false},
      CollectionItem{"3", "Three", {}, true},
      CollectionItem{"4", "Four", {}, true},
      CollectionItem{"5", "Five", {}, true},
  });
  auto &list = ui.make_root<ListWidget>(model);
  list.set_selection_mode(SelectionMode::Multiple);
  list.set_row_height(30.0f);
  list.set_row_spacing(0.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 240.0f, 90.0f});

  std::vector<std::vector<size_t>> changes;
  list.selection_changed().connect(
      [&changes](ListWidget &, const std::vector<size_t> &selected) {
        changes.push_back(selected);
      });
  size_t activated = SelectionModel::npos;
  list.activated().connect(
      [&activated](ListWidget &, size_t index, const CollectionItem &) {
        activated = index;
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 10.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{0}));

  std::vector<std::tuple<int64_t, float, float>> context_requests;
  list.context_menu_requested().connect(
      [&context_requests](ListWidget &, int64_t index, float x, float y) {
        context_requests.emplace_back(index, x, y);
      });
  pointer.button = 1;
  pointer.x = 10.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{0}));
  assert((context_requests ==
          std::vector<std::tuple<int64_t, float, float>>{{0, 10.0f, 15.0f}}));
  pointer.button = 0;

  pointer.y = 45.0f;
  pointer.modifiers = TC_UI_MOD_CTRL;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{0, 1}));

  list.set_scroll_y(60.0f);
  pointer.y = 75.0f;
  pointer.modifiers = TC_UI_MOD_SHIFT;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(
      (list.selection().selected_indices() == std::vector<size_t>{1, 2, 3, 4}));

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_UP_ARROW;
  key.modifiers = 0;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert((list.selection().selected_indices() == std::vector<size_t>{3}));
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == 3);
  key.key = TC_UI_KEY_A;
  key.modifiers = TC_UI_MOD_CTRL;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(list.selection().selected_indices().size() == model->size() - 1);
  assert(changes.size() >= 5);

  tc_ui_document_destroy(document_handle);
}

void test_list_widget_model_notifications_preserve_lifetime_and_shift_selection() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
      CollectionItem{"2", "Two", {}, true},
  });
  std::weak_ptr<CollectionModel> weak_model = model;
  auto &list = ui.make_root<ListWidget>(model);
  assert(list.select_index(2));
  model->erase(0);
  assert((list.selection().selected_indices() == std::vector<size_t>{1}));
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = list.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<CollectionModel>();
  destroying_model->set_items({
      CollectionItem{"0", "Zero", {}, true},
      CollectionItem{"1", "One", {}, true},
  });
  auto &destroying_list = ui.make_root<ListWidget>(destroying_model);
  assert(destroying_list.select_index(1));
  const tc_widget_handle destroying_handle = destroying_list.handle();
  destroying_list.selection_changed().connect([&document, destroying_handle](
                                                  ListWidget &,
                                                  const std::vector<size_t> &) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(0);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));

  tc_ui_document_destroy(document_handle);
}

void test_tree_model_stable_ids_move_and_expansion_reconcile() {
  TreeModel model;
  const TreeNodeId root_a =
      model.append_root(CollectionItem{"a", "A", {}, true});
  const TreeNodeId root_b =
      model.append_root(CollectionItem{"b", "B", {}, true});
  const TreeNodeId child =
      model.append_child(root_a, CollectionItem{"child", "Child", {}, true});
  const TreeNodeId grandchild = model.append_child(
      child, CollectionItem{"grandchild", "Grandchild", {}, true});
  assert(model.size() == 4);
  assert(model.node(grandchild).parent == child);
  assert((model.children(root_a) == std::vector<TreeNodeId>{child}));

  model.move(child, root_b);
  assert(model.children(root_a).empty());
  assert((model.children(root_b) == std::vector<TreeNodeId>{child}));
  assert(model.node(child).parent == root_b);

  bool rejected_cycle = false;
  try {
    model.move(root_b, grandchild);
  } catch (const std::invalid_argument &) {
    rejected_cycle = true;
  }
  assert(rejected_cycle);

  TreeExpansionModel expansion;
  assert(expansion.set_expanded(root_b, true));
  assert(expansion.set_expanded(child, true));
  model.erase(child);
  assert(!model.contains(child));
  assert(!model.contains(grandchild));
  assert(expansion.reconcile(model));
  assert(expansion.expanded(root_b));
  assert(!expansion.expanded(child));

  bool rejected_utf8 = false;
  try {
    model.append_root(CollectionItem{"bad", std::string("\xff", 1), {}, true});
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);
}

void test_tree_widget_virtualizes_large_expanded_model() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  auto expansion = std::make_shared<TreeExpansionModel>();
  for (size_t root_index = 0; root_index < 100; ++root_index) {
    const TreeNodeId root =
        model->append_root(CollectionItem{"root-" + std::to_string(root_index),
                                          "Root " + std::to_string(root_index),
                                          {},
                                          true});
    expansion->set_expanded(root, true);
    for (size_t child_index = 0; child_index < 100; ++child_index) {
      model->append_child(root,
                          CollectionItem{"node-" + std::to_string(root_index) +
                                             "-" + std::to_string(child_index),
                                         "Node " + std::to_string(child_index),
                                         {},
                                         true});
    }
  }
  auto &tree = ui.make_root<TreeWidget>(model, expansion);
  tree.set_row_height(24.0f);
  tree.set_row_spacing(1.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 100.0f});
  assert(model->size() == 10100);
  assert(tree.visible_count() == 10100);
  assert(tree.child_count() == 0);
  assert(tree.content_height() > 250000.0f);
  const auto [first, last] = tree.visible_range();
  assert(first == 0);
  assert(last <= 6);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 12);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  const TreeNodeId last_root = model->roots().back();
  const TreeNodeId last_child = model->children(last_root).back();
  assert(tree.select_node(last_child));
  assert(tree.scroll_y() > 250000.0f);
  assert(tree.visible_range().first > 10000);

  tc_ui_document_destroy(document_handle);
}

void test_tree_widget_pointer_keyboard_signals_and_lifetime() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  const TreeNodeId root =
      model->append_root(CollectionItem{"root", "Root", {}, true});
  const TreeNodeId first =
      model->append_child(root, CollectionItem{"first", "First", {}, true});
  const TreeNodeId disabled = model->append_child(
      root, CollectionItem{"disabled", "Disabled", {}, false});
  const TreeNodeId last =
      model->append_child(root, CollectionItem{"last", "Last", {}, true});
  std::weak_ptr<TreeModel> weak_model = model;
  auto &tree = ui.make_root<TreeWidget>(model);
  tree.set_row_height(30.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 90.0f});

  std::vector<TreeNodeId> selections;
  std::vector<std::pair<TreeNodeId, bool>> expansions;
  TreeNodeId activated = kInvalidTreeNodeId;
  TreeNodeId delete_requested = kInvalidTreeNodeId;
  TreeNodeId context_requested = kInvalidTreeNodeId;
  tree.selection_changed().connect(
      [&selections](TreeWidget &, TreeNodeId node) {
        selections.push_back(node);
      });
  tree.expansion_changed().connect(
      [&expansions](TreeWidget &, TreeNodeId node, bool value) {
        expansions.emplace_back(node, value);
      });
  tree.activated().connect(
      [&activated](TreeWidget &, TreeNodeId node, const CollectionItem &) {
        activated = node;
      });
  tree.delete_requested().connect(
      [&delete_requested](TreeWidget &, TreeNodeId node,
                          const CollectionItem &) { delete_requested = node; });
  tree.context_menu_requested().connect(
      [&context_requested](TreeWidget &, TreeNodeId node, float, float) {
        context_requested = node;
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 5.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.expanded(root));
  assert(expansions.back() == std::make_pair(root, true));

  pointer.x = 40.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == root);

  pointer.button = 1;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(context_requested == root);
  pointer.button = 0;

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == first);
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == last);
  assert(tree.selected_node() != disabled);
  key.key = TC_UI_KEY_LEFT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == root);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(tree.selected_node() == first);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == first);
  key.key = TC_UI_KEY_DELETE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(delete_requested == first);
  assert(!selections.empty());

  model->erase(first);
  assert(tree.selected_node() == kInvalidTreeNodeId);
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = tree.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<TreeModel>();
  const TreeNodeId destroying_root = destroying_model->append_root(
      CollectionItem{"destroying-root", "Root", {}, true});
  const TreeNodeId destroying_child = destroying_model->append_child(
      destroying_root, CollectionItem{"destroying-child", "Child", {}, true});
  auto &destroying_tree = ui.make_root<TreeWidget>(destroying_model);
  assert(destroying_tree.select_node(destroying_child));
  const tc_widget_handle destroying_handle = destroying_tree.handle();
  destroying_tree.selection_changed().connect([&document, destroying_handle](
                                                  TreeWidget &, TreeNodeId) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(destroying_child);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));

  tc_ui_document_destroy(document_handle);
}

void test_tree_widget_drag_drop_positions_and_capture() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeModel>();
  const TreeNodeId first =
      model->append_root(CollectionItem{"first", "First", {}, true});
  const TreeNodeId second =
      model->append_root(CollectionItem{"second", "Second", {}, true});
  auto &tree = ui.make_root<TreeWidget>(model);
  tree.set_row_height(30.0f);
  tree.set_draggable(true);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 90.0f});

  TreeNodeId dropped = kInvalidTreeNodeId;
  TreeNodeId target = kInvalidTreeNodeId;
  TreeDropPosition position = TreeDropPosition::Root;
  tree.drop_requested().connect([&](TreeWidget &, TreeNodeId dragged,
                                    TreeNodeId drop_target,
                                    TreeDropPosition drop_position) {
    dropped = dragged;
    target = drop_target;
    position = drop_position;
  });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = 40.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), tree.handle()));

  pointer.type = TC_UI_POINTER_MOVE;
  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tree.dragging());

  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));
  assert(!tree.dragging());
  assert(dropped == first);
  assert(target == second);
  assert(position == TreeDropPosition::Inside);

  tc_ui_document_destroy(document_handle);
}

void test_table_models_preserve_row_ids_and_validate_columns() {
  TableModel model;
  const TableRowId first =
      model.append(TableRowData{"first", {"First", "1"}, true});
  const TableRowId last =
      model.append(TableRowData{"last", {"Last", "2"}, true});
  const TableRowId middle =
      model.insert(1, TableRowData{"middle", {"Middle", "3"}, false});
  assert(model.size() == 3);
  assert(model.index_of(first) == 0);
  assert(model.index_of(middle) == 1);
  assert(model.index_of(last) == 2);

  model.update(last, TableRowData{"last", {"Updated", "4"}, true});
  assert(model.row(last).data.cells.front() == "Updated");
  model.erase(middle);
  assert(!model.contains(middle));
  assert(model.index_of(last) == 1);

  bool rejected_utf8 = false;
  try {
    model.append(TableRowData{"bad", {std::string("\xff", 1)}, true});
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);

  TableColumnModel columns;
  columns.set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 80.0f, 50.0f,
                  120.0f, 1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
  });
  assert(columns.resize(0, 10.0f) == 50.0f);
  assert(columns.resize(0, 200.0f) == 120.0f);
  assert(columns.column(0).policy == TableColumnPolicy::Fixed);

  bool rejected_duplicate = false;
  try {
    columns.append(TableColumn{"name", "Duplicate", TableColumnPolicy::Stretch,
                               0.0f, 40.0f, 0.0f, 1.0f, true});
  } catch (const std::invalid_argument &) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);
}

void test_tree_table_model_preserves_identity_and_validates_hierarchy() {
  TreeTableModel model;
  model.set_rows({
      TreeTableRowData{"render", "", {"Render", "12.0"}, true},
      TreeTableRowData{"render/compose", "render", {"Compose", "8.0"}, true},
      TreeTableRowData{"events", "", {"Events", "2.0"}, true},
  });
  const TreeTableNodeId render = model.find("render");
  const TreeTableNodeId compose = model.find("render/compose");
  assert(render != kInvalidTreeTableNodeId);
  assert(model.roots() ==
         std::vector<TreeTableNodeId>({render, model.find("events")}));
  assert(model.node(render).children ==
         std::vector<TreeTableNodeId>({compose}));
  assert(model.node(compose).parent == render);

  model.set_rows({
      TreeTableRowData{"events", "", {"Events", "3.0"}, true},
      TreeTableRowData{"render", "", {"Render", "10.0"}, true},
      TreeTableRowData{"render/compose", "render", {"Compose", "6.0"}, true},
  });
  assert(model.find("render") == render);
  assert(model.find("render/compose") == compose);
  assert(model.roots().front() == model.find("events"));

  bool rejected = false;
  try {
    model.set_rows({TreeTableRowData{"child", "missing", {"Child"}, true}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(model.find("render") == render);

  rejected = false;
  try {
    model.set_rows({TreeTableRowData{"a", "b", {"A"}, true},
                    TreeTableRowData{"b", "a", {"B"}, true}});
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
}

void test_tree_table_widget_expansion_navigation_and_columns() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TreeTableModel>();
  model->set_rows({
      TreeTableRowData{"render", "", {"Render", "12.0"}, true},
      TreeTableRowData{"render/compose", "render", {"Compose", "8.0"}, true},
      TreeTableRowData{
          "render/compose/pass", "render/compose", {"Pass", "5.0"}, true},
      TreeTableRowData{"events", "", {"Events", "2.0"}, true},
  });
  auto columns = std::make_shared<TableColumnModel>();
  columns->set_columns({
      TableColumn{"section", "Section", TableColumnPolicy::Stretch, 0.0f,
                  100.0f},
      TableColumn{"ms", "ms", TableColumnPolicy::Fixed, 60.0f, 40.0f},
  });
  auto expansion = std::make_shared<TreeExpansionModel>();
  const TreeTableNodeId render = model->find("render");
  const TreeTableNodeId compose = model->find("render/compose");
  expansion->set_expanded(render, true);
  expansion->set_expanded(compose, true);
  auto &table = ui.make_root<TreeTableWidget>(model, columns, expansion);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 320.0f, 180.0f});
  assert(table.visible_count() == 4);
  assert(table.visible_row(2).depth == 2);
  assert(near(table.column_layout()[0].width, 260.0f));
  assert(near(table.column_layout()[1].width, 60.0f));

  table.select_node(compose);
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_LEFT;
  assert(table.key_event(tc_ui_document_handle_invalid(), &key) == TC_UI_EVENT_HANDLED);
  assert(!table.expanded(compose));
  assert(table.visible_count() == 3);
  key.key = TC_UI_KEY_RIGHT;
  assert(table.key_event(tc_ui_document_handle_invalid(), &key) == TC_UI_EVENT_HANDLED);
  assert(table.expanded(compose));
  assert(table.visible_count() == 4);

  model->set_rows({
      TreeTableRowData{"render", "", {"Render", "11.0"}, true},
      TreeTableRowData{"render/compose", "render", {"Compose", "7.0"}, true},
      TreeTableRowData{"events", "", {"Events", "3.0"}, true},
  });
  assert(table.selected_node() == compose);
  assert(table.expanded(compose));
  assert(table.visible_count() == 3);

  table.set_expanded(render, false);
  assert(table.visible_count() == 2);
  assert(near(table.content_height(), table.row_height() * 2.0f));

  tc_ui_document_destroy(document_handle);
}

void test_file_grid_widget_virtualizes_large_model_and_responsive_layout() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  std::vector<CollectionItem> items;
  items.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    items.push_back(CollectionItem{"file-" + std::to_string(index),
                                   index == 0
                                       ? "A very long UTF-8 filename пример.txt"
                                       : "File " + std::to_string(index),
                                   ".txt", true, index == 0 ? 77u : 0u});
  }
  model->set_items(std::move(items));
  auto &grid = ui.make_root<FileGridWidget>(model);
  grid.set_tile_size(50.0f, 60.0f);
  grid.set_tile_spacing(4.0f);
  grid.set_padding(4.0f);
  grid.set_icon_size(20.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 220.0f, 128.0f});

  assert(grid.child_count() == 0);
  assert(grid.column_count() == 4);
  assert(grid.row_count() == 2500);
  assert(grid.content_height() > 150000.0f);
  assert(grid.has_scrollbar());
  const auto [first, last] = grid.visible_range();
  assert(first == 0);
  assert(last <= 16);

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 32);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXTURE) == 1);
  bool found_elided_name = false;
  for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list);
       ++index) {
    const tc_ui_draw_command *command =
        tc_ui_draw_list_command_at(draw_list, index);
    if (command && command->type == TC_UI_DRAW_TEXT && command->text &&
        std::string(command->text).find("...") != std::string::npos) {
      found_elided_name = true;
    }
  }
  assert(found_elided_name);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(grid.select_index(9999));
  assert(grid.scroll_y() > 150000.0f);
  assert(grid.visible_range().first > 9980);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 112.0f, 128.0f});
  assert(grid.column_count() == 2);
  assert(grid.row_count() == 5000);

  tc_ui_document_destroy(document_handle);
}

CommandData test_command(std::string id, std::string label, bool enabled = true,
                         bool checkable = false, bool checked = false) {
  return CommandData{
      std::move(id), std::move(label), {},      {}, {}, CommandKind::Action,
      enabled,       checkable,        checked, 0,  {}};
}

void test_command_model_stable_ids_validation_and_mutation() {
  CommandModel model;
  const CommandId first = model.append(test_command("first", "First"));
  const CommandId checked =
      model.append(test_command("checked", "Checked", true, true));
  model.insert(1, CommandData{"separator",
                              {},
                              {},
                              {},
                              {},
                              CommandKind::Separator,
                              true,
                              false,
                              false,
                              0,
                              {}});
  assert(model.size() == 3);
  assert(model.index_of(first) == 0);
  assert(model.index_of(checked) == 2);
  model.set_checked(checked, true);
  assert(model.command(checked).data.checked);
  model.set_enabled(first, false);
  assert(!model.command(first).data.enabled);

  bool rejected_duplicate = false;
  try {
    model.append(test_command("first", "Duplicate"));
  } catch (const std::invalid_argument &) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);

  bool rejected_checked = false;
  try {
    model.append(test_command("invalid-checked", "Invalid", true, false, true));
  } catch (const std::invalid_argument &) {
    rejected_checked = true;
  }
  assert(rejected_checked);

  model.erase(first);
  assert(!model.contains(first));
  assert(model.index_of(checked) == 1);
}

void test_tool_bar_layout_activation_capture_and_model_lifetime() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CommandModel>();
  const CommandId save = model->append(CommandData{"save",
                                                   "Save",
                                                   "S",
                                                   "Ctrl+S",
                                                   "Save scene",
                                                   CommandKind::Action,
                                                   true,
                                                   false,
                                                   false,
                                                   0,
                                                   {}});
  model->append(CommandData{"separator",
                            {},
                            {},
                            {},
                            {},
                            CommandKind::Separator,
                            true,
                            false,
                            false,
                            0,
                            {}});
  const CommandId snap = model->append(CommandData{"snap",
                                                   "Snap",
                                                   {},
                                                   {},
                                                   "Toggle snap",
                                                   CommandKind::Action,
                                                   true,
                                                   true,
                                                   false,
                                                   0,
                                                   {}});
  model->append(test_command("disabled", "Disabled", false));
  std::weak_ptr<CommandModel> weak_model = model;
  auto &toolbar = ui.make_root<ToolBar>(model);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 40.0f});
  assert(toolbar.item_rects().size() == 4);
  assert(toolbar.item_rects()[0].width > toolbar.item_height());
  assert(toolbar.item_rects()[1].width < toolbar.item_height());
  toolbar.set_centered(true);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 40.0f});
  const auto &centered_rects = toolbar.item_rects();
  const float content_center =
      (centered_rects.front().x + centered_rects.back().x +
       centered_rects.back().width) *
      0.5f;
  assert(std::abs(content_center - 180.0f) < 0.01f);
  toolbar.set_centered(false);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 360.0f, 40.0f});

  std::vector<CommandId> activated;
  toolbar.activated().connect(
      [&activated](ToolBar &, size_t, CommandId id, const CommandData &) {
        activated.push_back(id);
      });
  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = toolbar.item_rects()[0].x + 2.0f;
  pointer.y = 20.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(toolbar.hovered_tooltip() == "Save scene");
  pointer.type = TC_UI_POINTER_DOWN;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), toolbar.handle()));
  pointer.type = TC_UI_POINTER_UP;
  pointer.x = 500.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(activated.empty());
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = toolbar.item_rects()[2].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(activated == std::vector<CommandId>{snap});
  assert(model->command(snap).data.checked);

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = toolbar.item_rects()[3].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  assert(activated.size() == 1);

  model->set_enabled(save, false);
  assert(!model->command(save).data.enabled);
  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = toolbar.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  tc_ui_document_destroy(document_handle);
}

void test_status_bar_explicit_message_lifecycle_and_utf8_validation() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &status = ui.make_root<StatusBar>("Ready");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 300.0f, 24.0f});
  assert(status.displayed_text() == "Ready");
  status.show_message("Saved ✓");
  assert(status.has_message());
  assert(status.displayed_text() == "Saved ✓");
  status.set_text("Idle");
  assert(status.displayed_text() == "Saved ✓");
  status.clear_message();
  assert(status.displayed_text() == "Idle");

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_FILL_RECT) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_LINE) == 1);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) == 1);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  bool rejected_utf8 = false;
  try {
    status.show_message(std::string("\xff", 1));
  } catch (const std::invalid_argument &) {
    rejected_utf8 = true;
  }
  assert(rejected_utf8);

  tc_ui_document_destroy(document_handle);
}

void test_menu_overlay_navigation_submenus_scrolling_and_dismissal() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto recent = std::make_shared<CommandModel>();
  const CommandId recent_scene = recent->append(CommandData{"recent-scene",
                                                            "Scene.termin",
                                                            {},
                                                            {},
                                                            {},
                                                            CommandKind::Action,
                                                            true,
                                                            false,
                                                            false,
                                                            0,
                                                            {}});
  auto model = std::make_shared<CommandModel>();
  model->append(test_command("disabled", "Disabled", false));
  model->append(CommandData{"separator",
                            {},
                            {},
                            {},
                            {},
                            CommandKind::Separator,
                            true,
                            false,
                            false,
                            0,
                            {}});
  model->append(CommandData{"recent",
                            "Recent",
                            {},
                            {},
                            {},
                            CommandKind::Action,
                            true,
                            false,
                            false,
                            0,
                            recent});
  model->append(CommandData{"snap",
                            "Snap",
                            {},
                            {},
                            {},
                            CommandKind::Action,
                            true,
                            true,
                            false,
                            0,
                            {}});
  auto &menu = ui.make<Menu>(model);
  menu.set_max_visible_height(64.0f);
  std::string activated;
  menu.activated().connect(
      [&activated](Menu &, size_t, CommandId, const CommandData &command) {
        activated = command.stable_id;
      });
  assert(menu.show(document.get(), tc_ui_point{390.0f, 290.0f},
                   tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f}));
  assert(document.overlay_count() == 1);
  assert(menu.bounds().x + menu.bounds().width <= 400.0f);
  assert(menu.bounds().y + menu.bounds().height <= 300.0f);
  assert(menu.content_height() > menu.bounds().height);

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(menu.current_index() == 2);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 2);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == "recent-scene");
  assert(document.overlay_count() == 0);

  assert(menu.show(document.get(), tc_ui_point{10.0f, 10.0f},
                   tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f}));
  tc_ui_pointer_event wheel{};
  wheel.type = TC_UI_POINTER_WHEEL;
  wheel.x = 20.0f;
  wheel.y = 20.0f;
  wheel.wheel_y = -1.0f;
  assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
  assert(menu.scroll_offset() > 0.0f);
  tc_ui_pointer_event outside{};
  outside.type = TC_UI_POINTER_DOWN;
  outside.x = 350.0f;
  outside.y = 250.0f;
  assert(document.dispatch_pointer_event(outside) == TC_UI_EVENT_HANDLED);
  assert(!menu.open());
  assert(document.overlay_count() == 0);
  assert(recent->contains(recent_scene));

  tc_ui_document_destroy(document_handle);
}

void test_menu_bar_adjacent_switching_shortcuts_and_overlay_lifetime() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto file = std::make_shared<CommandModel>();
  file->append(CommandData{"save",
                           "Save",
                           {},
                           "Ctrl+S",
                           {},
                           CommandKind::Action,
                           true,
                           true,
                           false,
                           0,
                           {}});
  auto edit = std::make_shared<CommandModel>();
  const CommandId undo = edit->append(CommandData{"undo",
                                                  "Undo",
                                                  {},
                                                  "Ctrl+Z",
                                                  {},
                                                  CommandKind::Action,
                                                  true,
                                                  false,
                                                  false,
                                                  0,
                                                  {}});
  auto &root = ui.make_root<BoxLayout>(Orientation::Vertical, "menu-bar-root");
  auto &bar = ui.make<MenuBar>();
  root.add_fixed_child(bar, 30.0f);
  bar.set_entries({{"file", "File", file}, {"edit", "Edit", edit}});
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 400.0f, 300.0f});
  assert(bar.item_rects().size() == 2);
  std::vector<std::string> activated;
  bar.activated().connect(
      [&activated](MenuBar &, size_t, CommandId, const CommandData &command) {
        activated.push_back(command.stable_id);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = bar.item_rects()[0].x + 2.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 0);
  assert(document.overlay_count() == 1);
  assert((tc_ui_document_overlay_flags_at(document.get(), 0) &
          TC_UI_OVERLAY_ALLOW_ROOT_HIT) != 0);
  const tc_widget_handle anchor_hit = tc_ui_document_hit_test(
      document.get(), bar.item_rects()[1].x + 2.0f, 10.0f);
  assert(!tc_widget_handle_is_invalid(anchor_hit));
  assert(tc_widget_handle_eq(anchor_hit, bar.handle()));
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED);
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = bar.item_rects()[1].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 1);
  assert(document.overlay_count() == 1);
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = bar.item_rects()[0].x + 2.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(bar.open_index() == 0);
  assert(document.overlay_count() == 1);

  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(!bar.menu_open());
  assert(document.overlay_count() == 0);
  assert(bar.dispatch_shortcut('s', TC_UI_MOD_CTRL));
  assert(file->command_at(0).data.checked);
  assert(bar.dispatch_shortcut('Z', TC_UI_MOD_CTRL));
  assert(activated == std::vector<std::string>({"save", "undo"}));
  assert(edit->contains(undo));

  const tc_widget_handle bar_handle = bar.handle();
  assert(tc_ui_document_destroy_widget(document.get(), bar_handle));

  tc_ui_document_destroy(document_handle);
}

void test_dialog_modal_stack_focus_actions_and_exactly_once_results() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &background = ui.make_root<TextInput>("Background");
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f});
  assert(document.set_focus(background));

  auto &dialog = ui.make<Dialog>("Confirm");
  auto &content = ui.make<Label>("Apply changes?");
  const tc_widget_handle content_handle = content.handle();
  dialog.set_content(content);
  dialog.set_actions({
      DialogAction{"apply", "Apply", true, false},
      DialogAction{"cancel", "Cancel", false, true},
  });
  std::vector<DialogResult> results;
  dialog.finished().connect([&results](Dialog &, const DialogResult &result) {
    results.push_back(result);
  });
  assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(document.overlay_count() == 1);
  assert(dialog.button_handles().size() == 2);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[0]));
  assert(dialog.bounds().x > 0.0f && dialog.bounds().y > 0.0f);

  tc_ui_key_event tab{};
  tab.type = TC_UI_KEY_DOWN;
  tab.key = TC_UI_KEY_TAB;
  assert(document.dispatch_key_event(tab) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[1]));

  auto &nested = ui.make<Dialog>("Nested");
  nested.set_actions({
      DialogAction{"ok", "OK", true, false},
      DialogAction{"back", "Back", false, true},
  });
  std::vector<DialogResult> nested_results;
  nested.finished().connect(
      [&nested_results](Dialog &, const DialogResult &result) {
        nested_results.push_back(result);
      });
  assert(nested.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(document.overlay_count() == 2);
  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 1);
  assert(nested_results.size() == 1);
  assert(nested_results[0].action_id == "back");
  assert(nested_results[0].reason == DialogDismissReason::Escape);
  assert(tc_widget_handle_eq(document.focused_widget(),
                             dialog.button_handles()[1]));

  tc_widget *default_button_widget =
      tc_ui_document_resolve_widget(document.get(), dialog.button_handles()[0]);
  auto *default_button = dynamic_cast<Button *>(
      static_cast<Widget *>(default_button_widget->body));
  assert(default_button && document.set_focus(*default_button));
  tc_ui_key_event enter{};
  enter.type = TC_UI_KEY_DOWN;
  enter.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(enter) == TC_UI_EVENT_HANDLED);
  assert(document.overlay_count() == 0);
  assert(results.size() == 1);
  assert(results[0].action_id == "apply");
  assert(results[0].reason == DialogDismissReason::Action);
  assert(tc_widget_handle_eq(document.focused_widget(), background.handle()));

  assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  assert(dialog.close(document.get()));
  assert(results.size() == 2);
  assert(results[1].action_id.empty());
  assert(results[1].reason == DialogDismissReason::Programmatic);
  assert(!dialog.close(document.get()));
  assert(results.size() == 2);

  assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 800.0f, 600.0f}));
  tc_widget *apply_widget =
      tc_ui_document_resolve_widget(document.get(), dialog.button_handles()[0]);
  assert(apply_widget);
  tc_ui_pointer_event click{};
  click.type = TC_UI_POINTER_DOWN;
  click.button = 0;
  click.x = apply_widget->bounds.x + apply_widget->bounds.width * 0.5f;
  click.y = apply_widget->bounds.y + apply_widget->bounds.height * 0.5f;
  assert(tc_widget_handle_eq(document.hit_test(click.x, click.y),
                             dialog.button_handles()[0]));
  assert(document.dispatch_pointer_event(click) == TC_UI_EVENT_HANDLED);
  click.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(click) == TC_UI_EVENT_HANDLED);
  assert(results.size() == 3);
  assert(results[2].action_id == "apply");
  assert(results[2].reason == DialogDismissReason::Action);
  assert(document.overlay_count() == 0);

  const tc_widget_handle dialog_handle = dialog.handle();
  assert(tc_ui_document_destroy_widget(document.get(), dialog_handle));
  assert(!tc_ui_document_is_alive(document.get(), content_handle));

  tc_ui_document_destroy(document_handle);
}

void test_message_box_and_input_dialog_share_modal_result_contract() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto &message = ui.make<MessageBox>("Delete", "Delete selected entity?",
                                      MessageBoxKind::Question);
  std::vector<DialogResult> message_results;
  message.finished().connect(
      [&message_results](Dialog &, const DialogResult &result) {
        message_results.push_back(result);
      });
  assert(message.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(message.child_count() == 3);
  tc_ui_key_event escape{};
  escape.type = TC_UI_KEY_DOWN;
  escape.key = TC_UI_KEY_ESCAPE;
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(message_results.size() == 1);
  assert(message_results[0].action_id == "no");
  assert(message_results[0].reason == DialogDismissReason::Escape);

  auto &input = ui.make<InputDialog>("Rename", "New name", "Old name");
  std::vector<std::optional<std::string>> values;
  input.value_finished().connect(
      [&values](InputDialog &, const std::optional<std::string> &value) {
        values.push_back(value);
      });
  assert(input.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(input.value() == "Old name");
  input.set_value("New name");
  assert(input.value() == "New name");
  tc_ui_key_event enter{};
  enter.type = TC_UI_KEY_DOWN;
  enter.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(enter) == TC_UI_EVENT_HANDLED);
  assert(values.size() == 1 &&
         values[0] == std::optional<std::string>{"New name"});
  assert(!input.open());

  assert(input.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
  assert(document.dispatch_key_event(escape) == TC_UI_EVENT_HANDLED);
  assert(values.size() == 2 && !values[1].has_value());

  tc_ui_document_destroy(document_handle);
}

void test_file_grid_widget_input_scrollbar_signals_and_lifetime() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<CollectionModel>();
  for (size_t index = 0; index < 20; ++index) {
    model->append(CollectionItem{"file-" + std::to_string(index),
                                 "File " + std::to_string(index), ".txt",
                                 index != 3});
  }
  std::weak_ptr<CollectionModel> weak_model = model;
  auto &grid = ui.make_root<FileGridWidget>(model);
  grid.set_tile_size(50.0f, 30.0f);
  grid.set_tile_spacing(0.0f);
  grid.set_padding(0.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 120.0f, 90.0f});
  assert(grid.column_count() == 2);

  std::vector<size_t> activated;
  std::vector<size_t> deleted;
  std::vector<int64_t> contexts;
  grid.activated().connect(
      [&activated](FileGridWidget &, size_t index, const CollectionItem &) {
        activated.push_back(index);
      });
  grid.delete_requested().connect(
      [&deleted](FileGridWidget &, size_t index, const CollectionItem &) {
        deleted.push_back(index);
      });
  grid.context_menu_requested().connect(
      [&contexts](FileGridWidget &, int64_t index, float, float) {
        contexts.push_back(index);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.button = 0;
  pointer.x = 10.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 0);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_DOWN;

  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 2);
  key.key = TC_UI_KEY_RIGHT;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(grid.selection().current() == 4);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  key.key = TC_UI_KEY_DELETE;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == std::vector<size_t>{4});
  assert(deleted == std::vector<size_t>{4});

  pointer.button = 1;
  pointer.x = 10.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts == std::vector<int64_t>{0});
  pointer.x = 105.0f;
  pointer.y = 85.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts.back() == -1);

  std::vector<std::tuple<size_t, float, float, int32_t>> drags;
  grid.drag_requested().connect([&drags](FileGridWidget &, size_t index,
                                         float x, float y, int32_t modifiers) {
    drags.emplace_back(index, x, y, modifiers);
  });
  pointer.button = 0;
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.modifiers = TC_UI_MOD_CTRL;
  pointer.x = 10.0f;
  pointer.y = 10.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = 160.0f;
  pointer.y = 140.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert((drags == std::vector<std::tuple<size_t, float, float, int32_t>>{
                       {0, 160.0f, 140.0f, TC_UI_MOD_CTRL}}));

  pointer.button = 0;
  pointer.modifiers = 0;
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 118.0f;
  pointer.y = 5.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), grid.handle()));
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.y = 50.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(grid.scroll_y() > 0.0f);
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  model.reset();
  assert(!weak_model.expired());
  const tc_widget_handle handle = grid.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());

  auto destroying_model = std::make_shared<CollectionModel>();
  destroying_model->set_items({
      CollectionItem{"first", "First", {}, true},
      CollectionItem{"last", "Last", {}, true},
  });
  auto &destroying_grid = ui.make_root<FileGridWidget>(destroying_model);
  assert(destroying_grid.select_index(1));
  const tc_widget_handle destroying_handle = destroying_grid.handle();
  destroying_grid.selection_changed().connect([&document, destroying_handle](
                                                  FileGridWidget &,
                                                  const std::vector<size_t> &) {
    assert(tc_ui_document_destroy_widget(document.get(), destroying_handle));
  });
  destroying_model->erase(0);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));

  tc_ui_document_destroy(document_handle);
}

void test_table_widget_virtualizes_large_model_and_lays_out_columns() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TableModel>();
  std::vector<TableRowData> rows;
  rows.reserve(10000);
  for (size_t index = 0; index < 10000; ++index) {
    rows.push_back(TableRowData{
        "row-" + std::to_string(index),
        {"Row " + std::to_string(index), std::to_string(index), "Ready"},
        true,
    });
  }
  model->set_rows(std::move(rows));
  auto columns = std::make_shared<TableColumnModel>();
  columns->set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 80.0f, 60.0f, 0.0f,
                  1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
      TableColumn{"state", "State", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  160.0f, 2.0f, true},
  });
  auto &table = ui.make_root<TableWidget>(model, columns);
  table.set_row_height(24.0f);
  table.set_header_height(28.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 400.0f, 128.0f});

  assert(table.child_count() == 0);
  assert(table.content_height() == 240000.0f);
  const auto [first, last] = table.visible_range();
  assert(first == 0);
  assert(last <= 6);
  const auto &layout = table.column_layout();
  assert(layout.size() == 3);
  assert(near(layout[0].width, 80.0f));
  assert(layout[1].width >= 40.0f);
  assert(near(layout[2].width, 160.0f));
  assert(near(layout[2].x + layout[2].width, 400.0f));

  tc_ui_draw_list *draw_list = tc_ui_draw_list_create();
  tc_ui_paint_context *context = tc_ui_paint_context_create(draw_list);
  document.paint_roots(context);
  assert(count_commands(draw_list, TC_UI_DRAW_TEXT) <= 21);
  tc_ui_paint_context_destroy(context);
  tc_ui_draw_list_destroy(draw_list);

  assert(table.select_row(9999));
  assert(table.scroll_y() > 239000.0f);
  assert(table.visible_range().first > 9990);

  tc_ui_document_destroy(document_handle);
}

void test_table_widget_pointer_keyboard_resize_signals_and_lifetime() {
  tc_ui_document_handle document_handle = tc_ui_document_create();
  TcDocument document(document_handle);
  install_test_text_measurer(document);
  DocumentBuilder ui(document);
  auto model = std::make_shared<TableModel>();
  const TableRowId first =
      model->append(TableRowData{"first", {"First", "1"}, true});
  const TableRowId disabled =
      model->append(TableRowData{"disabled", {"Disabled", "2"}, false});
  const TableRowId last =
      model->append(TableRowData{"last", {"Last", "3"}, true});
  auto columns = std::make_shared<TableColumnModel>();
  columns->set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Fixed, 100.0f, 60.0f,
                  180.0f, 1.0f, true},
      TableColumn{"value", "Value", TableColumnPolicy::Stretch, 0.0f, 40.0f,
                  0.0f, 1.0f, true},
  });
  std::weak_ptr<TableModel> weak_model = model;
  std::weak_ptr<TableColumnModel> weak_columns = columns;
  auto &table = ui.make_root<TableWidget>(model, columns);
  table.set_row_height(30.0f);
  table.set_header_height(30.0f);
  document.layout_roots(tc_ui_rect{0.0f, 0.0f, 260.0f, 120.0f});

  size_t clicked_column = SIZE_MAX;
  size_t resized_column = SIZE_MAX;
  float resized_width = 0.0f;
  TableRowId activated = kInvalidTableRowId;
  std::vector<int64_t> contexts;
  table.header_clicked().connect(
      [&clicked_column](TableWidget &, size_t index, const TableColumn &) {
        clicked_column = index;
      });
  table.column_resized().connect([&resized_column, &resized_width](
                                     TableWidget &, size_t index, float width) {
    resized_column = index;
    resized_width = width;
  });
  table.activated().connect(
      [&activated](TableWidget &, size_t, TableRowId id, const TableRowData &) {
        activated = id;
      });
  table.context_menu_requested().connect(
      [&contexts](TableWidget &, int64_t index, float, float) {
        contexts.push_back(index);
      });

  tc_ui_pointer_event pointer{};
  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 20.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(clicked_column == 0);

  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(table.selection().current() == 0);
  tc_ui_key_event key{};
  key.type = TC_UI_KEY_DOWN;
  key.key = TC_UI_KEY_DOWN_ARROW;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(table.selection().current() == 2);
  key.key = TC_UI_KEY_ENTER;
  assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
  assert(activated == last);

  pointer.button = 1;
  pointer.y = 45.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(contexts == std::vector<int64_t>{0});
  assert(table.clear_selection());
  pointer.button = 0;

  pointer.type = TC_UI_POINTER_DOWN;
  pointer.x = 100.0f;
  pointer.y = 15.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_eq(document.pointer_capture(), table.handle()));
  pointer.type = TC_UI_POINTER_MOVE;
  pointer.x = 140.0f;
  pointer.y = -50.0f;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(resized_column == 0);
  assert(near(resized_width, 140.0f));
  pointer.type = TC_UI_POINTER_UP;
  assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
  assert(tc_widget_handle_is_invalid(document.pointer_capture()));

  assert(table.select_row(0));
  model->insert(0, TableRowData{"inserted", {"Inserted", "0"}, true});
  assert(table.selection().current() == 1);
  model->erase(first);
  assert(table.selection().selected_indices().empty());
  assert(model->contains(disabled));

  model.reset();
  columns.reset();
  assert(!weak_model.expired());
  assert(!weak_columns.expired());
  const tc_widget_handle handle = table.handle();
  assert(tc_ui_document_destroy_widget(document.get(), handle));
  assert(weak_model.expired());
  assert(weak_columns.expired());

  auto destroying_model = std::make_shared<TableModel>();
  const TableRowId destroying_first = destroying_model->append(
      TableRowData{"destroying-first", {"First"}, true});
  destroying_model->append(TableRowData{"destroying-last", {"Last"}, true});
  auto &destroying_table = ui.make_root<TableWidget>(destroying_model);
  assert(destroying_table.select_row(1));
  const tc_widget_handle destroying_handle = destroying_table.handle();
  destroying_table.selection_changed().connect(
      [&document, destroying_handle](TableWidget &,
                                     const std::vector<size_t> &) {
        assert(
            tc_ui_document_destroy_widget(document.get(), destroying_handle));
      });
  destroying_model->erase(destroying_first);
  assert(!tc_ui_document_is_alive(document.get(), destroying_handle));

  tc_ui_document_destroy(document_handle);
}

void test_host_click_count_drives_collection_activation() {
  {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    DocumentBuilder ui(document);
    auto model = std::make_shared<CollectionModel>();
    model->append(CollectionItem{"item", "Item"});
    auto &list = ui.make_root<ListWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 80.0f});
    size_t activations = 0;
    list.activated().connect(
        [&activations](ListWidget &, size_t, const CollectionItem &) {
          ++activations;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.x = 10.0f;
    pointer.y = 10.0f;
    pointer.click_count = 1;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 0);
    pointer.click_count = 2;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
    pointer.click_count = 3;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
    tc_ui_document_destroy(document_handle);
  }
  {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    DocumentBuilder ui(document);
    auto model = std::make_shared<TreeModel>();
    model->append_root(CollectionItem{"first", "First"});
    const TreeNodeId node =
        model->append_root(CollectionItem{"second", "Second"});
    auto &tree = ui.make_root<TreeWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 80.0f});
    TreeNodeId activated = kInvalidTreeNodeId;
    tree.activated().connect(
        [&activated](TreeWidget &, TreeNodeId id, const CollectionItem &) {
          activated = id;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 1;
    pointer.x = 30.0f;
    pointer.y = 10.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    pointer.click_count = 2;
    pointer.y = 38.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activated == kInvalidTreeNodeId);
    pointer.click_count = 3;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activated == node);
    tc_ui_document_destroy(document_handle);
  }
  {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    DocumentBuilder ui(document);
    auto model = std::make_shared<TableModel>();
    const TableRowId row = model->append(TableRowData{"row", {"Row"}, true});
    auto columns = std::make_shared<TableColumnModel>();
    columns->set_columns({TableColumn{
        "value", "Value", TableColumnPolicy::Stretch, 0.0f, 20.0f}});
    auto &table = ui.make_root<TableWidget>(model, columns);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 100.0f});
    TableRowId activated = kInvalidTableRowId;
    table.activated().connect(
        [&activated](TableWidget &, size_t, TableRowId id,
                     const TableRowData &) { activated = id; });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 1;
    pointer.x = 10.0f;
    pointer.y = 40.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    pointer.click_count = 2;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activated == row);
    tc_ui_document_destroy(document_handle);
  }
  {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    DocumentBuilder ui(document);
    auto model = std::make_shared<CollectionModel>();
    model->append(CollectionItem{"file", "File"});
    auto &grid = ui.make_root<FileGridWidget>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 120.0f});
    size_t activations = 0;
    grid.activated().connect(
        [&activations](FileGridWidget &, size_t, const CollectionItem &) {
          ++activations;
        });
    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.button = 0;
    pointer.click_count = 1;
    pointer.x = 20.0f;
    pointer.y = 20.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    pointer.click_count = 2;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(activations == 1);
    tc_ui_document_destroy(document_handle);
  }
}

} // namespace termin_gui_native_test
