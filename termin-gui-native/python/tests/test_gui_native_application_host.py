import os
import subprocess
import threading
from pathlib import Path

import pytest
import numpy as np

from termin.display import WindowedGraphicsSession
from termin.gui_native import _gui_native
from termin.gui_native import (
    CanvasTextureLayer,
    Document,
    DynamicTextureLease,
    DynamicTextureOwnership,
    GuiWindowHost,
    GuiWindowAdapter,
    OffscreenGuiComposition,
    OffscreenGuiApplication,
    StandaloneGuiApplication,
    WindowKey,
)


def test_application_host_types_are_public_and_document_close_is_idempotent():
    document = Document()
    assert not document.closed
    document.close()
    document.close()
    assert document.closed
    assert GuiWindowHost.__module__ == "termin.gui_native._gui_native"
    assert GuiWindowAdapter.__module__ == "termin.gui_native._gui_native"
    assert not hasattr(_gui_native, "PythonGuiWindowAdapter")
    assert not hasattr(GuiWindowHost, "defer")
    assert not hasattr(GuiWindowHost, "run_deferred")
    assert not hasattr(GuiWindowAdapter, "defer")
    assert not hasattr(GuiWindowAdapter, "run_deferred")
    assert OffscreenGuiComposition.__module__ == "termin.gui_native._gui_native"
    assert OffscreenGuiApplication is OffscreenGuiComposition
    assert OffscreenGuiApplication.__module__ == "termin.gui_native._gui_native"
    assert StandaloneGuiApplication.__module__ == "termin.gui_native._gui_native"
    assert DynamicTextureLease.__module__ == "termin.gui_native._gui_native"


def test_offscreen_application_renders_reads_pixels_and_accepts_synthetic_input():
    application = OffscreenGuiComposition(
        width=64,
        height=48,
        continuous_rendering=False,
    )
    assert application.frame_generation == 0
    assert application.render_frame()
    assert application.frame_generation == 1
    pixels = application.read_frame_rgba_float()
    assert pixels.shape == (48, 64, 4)
    assert pixels.dtype == np.float32
    assert pixels[0, 0, 3] > 0.9

    text_input = application.document.create_text_input()
    assert application.document.add_root(text_input.handle)
    assert application.document.set_focus(text_input.handle)
    assert application.render_frame()
    application.push_key(WindowKey.A)
    application.push_text("headless")
    assert application.pump_events() == 2
    assert text_input.text == "headless"

    lease = DynamicTextureLease(application)
    lease.set_rgba8(np.full((2, 3, 4), 127, dtype=np.uint8))
    assert not lease.empty

    application.resize(32, 24)
    assert application.latest_frame_size == [64, 48]
    assert application.read_frame_rgba_float().shape == (48, 64, 4)
    assert application.render_frame()
    assert application.framebuffer_size == [32, 24]
    assert application.read_frame_rgba_float().shape == (24, 32, 4)
    assert application.document.clear_focus(text_input.handle)
    application.request_close()
    assert application.should_close
    application.close()
    assert application.closed
    assert lease.closed
    assert application.document.closed


def test_installed_sdk_offscreen_python_consumer_without_display():
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
from termin.gui_native import OffscreenGuiComposition

