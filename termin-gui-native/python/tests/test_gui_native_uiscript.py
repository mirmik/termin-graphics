from pathlib import Path
import sys

import pytest

from termin.gui_native import (
    tc_ui_document_create,
    DrawList,
    PaintContext,
    PointerEvent,
    PointerEventType,
    Rect,
    UiDocumentAsset,
    UiScriptError,
    UiScriptLoader,
)


CAMERA_SCRIPT = """
uiscript: 2
root:
  type: termin.gui.OverlayLayout
  name: overlay
  background_color: [0, 0, 0, 0]
  children:
    - type: termin.gui.HStack
      name: controls
      spacing: 4
      children:
        - type: termin.gui.IconButton
          name: inspect_btn
          icon: I
          tooltip: Inspect
          size: 26
          font_size: 14
          background_color: [0.15, 0.15, 0.15, 0.85]
          hover_color: [0.25, 0.25, 0.25, 0.95]
          active_color: [0.2, 0.5, 0.8, 0.95]
          border_radius: 4
"""


def test_uiscript_v2_parses_to_native_description():
    description = UiScriptLoader().parser.parse(CAMERA_SCRIPT)

    assert description.version == 2
    assert description.root.type_name == "termin.gui.OverlayLayout"
    assert description.type_dependencies == [
        "termin.gui.OverlayLayout",
        "termin.gui.HStack",
        "termin.gui.IconButton",
    ]
    button = description.root.children[0].children[0]
    assert button.name == "inspect_btn"
    assert button.properties["tooltip"] == "Inspect"


@pytest.mark.parametrize(
    ("source", "message"),
    [
        (
            "root: {type: termin.gui.Panel}",
            "document.uiscript: expected dialect version 2",
        ),
        (
            "uiscript: 1\nroot: {type: termin.gui.Panel}",
            "expected dialect version 2",
        ),
        (
            "uiscript: 2\nroot: {type: termin.gui.Mystery}",
            "unknown registered widget type",
        ),
        (
            "uiscript: 2\nroot: {type: termin.gui.Panel, mystery: 1}",
            "unsupported termin.gui.Panel property",
        ),
        (
            "uiscript: 2\nroot: {type: termin.gui.Panel, children: "
            "[{type: termin.gui.Panel, name: x}, "
            "{type: termin.gui.Panel, name: x}]}",
            "duplicate widget name 'x'",
        ),
    ],
)
def test_uiscript_v2_rejects_unsupported_or_ambiguous_input(source, message):
    with pytest.raises(UiScriptError, match=message):
        UiScriptLoader().parser.parse(source)


def test_uiscript_materialization_lookup_reload_and_teardown():
    document = tc_ui_document_create()
    loader = UiScriptLoader()
    loaded = loader.load_string(CAMERA_SCRIPT, document=document)

    assert document.live_widget_count == 3
    assert document.root_count == 1
    assert loaded.root.widget.stable_id == "overlay"
    assert loaded.named("inspect_btn").widget.name == "inspect_btn"
    assert loaded.named("inspect_btn").tooltip == "Inspect"
    assert loaded.named("inspect_btn").widget.preferred_size.width == pytest.approx(26.0)
    assert loaded.widgets["inspect_btn"].properties["active_color"] == pytest.approx(
        [0.2, 0.5, 0.8, 0.95]
    )

    old_root = loaded.root.widget
    replacement_source = CAMERA_SCRIPT.replace("inspect_btn", "replacement_btn")
    replacement = loader.reload(loaded, replacement_source)
    assert document.live_widget_count == 3
    assert not old_root.alive
    assert replacement.named("replacement_btn").widget.alive

    replacement.close()
    replacement.close()
    assert document.live_widget_count == 0
    assert document.root_count == 0


def test_uiscript_icon_button_materializes_visual_states():
    source = """
uiscript: 2
root:
  type: termin.gui.IconButton
  name: stateful
  icon: S
  background_color: [0.1, 0.2, 0.3, 1]
  hover_color: [0.2, 0.3, 0.4, 1]
  active_color: [0.3, 0.4, 0.5, 1]
  size: 32
"""
    loaded = UiScriptLoader().load_string(source)
    document = loaded.document
    button = loaded.named("stateful")
    document.layout_roots(Rect(0.0, 0.0, 32.0, 32.0))

    def background():
        draw_list = DrawList()
        document.paint_roots(PaintContext(draw_list))
        color = draw_list.commands[0].color
        return color.r, color.g, color.b, color.a

    assert background() == pytest.approx((0.1, 0.2, 0.3, 1.0))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Move
    pointer.x = 8.0
    pointer.y = 8.0
    document.dispatch_pointer_event(pointer)
    assert background() == pytest.approx((0.2, 0.3, 0.4, 1.0))
    button.active = True
    assert background() == pytest.approx((0.3, 0.4, 0.5, 1.0))
    loaded.close()


