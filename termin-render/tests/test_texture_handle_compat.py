from termin.render.texture_handle import get_normal_texture_handle, get_white_texture_handle
from tgfx import TcTexture


def test_default_texture_helpers_return_tc_texture() -> None:
    white = get_white_texture_handle()
    normal = get_normal_texture_handle()

    assert isinstance(white, TcTexture)
    assert isinstance(normal, TcTexture)
    assert white.is_valid
    assert normal.is_valid
    assert white.uuid == "__white_1x1__"
    assert normal.uuid == "__normal_1x1__"
