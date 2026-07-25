import os
from pathlib import Path
import subprocess

import numpy as np

from termin.gui_native import (
    CommandData,
    CommandModel,
    DynamicTextureLease,
    MenuBarEntry,
    ModifierFlag,
    OffscreenGuiComposition,
    tc_ui_document_create,
    tc_ui_document_destroy,
)


def test_public_surface_contains_compositions_but_no_ownership_hosts():
    import termin.gui_native as gui_native

    document = tc_ui_document_create()
    assert document.valid
    tc_ui_document_destroy(document)
    assert not document.valid
    assert OffscreenGuiComposition.__module__ == "termin.gui_native._gui_native"
    assert DynamicTextureLease.__module__ == "termin.gui_native._gui_native"
    assert "termin.display" not in gui_native.__dict__
    assert "GuiWindowHost" not in gui_native.__dict__
    assert "StandaloneGuiApplication" not in gui_native.__dict__
    assert "OffscreenGuiApplication" not in gui_native.__dict__


def test_offscreen_composition_renders_and_accepts_synthetic_input():
    application = OffscreenGuiComposition(
        width=64,
        height=48,
        continuous_rendering=False,
    )
    document = application.document
    assert application.render_frame()
    pixels = application.read_frame_rgba_float()
    assert pixels.shape == (48, 64, 4)
    assert pixels.dtype == np.float32

    text_input = document.create_text_input()
    assert document.add_root(text_input.handle)
    assert document.set_focus(text_input.handle)
    application.push_text("headless")
    assert application.pump_events() == 1
    assert text_input.text == "headless"

    commands = CommandModel()
    commands.append(CommandData("redo", "Redo", shortcut="Ctrl+Y"))
    menu_bar = document.create_menu_bar()
    menu_bar.entries = [MenuBarEntry("edit", "Edit", commands)]
    assert document.add_root(menu_bar.handle)
    activations = []
    menu_bar.connect_activated(
        lambda _menu, _command, command: activations.append(command.stable_id)
    )
    application.set_unhandled_key_handler(menu_bar.dispatch_shortcut)
    application.push_key(ord("Y"), modifiers=ModifierFlag.Ctrl)
    assert application.pump_events() == 1
    assert activations == ["redo"]

    lease = DynamicTextureLease(application)
    lease.set_rgba8(np.full((2, 3, 4), 127, dtype=np.uint8))
    assert not lease.empty
    application.request_close()
    application.close()
    assert lease.closed
    assert not document.valid


def test_installed_sdk_offscreen_consumer_has_no_window_dependency():
    sdk_root = Path(os.environ["TERMIN_SDK"]).resolve()
    environment = os.environ.copy()
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)
    environment.update(
        {
            "PYTHONHOME": str(sdk_root / "__invalid_python_home__"),
            "PYTHONPATH": str(sdk_root / "__invalid_python_path__"),
            "PYTHONUSERBASE": str(sdk_root / "__invalid_user_base__"),
        }
    )
    script = """
import sys
from termin.gui_native import OffscreenGuiComposition
assert "termin.display" not in sys.modules
assert "termin.gui_native._gui_native_window" not in sys.modules
application = OffscreenGuiComposition(width=32, height=24, continuous_rendering=False)
assert application.render_frame()
assert application.read_frame_rgba_float().shape == (24, 32, 4)
application.close()
"""
    result = subprocess.run(
        [str(sdk_root / "bin" / "termin_python"), "-I", "-c", script],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
        cwd=sdk_root,
    )
    assert result.returncode == 0, result.stderr
