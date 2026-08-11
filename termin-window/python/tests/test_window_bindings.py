from termin.window import (
    BackendWindow,
    WindowHandle,
    WindowManager,
    WindowedGraphicsSession,
)
from tgfx import GraphicsHost


def test_window_api_is_owned_by_termin_window():
    assert WindowHandle.__module__ == "termin.window._window_native"
    assert WindowManager.__module__ == "termin.window._window_native"
    assert WindowedGraphicsSession is not None
    assert BackendWindow is not None
    assert GraphicsHost is not None
    assert hasattr(WindowedGraphicsSession, "graphics")
    assert hasattr(WindowManager, "create_window")
    assert hasattr(WindowManager, "pump_events")
    assert hasattr(BackendWindow, "content_scale")
    assert hasattr(BackendWindow, "poll_events")
