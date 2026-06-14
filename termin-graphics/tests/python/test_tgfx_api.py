import tgfx
from tcbase._geom_native import Vec3


def test_basic_types_and_render_state():
    c = tgfx.Color4.red()
    assert c.r == 1.0
    assert c.g == 0.0
    assert c.b == 0.0
    assert c.a == 1.0

    rs = tgfx.RenderState.opaque()
    assert rs.depth_test is True
    assert rs.depth_write is True
    assert tgfx.ShaderVariantOp.LINE_MATERIAL_FRAGMENT is not tgfx.ShaderVariantOp.NONE
    assert tgfx.ShaderLanguage.GLSL is not tgfx.ShaderLanguage.SLANG
    assert tgfx.ShaderArtifactPolicy.REQUIRED is not tgfx.ShaderArtifactPolicy.OPTIONAL


def test_render_state_transparent():
    rs = tgfx.RenderState.transparent()
    assert rs.depth_test is True
    assert rs.depth_write is False
    assert rs.blend is True


def test_shader_metadata_binding_smoke():
    shader = tgfx.TcShader.from_sources(
        "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}",
        "#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}",
        "",
        "python_shader_metadata_smoke",
        "",
        tgfx.ShaderLanguage.SLANG,
        tgfx.ShaderArtifactPolicy.REQUIRED,
    )

    assert shader.is_valid
    assert shader.language == tgfx.ShaderLanguage.SLANG
    assert shader.artifact_policy == tgfx.ShaderArtifactPolicy.REQUIRED
    assert shader.requires_artifacts is True


def test_shader_from_sources_accepts_explicit_entries():
    shader = tgfx.TcShader.from_sources(
        "import termin_prelude;\n"
        "struct VertexOutput { float4 position : SV_Position; };\n"
        "[shader(\"vertex\")] VertexOutput vs_main() { "
        "VertexOutput o; o.position = float4(0, 0, 0, 1); return o; }",
        "struct FragmentOutput { float4 color : SV_Target0; };\n"
        "[shader(\"fragment\")] FragmentOutput fs_main() { "
        "FragmentOutput o; o.color = float4(1); return o; }",
        "",
        "python_shader_entry_smoke",
        "",
        tgfx.ShaderLanguage.SLANG,
        tgfx.ShaderArtifactPolicy.REQUIRED,
        "vs_main",
        "fs_main",
    )

    assert shader.is_valid
    assert shader.vertex_entry == "vs_main"
    assert shader.fragment_entry == "fs_main"
    assert shader.geometry_entry == "main"


def test_builtin_catalog_shader_binding_smoke():
    shader = tgfx.TcShader.from_builtin_catalog("termin-engine-present-blit")

    assert shader.is_valid
    assert shader.uuid == "termin-engine-present-blit"
    assert shader.name == "PresentBlitVSFS"
    assert shader.vertex_entry == "vs_main"
    assert shader.fragment_entry == "fs_main"
    assert "POSITION" in shader.vertex_source
    assert "vs_main" in shader.vertex_source
    assert "u_tex" in shader.fragment_source


def test_canvas2d_binding_smoke():
    color = tgfx.CanvasColor(1.0, 0.5, 0.25, 1.0)
    assert tuple(color) == (1.0, 0.5, 0.25, 1.0)

    point = tgfx.CanvasVec2(3.0, 4.0)
    assert point.x == 3.0
    assert point.y == 4.0

    renderer = tgfx.Canvas2DRenderer()
    assert renderer.default_font is None
    assert renderer.measure_text("no font", 14.0) == (0.0, 0.0)


def test_immediate_renderer_binding_smoke():
    renderer = tgfx.ImmediateRenderer()
    renderer.begin()
    renderer.line(Vec3(0.0, 0.0, 0.0), Vec3(1.0, 0.0, 0.0), tgfx.Color4.red())

    assert renderer.line_count == 1
    assert renderer.triangle_count == 0


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


def test_world_space_line_binding_smoke():
    style = tgfx.WorldSpaceLineStyle()
    style.width = 0.125
    style.color = (0.25, 0.75, 1.0, 1.0)
    style.cap = tgfx.LineCapStyle.Round
    style.join = tgfx.LineJoinStyle.Round
    style.round_segments = 12

    assert style.width == 0.125
    assert style.color == [0.25, 0.75, 1.0, 1.0]
    assert style.cap == tgfx.LineCapStyle.Round
    assert style.join == tgfx.LineJoinStyle.Round
    assert style.round_segments == 12

    params = tgfx.WorldSpaceLineParams()
    params.view_projection = tuple(
        1.0 if i in (0, 5, 10, 15) else 0.0
        for i in range(16)
    )
    params.camera_position = (1.0, 2.0, 3.0)
    params.lighting_enabled = True

    assert len(params.view_projection) == 16
    assert params.camera_position == [1.0, 2.0, 3.0]
    assert params.lighting_enabled is True
    assert tgfx.WorldSpaceLineRenderer() is not None