def test_uiscript_failed_reload_preserves_old_tree_and_cleans_attempt():
    document = tc_ui_document_create()
    loader = UiScriptLoader()
    loaded = loader.load_string(CAMERA_SCRIPT, document=document)

    with pytest.raises(UiScriptError, match="unsupported termin.gui.OverlayLayout property"):
        loader.reload(loaded, CAMERA_SCRIPT.replace("background_color:", "unknown_color:"))

    assert document.live_widget_count == 3
    assert loaded.root.widget.alive
    loaded.close()


def test_ui_document_asset_compiles_roundtrips_and_reloads_transactionally():
    UiDocumentAsset.clear_registry_for_tests()
    compiled = UiDocumentAsset.compile_source_json(
        "test-native-ui",
        "Test native UI",
        "UI/test.uiscript",
        CAMERA_SCRIPT,
    )
    asset = UiDocumentAsset.declare_compiled_json(
        compiled, expected_uuid="test-native-ui"
    )

    assert asset.valid
    assert asset.handle.valid
    assert asset.uuid == "test-native-ui"
    assert asset.source_identity == "UI/test.uiscript"
    assert asset.revision == 1
    assert asset.type_dependencies == [
        "termin.gui.OverlayLayout",
        "termin.gui.HStack",
        "termin.gui.IconButton",
    ]
    first = asset.instantiate()
    second = asset.instantiate()
    assert first.document.live_widget_count == 3
    assert second.document.live_widget_count == 3

    assert not asset.reload_source(
        CAMERA_SCRIPT.replace("background_color:", "unknown_color:")
    )
    assert asset.revision == 1
    assert first.named("inspect_btn").widget.alive

    replacement_source = CAMERA_SCRIPT.replace("inspect_btn", "replacement_btn")
    assert asset.reload_source(replacement_source)
    assert asset.revision == 2
    replacement = asset.reload_instance(first)
    assert replacement.named("replacement_btn").widget.alive
    assert not first.root.widget.alive
    assert second.named("inspect_btn").widget.alive

    replacement.close()
    second.close()
    assert asset.remove()
    assert not asset.valid
    UiDocumentAsset.clear_registry_for_tests()


def test_editor_camera_uiscript_is_in_the_supported_v2_dialect():
    root = Path(__file__).resolve().parents[3]
    script = root / "termin-stdlib/python/termin/stdlib/resources/uiscript/editor_camera_ui.uiscript"
    description = UiScriptLoader().parser.parse(script.read_text(encoding="utf-8"))
    assert description.root.name == "editor_camera_root"
    assert {child.name for child in description.root.children} == {
        "bottom_left_panel",
        "top_right_panel",
    }

    sys.modules.pop("tcgui", None)
    loaded = UiScriptLoader().load(script)
    assert loaded.document.live_widget_count == 8
    assert loaded.named("colliders_btn").widget.stable_id == "colliders_btn"
    assert loaded.named("ortho_btn").widget.stable_id == "ortho_btn"
    assert "tcgui" not in sys.modules

    loaded.document.layout_roots(Rect(0.0, 0.0, 800.0, 600.0))
    colliders = loaded.named("colliders_btn").widget
    ortho = loaded.named("ortho_btn").widget
    assert (colliders.bounds.x, colliders.bounds.y) == pytest.approx((10.0, 564.0))
    assert (ortho.bounds.x, ortho.bounds.y) == pytest.approx((764.0, 10.0))
    assert loaded.document.hit_test(777.0, 23.0) == ortho.handle
    assert not loaded.document.hit_test(400.0, 300.0).valid

    loaded.document.layout_roots(Rect(5.0, 7.0, 1000.0, 700.0))
    assert (colliders.bounds.x, colliders.bounds.y) == pytest.approx((15.0, 671.0))
    assert (ortho.bounds.x, ortho.bounds.y) == pytest.approx((969.0, 17.0))
    loaded.close()
