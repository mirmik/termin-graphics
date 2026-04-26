import tgfx


def test_basic_types_and_render_state():
    c = tgfx.Color4.red()
    assert c.r == 1.0
    assert c.g == 0.0
    assert c.b == 0.0
    assert c.a == 1.0

    rs = tgfx.RenderState.opaque()
    assert rs.depth_test is True
    assert rs.depth_write is True


def test_render_state_transparent():
    rs = tgfx.RenderState.transparent()
    assert rs.depth_test is True
    assert rs.depth_write is False
    assert rs.blend is True