application = OffscreenGuiComposition(
    width=32,
    height=24,
    continuous_rendering=False,
)
assert application.render_frame()
pixels = application.read_frame_rgba_float()
assert pixels.shape == (24, 32, 4)
assert application.frame_generation == 1
application.close()
"""
    result = subprocess.run(
        [str(sdk_root / "bin" / "termin_python"), "-I", "-c", script],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    assert result.returncode == 0, result.stderr


def test_installed_sdk_offscreen_cpp_consumer_without_display(tmp_path):
    sdk_root = Path(os.environ["TERMIN_SDK"]).resolve()
    source_root = (
        Path(__file__).resolve().parents[2]
        / "tests"
        / "installed_offscreen_consumer"
    )
    build_root = tmp_path / "build"
    environment = os.environ.copy()
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)

    configure = subprocess.run(
        [
            "cmake",
            "-S",
            str(source_root),
            "-B",
            str(build_root),
            f"-DCMAKE_PREFIX_PATH={sdk_root}",
            "-DCMAKE_BUILD_TYPE=Release",
        ],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    assert configure.returncode == 0, configure.stderr
    build = subprocess.run(
        ["cmake", "--build", str(build_root), "--parallel", "2"],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    assert build.returncode == 0, build.stderr
    executable = build_root / "termin_gui_native_offscreen_consumer"
    if os.name == "nt":
        executable = build_root / "Release" / "termin_gui_native_offscreen_consumer.exe"
    run = subprocess.run(
        [str(executable)],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    assert run.returncode == 0, run.stderr


@pytest.mark.skipif(
    os.environ.get("TERMIN_GUI_NATIVE_LIVE_TEST") != "1",
    reason="requires an SDL presentation backend",
)
def test_gui_window_host_lifecycle_keepalive_and_cross_thread_access():
    session = WindowedGraphicsSession.create_native()
    document = Document()
    host = GuiWindowHost(
        session,
        document,
        title="Python GuiWindowHost test",
        width=96,
        height=64,
        continuous_rendering=False,
    )

    with pytest.raises(RuntimeError, match="GuiWindowHost"):
        document.close()
    with pytest.raises(RuntimeError, match="presentation windows"):
        session.close()

    submitter = threading.Thread(target=host.request_repaint)
    submitter.start()
    submitter.join()
    assert host.tick()
    assert host.rendered_frame_count == 1

    # The binding owns Python references to both borrowed C++ owners.
    del session
    del document
    assert host.tick()

    host.close()
    host.close()
    assert host.closed
    with pytest.raises(RuntimeError, match="closed"):
        host.tick()


@pytest.mark.skipif(
    os.environ.get("TERMIN_GUI_NATIVE_LIVE_TEST") != "1",
    reason="requires an SDL presentation backend",
)
def test_standalone_application_exposes_document_and_ordered_close():
    application = StandaloneGuiApplication(
        title="Python standalone host test",
        width=96,
        height=64,
        continuous_rendering=False,
    )
    document = application.document
    host = application.window_host

    assert not document.closed
    assert not host.closed
    host.request_repaint()
    assert application.tick()

    application.close()
    application.close()
    assert application.closed
    assert document.closed
    assert host.closed


@pytest.mark.skipif(
    os.environ.get("TERMIN_GUI_NATIVE_LIVE_TEST") != "1",
    reason="requires an SDL presentation backend",
)
def test_dynamic_texture_lease_updates_canvas_and_follows_host_lifetime():
    application = StandaloneGuiApplication(
        title="Python dynamic texture lease test",
        width=96,
        height=64,
        continuous_rendering=False,
    )
    canvas = application.document.create_canvas()
    assert application.document.add_root(canvas.handle)
    lease = DynamicTextureLease(application.window_host)
    lease.bind_canvas(canvas)

    pixels = np.zeros((3, 4, 4), dtype=np.uint8)
    pixels[:, :, 0] = 255
    lease.set_rgba8(pixels)
    first_id = lease.texture.id
    assert first_id
    assert lease.ownership == DynamicTextureOwnership.OWNED
    assert (lease.width, lease.height) == (4, 3)
    assert application.tick()

    lease.update_region_rgba8(
        1, 1, np.full((1, 2, 4), 127, dtype=np.uint8)
    )
    lease.set_rgba8(np.full((5, 2, 4), 64, dtype=np.uint8))
    assert lease.texture.id != first_id
    assert (lease.width, lease.height) == (2, 5)

    lease.bind_canvas(canvas, CanvasTextureLayer.OVERLAY)
    lease.unbind_canvas(canvas, CanvasTextureLayer.OVERLAY)
    lease.clear()
    assert lease.empty
    lease.set_rgba8(pixels)
    application.close()
    assert lease.closed
    with pytest.raises(RuntimeError, match="after release"):
        lease.set_rgba8(pixels)


@pytest.mark.skipif(
    os.environ.get("TERMIN_GUI_NATIVE_LIVE_TEST") != "1",
    reason="requires an SDL presentation backend",
)
def test_installed_sdk_standalone_consumer_in_hostile_environment():
    sdk_root = Path(os.environ["TERMIN_SDK"]).resolve()
    environment = os.environ.copy()
    environment.update(
        {
            "PYTHONHOME": str(sdk_root / "__invalid_python_home__"),
            "PYTHONPATH": str(sdk_root / "__invalid_python_path__"),
            "PYTHONUSERBASE": str(sdk_root / "__invalid_user_base__"),
            "TERMIN_BACKEND": "opengl",
            "SDL_VIDEODRIVER": "offscreen",
        }
    )
    script = """
from termin.gui_native import StandaloneGuiApplication

application = StandaloneGuiApplication(
    title="Installed Python consumer",
    width=64,
    height=64,
    continuous_rendering=False,
)
application.window_host.request_repaint()
assert application.tick()
application.close()
"""
    result = subprocess.run(
        [str(sdk_root / "bin" / "termin_python"), "-I", "-c", script],
        check=False,
        capture_output=True,
        text=True,
        env=environment,
    )
    assert result.returncode == 0, result.stderr
