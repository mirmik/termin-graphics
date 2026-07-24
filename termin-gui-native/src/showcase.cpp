#include <termin/gui_native/showcase.hpp>

#include <termin/gui_native/widgets.hpp>

namespace termin::gui_native {

ShowcaseRefs build_showcase(TcDocument document) {
  DocumentBuilder ui(document);
  ShowcaseRefs refs;

  auto &root = ui.make_root<VStack>("showcase-root");
  root.set_padding(EdgeInsets{18.0f, 18.0f, 18.0f, 18.0f})
      .set_spacing(14.0f)
      .set_background(Color{0.055f, 0.060f, 0.070f, 1.0f});

  auto recent_menu = std::make_shared<CommandModel>();
  recent_menu->set_commands({
      CommandData{"recent-scene", "Showcase.termin", {}, {}, {},
                  CommandKind::Action, true, false, false, 0, {}},
  });
  auto file_menu = std::make_shared<CommandModel>();
  file_menu->set_commands({
      CommandData{"new", "New Scene", {}, "Ctrl+N", {}, CommandKind::Action,
                  true, false, false, 0, {}},
      CommandData{"recent", "Open Recent", {}, {}, {}, CommandKind::Action,
                  true, false, false, 0, std::move(recent_menu)},
      CommandData{"file-separator", {}, {}, {}, {}, CommandKind::Separator,
                  true, false, false, 0, {}},
      CommandData{"quit", "Quit", {}, "Ctrl+Q", {}, CommandKind::Action,
                  true, false, false, 0, {}},
  });
  auto view_menu = std::make_shared<CommandModel>();
  view_menu->set_commands({
      CommandData{"grid", "Grid", {}, {}, {}, CommandKind::Action, true, true,
                  true, 0, {}},
  });
  refs.menu_bar = &ui.make<MenuBar>();
  refs.menu_bar->set_entries({
      MenuBarEntry{"file", "File", std::move(file_menu)},
      MenuBarEntry{"view", "View", std::move(view_menu)},
  });
  root.add_fixed_child(*refs.menu_bar, 30.0f);

  auto toolbar_model = std::make_shared<CommandModel>();
  toolbar_model->set_commands({
      CommandData{"save", "Save", "S", "Ctrl+S", "Save scene", CommandKind::Action,
                  true, false, false, 0, {}},
      CommandData{"separator", {}, {}, {}, {}, CommandKind::Separator,
                  true, false, false, 0, {}},
      CommandData{"snap", "Snap", {}, {}, "Toggle snapping", CommandKind::Action,
                  true, true, true, 0, {}},
  });
  refs.toolbar = &ui.make<ToolBar>(std::move(toolbar_model));
  root.add_fixed_child(*refs.toolbar, 40.0f);

  auto &top = ui.make<HStack>("showcase-top");
  top.set_spacing(12.0f);
  root.add_child(top);

  auto &navigation = ui.make<VStack>("navigation");
  navigation.set_padding(EdgeInsets{12.0f, 12.0f, 12.0f, 12.0f})
      .set_spacing(8.0f)
      .set_background(Color{0.10f, 0.11f, 0.13f, 1.0f})
      .set_border(Color{0.28f, 0.30f, 0.34f, 1.0f}, 1.0f);
  top.add_child(navigation);
  navigation.add_child(
      ui.make<Label>("Native UI", 18.0f, Color{0.92f, 0.95f, 1.0f, 1.0f}));
  navigation.add_child(
      ui.make<Button>("Scene", Color{0.18f, 0.32f, 0.54f, 1.0f}));
  navigation.add_child(
      ui.make<Button>("Assets", Color{0.16f, 0.42f, 0.32f, 1.0f}));
  navigation.add_child(
      ui.make<Button>("Build", Color{0.48f, 0.28f, 0.20f, 1.0f}));

  refs.content_scroll = &ui.make<ScrollArea>("content-scroll");
  top.add_child(*refs.content_scroll);

  auto &content = ui.make<VStack>("content");
  content.set_padding(EdgeInsets{14.0f, 14.0f, 14.0f, 14.0f})
      .set_spacing(10.0f)
      .set_background(Color{0.13f, 0.14f, 0.16f, 1.0f})
      .set_border(Color{0.36f, 0.39f, 0.44f, 1.0f}, 1.0f);
  refs.content_scroll->set_content(content);

  auto &preview_split =
      ui.make<Splitter>(Orientation::Horizontal, "preview-split");
  preview_split.set_split_fraction(0.58f)
      .set_min_extents(96.0f, 96.0f)
      .set_divider_thickness(8.0f);
  content.add_child(preview_split);
  preview_split.set_first(
      ui.make<Panel>("preview-a").set_fill(Color{0.20f, 0.42f, 0.62f, 1.0f}));
  preview_split.set_second(
      ui.make<Panel>("preview-b").set_fill(Color{0.26f, 0.48f, 0.34f, 1.0f}));

  content.add_preferred_child(ui.make<Separator>(Orientation::Horizontal));

  auto &controls_group = ui.make<GroupBox>("Controls", "controls-group");
  content.add_preferred_child(controls_group);

  auto &controls = ui.make<HStack>("controls");
  controls.set_spacing(10.0f);
  controls_group.set_content(controls);
  refs.checkbox = &ui.make<Checkbox>(true);
  refs.slider = &ui.make<Slider>(0.62f);
  refs.progress = &ui.make<ProgressBar>(0.35f);
  refs.text_input = &ui.make<TextInput>("Scene 01");
  controls.add_child(
      ui.make<Label>("Live", 14.0f, Color{0.78f, 0.84f, 0.92f, 1.0f}));
  controls.add_child(*refs.checkbox);
  controls.add_child(*refs.slider);
  controls.add_child(*refs.progress);
  controls.add_child(*refs.text_input);
  refs.slider->changed().connect(
      [progress = refs.progress](Slider &, float value) {
        progress->set_value(value);
      });

  refs.text_area =
      &ui.make<TextArea>("Native TextArea\nUTF-8 selection and host "
                         "clipboard\nHorizontal and vertical scrolling");
  content.add_fixed_child(*refs.text_area, 96.0f);

  auto list_model = std::make_shared<CollectionModel>();
  list_model->set_items({
      CollectionItem{"scene", "Scene hierarchy", "Reusable collection model",
                     true},
      CollectionItem{"assets", "Asset browser", {}, true},
      CollectionItem{"disabled", "Unavailable source", "Disabled item", false},
      CollectionItem{"build", "Build output", {}, true},
  });
  refs.list = &ui.make<ListWidget>(std::move(list_model));
  refs.list->set_selection_mode(SelectionMode::Multiple);
  refs.list->select_index(1);
  content.add_fixed_child(*refs.list, 112.0f);

  auto tree_model = std::make_shared<TreeModel>();
  const TreeNodeId scene =
      tree_model->append_root(CollectionItem{"scene-root", "Scene", {}, true});
  const TreeNodeId camera = tree_model->append_child(
      scene, CollectionItem{"camera", "Camera", {}, true});
  tree_model->append_child(scene,
                           CollectionItem{"light", "Key Light", {}, true});
  auto tree_expansion = std::make_shared<TreeExpansionModel>();
  tree_expansion->set_expanded(scene, true);
  refs.tree =
      &ui.make<TreeWidget>(std::move(tree_model), std::move(tree_expansion));
  refs.tree->select_node(camera);
  content.add_fixed_child(*refs.tree, 96.0f);

  auto table_model = std::make_shared<TableModel>();
  table_model->set_rows({
      TableRowData{"camera", {"Camera", "Scene", "Active"}, true},
      TableRowData{"light", {"Key Light", "Scene", "Active"}, true},
      TableRowData{"mesh", {"Preview Mesh", "Assets", "Imported"}, true},
  });
  auto table_columns = std::make_shared<TableColumnModel>();
  table_columns->set_columns({
      TableColumn{"name", "Name", TableColumnPolicy::Stretch, 0.0f, 100.0f,
                  0.0f, 2.0f, true},
      TableColumn{"source", "Source", TableColumnPolicy::Stretch, 0.0f,
                  72.0f, 0.0f, 1.0f, true},
      TableColumn{"state", "State", TableColumnPolicy::Fixed, 88.0f, 64.0f,
                  120.0f, 1.0f, true},
  });
  refs.table = &ui.make<TableWidget>(std::move(table_model),
                                     std::move(table_columns));
  refs.table->select_row(0);
  content.add_fixed_child(*refs.table, 116.0f);

  auto file_grid_model = std::make_shared<CollectionModel>();
  file_grid_model->set_items({
      CollectionItem{"scene-file", "Scene", ".scene", true},
      CollectionItem{"mesh-file", "Robot", ".glb", true},
      CollectionItem{"material-file", "Steel", ".material", true},
      CollectionItem{"script-file", "Controller", ".py", true},
  });
  refs.file_grid = &ui.make<FileGridWidget>(std::move(file_grid_model));
  refs.file_grid->set_tile_size(76.0f, 64.0f);
  refs.file_grid->set_icon_size(16.0f);
  refs.file_grid->select_index(0);
  content.add_fixed_child(*refs.file_grid, 84.0f);

  auto &palette = ui.make<GridLayout>("palette-grid");
  palette.set_padding(EdgeInsets{8.0f, 8.0f, 8.0f, 8.0f})
      .set_spacing(8.0f, 8.0f)
      .set_background(Color{0.10f, 0.11f, 0.13f, 1.0f})
      .set_border(Color{0.30f, 0.32f, 0.36f, 1.0f}, 1.0f);
  palette.add_row(LayoutPolicy::Preferred);
  palette.add_column(LayoutPolicy::Stretch);
  palette.add_column(LayoutPolicy::Stretch);
  palette.add_column(LayoutPolicy::Stretch);
  palette.add_column(LayoutPolicy::Stretch);
  content.add_preferred_child(palette);
  palette.add_child(ui.make<Swatch>(Color{0.90f, 0.22f, 0.24f, 1.0f}), 0, 0);
  palette.add_child(ui.make<Swatch>(Color{0.95f, 0.72f, 0.28f, 1.0f}), 0, 1);
  palette.add_child(ui.make<Swatch>(Color{0.24f, 0.68f, 0.48f, 1.0f}), 0, 2);
  palette.add_child(ui.make<Swatch>(Color{0.28f, 0.52f, 0.92f, 1.0f}), 0, 3);

  refs.tabs = &ui.make<TabView>("bottom-tabs");
  root.add_child(*refs.tabs);
  refs.tabs->add_page(
      "Status",
      ui.make<Panel>("status-a").set_fill(Color{0.12f, 0.20f, 0.26f, 1.0f}));
  refs.tabs->add_page(
      "Output",
      ui.make<Panel>("status-b").set_fill(Color{0.22f, 0.17f, 0.28f, 1.0f}));

  refs.status_bar = &ui.make<StatusBar>("Ready | Native UI");
  root.add_fixed_child(*refs.status_bar, 24.0f);

  refs.message_box = &ui.make<MessageBox>(
      "Native Dialog", "Modal focus and result delivery", MessageBoxKind::Information);

  return refs;
}

} // namespace termin::gui_native
