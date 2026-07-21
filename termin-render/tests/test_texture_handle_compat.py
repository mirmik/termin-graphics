from termin.render.texture_handle import get_normal_texture_handle, get_white_texture_handle
from termin.render.texture import get_normal_texture, get_white_texture
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


def test_default_texture_helpers_keep_no_python_singletons() -> None:
    first_white_handle = get_white_texture_handle()
    second_white_handle = get_white_texture_handle()
    first_white = get_white_texture()
    second_white = get_white_texture()

    assert first_white_handle is not second_white_handle
    assert first_white is not second_white
    assert first_white_handle.uuid == second_white_handle.uuid == "__white_1x1__"
    assert first_white.texture_data is not second_white.texture_data
    assert first_white.texture_data.uuid == second_white.texture_data.uuid

    first_normal_handle = get_normal_texture_handle()
    second_normal_handle = get_normal_texture_handle()
    first_normal = get_normal_texture()
    second_normal = get_normal_texture()

    assert first_normal_handle is not second_normal_handle
    assert first_normal is not second_normal
    assert first_normal_handle.uuid == second_normal_handle.uuid == "__normal_1x1__"
    assert first_normal.texture_data is not second_normal.texture_data
    assert first_normal.texture_data.uuid == second_normal.texture_data.uuid
