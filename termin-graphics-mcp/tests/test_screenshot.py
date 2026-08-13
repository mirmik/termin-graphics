import base64

import numpy as np
import pytest

from termin.graphics.mcp import capture_texture_screenshot


class FakeDevice:
    def __init__(self, *, read_ok: bool = True) -> None:
        self.read_ok = read_ok
        self.texture = None

    def read_texture_rgba_float(self, texture, output) -> bool:
        self.texture = texture
        if not self.read_ok:
            return False
        pixels = np.asarray(output).reshape((-1, 4))
        pixels[:, :] = [0.25, 0.5, 1.0, 1.0]
        return True


def test_capture_texture_screenshot_writes_png_and_optional_image(tmp_path):
    device = FakeDevice()
    texture = object()
    result = capture_texture_screenshot(
        texture,
        device,
        width=3,
        height=2,
        output_path=str(tmp_path / "native-editor"),
        include_image=True,
        default_dir=tmp_path,
        default_prefix="unused",
        log_prefix="ScreenshotTest",
    )

    path = tmp_path / "native-editor.png"
    assert device.texture is texture
    assert result["path"] == str(path)
    assert result["width"] == 3
    assert result["height"] == 2
    assert result["mime_type"] == "image/png"
    assert base64.b64decode(result["base64"]) == path.read_bytes()
    assert path.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")


def test_capture_texture_screenshot_rejects_invalid_source(tmp_path):
    arguments = {
        "width": 1,
        "height": 1,
        "default_dir": tmp_path,
        "default_prefix": "shot",
        "log_prefix": "ScreenshotTest",
    }
    with pytest.raises(RuntimeError, match="texture is not available"):
        capture_texture_screenshot(None, FakeDevice(), **arguments)
    with pytest.raises(RuntimeError, match="invalid size"):
        capture_texture_screenshot(
            object(),
            FakeDevice(),
            width=0,
            height=1,
            default_dir=tmp_path,
            default_prefix="shot",
            log_prefix="ScreenshotTest",
        )
    with pytest.raises(RuntimeError, match="Failed to read"):
        capture_texture_screenshot(object(), FakeDevice(read_ok=False), **arguments)
