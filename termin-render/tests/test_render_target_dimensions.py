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
