from termin.render_framework import render_target_new


def test_python_render_target_dimensions_preserve_the_last_valid_size():
    target = render_target_new("python-dimension-validation")
    try:
        target.width = 64
        target.height = 48
        target.ensure_textures()
        color = target.color_texture
        depth = target.depth_texture
        assert color.width == 64
        assert color.height == 48
        assert depth.width == 64
        assert depth.height == 48

        target.width = -1
        target.height = 0
        target.width = 2**31 - 1
        assert (target.width, target.height) == (64, 48)

        target.width = 128
        target.height = 96
        target.ensure_textures()
        assert (color.width, color.height) == (128, 96)
        assert (depth.width, depth.height) == (128, 96)
    finally:
        target.free()


def test_python_render_target_color_encoding_is_explicit_and_validated():
    target = render_target_new("python-encoding-contract")
    try:
        assert target.color_encoding == "linear"
        target.color_encoding = "srgb"
        assert target.color_encoding == "linear"

        target.color_format = "rgba8"
        target.color_encoding = "srgb"
        target.ensure_textures()
        assert target.color_encoding == "srgb"
        assert str(target.color_texture.encoding).lower().endswith("srgb")

        target.color_format = "rgba16f"
        assert target.color_format == "rgba8"
    finally:
        target.free()
