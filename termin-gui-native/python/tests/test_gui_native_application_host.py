import os
import subprocess
import threading
from pathlib import Path

import pytest

from termin.display import WindowedGraphicsSession
from termin.gui_native import (
    Document,
    GuiWindowHost,
    StandaloneGuiApplication,
)


def test_application_host_types_are_public_and_document_close_is_idempotent():
    document = Document()
    assert not document.closed
    document.close()
    document.close()
    assert document.closed
    assert GuiWindowHost.__module__ == "termin.gui_native._gui_native"
    assert StandaloneGuiApplication.__module__ == "termin.gui_native._gui_native"


@pytest.mark.skipif(
    os.environ.get("TERMIN_GUI_NATIVE_LIVE_TEST") != "1",
    reason="requires an SDL presentation backend",
)
def test_gui_window_host_lifecycle_keepalive_and_deferred_callback():
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

    callbacks = []
    submitter = threading.Thread(
        target=lambda: host.defer(lambda: callbacks.append("owner-thread"))
    )
    submitter.start()
    submitter.join()
    assert host.tick()
    assert callbacks == ["owner-thread"]
    assert host.rendered_frame_count == 1

    def fail_callback():
        raise ValueError("deferred callback failure")

    host.defer(fail_callback)
    with pytest.raises(ValueError, match="deferred callback failure"):
        host.tick()

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
called = []
application.window_host.defer(lambda: called.append(True))
assert application.tick()
assert called == [True]
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
