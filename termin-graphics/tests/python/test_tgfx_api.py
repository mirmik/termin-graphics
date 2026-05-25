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


def test_canvas2d_binding_smoke():
    color = tgfx.CanvasColor(1.0, 0.5, 0.25, 1.0)
    assert tuple(color) == (1.0, 0.5, 0.25, 1.0)

    point = tgfx.CanvasVec2(3.0, 4.0)
    assert point.x == 3.0
    assert point.y == 4.0

    renderer = tgfx.Canvas2DRenderer()
    assert renderer.default_font is None
    assert renderer.measure_text("no font", 14.0) == (0.0, 0.0)


def test_screen_space_line_binding_smoke():
    style = tgfx.ScreenSpaceLineStyle()
    style.width_px = 5.0
    style.color = (1.0, 0.25, 0.5, 1.0)
    style.cap = tgfx.LineCapStyle.Round
    style.join = tgfx.LineJoinStyle.Bevel
    style.round_segments = 10

    assert style.width_px == 5.0
    assert style.color == [1.0, 0.25, 0.5, 1.0]
    assert style.cap == tgfx.LineCapStyle.Round
    assert style.join == tgfx.LineJoinStyle.Bevel
    assert style.round_segments == 10

    params = tgfx.ScreenSpaceLineParams()
    params.view_projection = tuple(
        1.0 if i in (0, 5, 10, 15) else 0.0
        for i in range(16)
    )
    params.viewport_width = 640.0
    params.viewport_height = 480.0

    assert len(params.view_projection) == 16
    assert params.viewport_width == 640.0
    assert params.viewport_height == 480.0
    assert tgfx.ScreenSpaceLineRenderer() is not None
