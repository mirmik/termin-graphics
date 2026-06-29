import pytest
from pathlib import Path

from termin.materials import (
    ShasderStage,
    ShaderMultyPhaseProgramm,
    ShaderPhase,
    MaterialProperty,
    create_material_from_parsed,
    parse_shader_text,
    parse_property_directive,
)
from termin.stdlib import stdlib_root


def test_parse_render_state_directives():
    shader_text = "\n".join(
        [
            "@program demo",
            "@language slang",
            "@phase main",
            "@priority 3",
            "@glDepthMask false",
            "@glDepthTest true",
            "@glBlend on",
            "@glCull off",
            "@stage vertex",
            "void main() {}",
            "@endstage",
            "@endphase",
        ]
    )

    parsed = parse_shader_text(shader_text)
    assert parsed.program == "demo"
    assert len(parsed.phases) == 1

    phase = parsed.phases[0]
    assert phase.phase_mark == "main"
    assert phase.priority == 3
    assert phase.gl_depth_mask is False
    assert phase.gl_depth_test is True
    assert phase.gl_blend is True
    assert phase.gl_cull is False
    assert phase.stages["vertex"].source == "void main() {}\n"


def test_render_state_directives_require_phase():
    directives = ("@glDepthTest true", "@glBlend true", "@glCull true")
    for directive in directives:
        with pytest.raises(RuntimeError):
            parse_shader_text(f"{directive}\n")


def test_render_state_directives_require_value():
    directives = ("@glDepthTest", "@glBlend", "@glCull")
    shader_body = "\n".join(["@phase main", "{directive}", "@endphase"])
    for directive in directives:
        with pytest.raises(RuntimeError):
            parse_shader_text(shader_body.format(directive=directive))


def test_parse_multiple_phases_and_stages():
    shader_text = "\n".join(
        [
            "@program composite",
            "@language slang",
            "@phase geometry",
            "@priority 1",
            "@glDepthTest on",
            "@stage vertex",
            "// vertex stage",
            "void main() {}",
            "@endstage",
            "@stage fragment",
            "// fragment stage",
            "float4 main() : SV_Target0 {",
            "  return float4(1.0);",
            "}",
            "@endstage",
            "@endphase",
            "@phase overlay",
            "@glDepthMask off",
            "@glDepthTest off",
            "@glBlend true",
            "@stage vertex",
            "// overlay vertex",
            "@endstage",
            "@endphase",
        ]
    )

    parsed = parse_shader_text(shader_text)
    assert parsed.program == "composite"
    assert len(parsed.phases) == 2

    geometry = parsed.phases[0]
    assert geometry.phase_mark == "geometry"
    assert geometry.priority == 1
    assert geometry.gl_depth_test is True
    assert geometry.gl_depth_mask is None
    assert geometry.gl_blend is None
    assert geometry.gl_cull is None
    assert geometry.stages["vertex"].source == "// vertex stage\nvoid main() {}\n"
    assert geometry.stages["fragment"].source == "// fragment stage\nfloat4 main() : SV_Target0 {\n  return float4(1.0);\n}\n"

    overlay = parsed.phases[1]
    assert overlay.phase_mark == "overlay"
    assert overlay.priority == 0  # default value
    assert overlay.gl_depth_mask is False
    assert overlay.gl_depth_test is False
    assert overlay.gl_blend is True
    assert overlay.gl_cull is None
    assert overlay.stages["vertex"].source == "// overlay vertex\n"


def test_tree_builders_have_uniform_signature():
    shader_text = "\n".join(
        [
            "@program mesh",
            "@language slang",
            "@phase depth",
            "@glDepthTest true",
            "@stage vertex",
            "void main() {}",
            "@endstage",
            "@endphase",
        ]
    )
    tree = parse_shader_text(shader_text)
    program = ShaderMultyPhaseProgramm.from_tree(tree)

    assert program.program == "mesh"
    assert len(program.phases) == 1

    depth_phase = program.phases[0]
    assert isinstance(depth_phase, ShaderPhase)
    assert depth_phase.phase_mark == "depth"
    assert depth_phase.gl_depth_test is True
    assert depth_phase.gl_blend is None
    assert depth_phase.gl_depth_mask is None
    assert depth_phase.stages["vertex"].source == "void main() {}\n"
    assert isinstance(depth_phase.stages["vertex"], ShasderStage)


def test_parse_property_directive_float():
    """Тест парсинга @property директивы для Float."""
    prop = parse_property_directive("@property Float u_roughness = 0.5")
    assert prop.name == "u_roughness"
    assert prop.property_type == "Float"
    assert prop.default == 0.5
    assert prop.range_min is None
    assert prop.range_max is None


def test_parse_property_directive_float_with_range():
    """Тест парсинга @property директивы для Float с range."""
    prop = parse_property_directive("@property Float u_metallic = 0.0 range(0.0, 1.0)")
    assert prop.name == "u_metallic"
    assert prop.property_type == "Float"
    assert prop.default == 0.0
    assert prop.range_min == 0.0
    assert prop.range_max == 1.0


def test_parse_property_directive_color():
    """Тест парсинга @property директивы для Color."""
    prop = parse_property_directive("@property Color u_color = Color(1.0, 0.5, 0.0, 1.0)")
    assert prop.name == "u_color"
    assert prop.property_type == "Color"
    assert prop.default == (1.0, 0.5, 0.0, 1.0)


def test_parse_property_directive_vec3():
    """Тест парсинга @property директивы для Vec3."""
    prop = parse_property_directive("@property Vec3 u_lightDir = Vec3(0.0, 1.0, 0.0)")
    assert prop.name == "u_lightDir"
    assert prop.property_type == "Vec3"
    assert prop.default == (0.0, 1.0, 0.0)


def test_parse_property_directive_texture2d():
    """Тест парсинга @property директивы для Texture."""
    prop = parse_property_directive("@property Texture u_mainTex")
    assert prop.name == "u_mainTex"
    assert prop.property_type == "Texture"
    assert prop.default is None


def test_parse_shader_text_rejects_implicit_glsl_shader():
    shader_text = "\n".join([
        "@program test",
        "@phase main",
        "@property Float u_strength = 0.5",
        "@stage fragment",
        "#version 450 core",
        "uniform float u_strength;",
        "out vec4 FragColor;",
        "void main() { FragColor = vec4(u_strength); }",
        "@endstage",
        "@endphase",
    ])

    with pytest.raises(RuntimeError, match="implicit GLSL .shader programs are no longer supported"):
        parse_shader_text(shader_text)


def test_slang_material_texture_declarations_are_synthesized():
    shader_text = "\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Texture2D u_tex0 = \"white\"",
        "@property Texture2D u_tex1 = \"white\"",
        "@property Texture2D u_tex2 = \"white\"",
        "@property Texture2D u_tex3 = \"white\"",
        "@property Texture2D u_tex4 = \"white\"",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {",
        "    return u_tex0.Sample(uv) + u_tex1.Sample(uv) + u_tex2.Sample(uv) + u_tex3.Sample(uv) + u_tex4.Sample(uv);",
        "}",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    fragment = program.phases[0].stages["fragment"].source

    assert "Sampler2D u_tex0;" in fragment
    assert "Sampler2D u_tex1;" in fragment
    assert "Sampler2D u_tex2;" in fragment
    assert "Sampler2D u_tex3;" in fragment
    assert "Sampler2D u_tex4;" in fragment
    assert "register(" not in fragment
    assert "layout(" not in fragment


def test_shader_interface_compare_separates_source_from_inputs():
    from termin.default_assets.render.shader_interface import compare_shader_interface

    base = parse_shader_text("\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Texture2D u_input_tex = \"white\"",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return u_input_tex.Sample(uv); }",
        "@endstage",
        "@endphase",
    ]))
    source_only = parse_shader_text("\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Texture2D u_input_tex = \"white\"",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return u_input_tex.Sample(uv * 0.5); }",
        "@endstage",
        "@endphase",
    ]))
    texture_input_added = parse_shader_text("\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Texture2D u_input_tex = \"white\"",
        "@property Texture2D u_depth_texture = \"depth_default\"",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return u_input_tex.Sample(uv) + u_depth_texture.Sample(uv); }",
        "@endstage",
        "@endphase",
    ]))
    numeric_uniform_added = parse_shader_text("\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Texture2D u_input_tex = \"white\"",
        "@property Float u_factor = 1.0",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return u_input_tex.Sample(uv) * material.u_factor; }",
        "@endstage",
        "@endphase",
    ]))

    no_interface_change = compare_shader_interface(base, source_only)
    assert no_interface_change.material_changed is False
    assert no_interface_change.graph_inputs_changed is False

    graph_input_change = compare_shader_interface(base, texture_input_added)
    assert graph_input_change.material_changed is True
    assert graph_input_change.graph_inputs_changed is True

    material_only_change = compare_shader_interface(base, numeric_uniform_added)
    assert material_only_change.material_changed is True
    assert material_only_change.graph_inputs_changed is False


def test_parse_property_in_phase():
    """Тест парсинга @property внутри @phase."""
    shader_text = "\n".join([
        "@program test",
        "@language slang",
        "@phase main",
        "@property Float u_roughness = 0.5",
        "@property Color u_color = Color(1.0, 0.0, 0.0, 1.0)",
        "@property Float u_metallic = 0.0 range(0.0, 1.0)",
        "@stage vertex",
        "void main() {}",
        "@endstage",
        "@endphase",
    ])

    parsed = parse_shader_text(shader_text)
    phase = parsed.phases[0]

    assert len(parsed.material_properties) == 3
    assert len(phase.uniforms) == 0

    u_roughness = parsed.material_properties[0]
    assert isinstance(u_roughness, MaterialProperty)
    assert u_roughness.name == "u_roughness"
    assert u_roughness.default == 0.5

    u_color = parsed.material_properties[1]
    assert u_color.name == "u_color"
    assert u_color.property_type == "Color"
    assert u_color.default == (1.0, 0.0, 0.0, 1.0)

    u_metallic = parsed.material_properties[2]
    assert u_metallic.name == "u_metallic"
    assert u_metallic.range_min == 0.0
    assert u_metallic.range_max == 1.0


def test_shader_phase_from_tree_with_properties():
    """Тест создания ShaderPhase с properties через from_tree."""
    shader_text = "\n".join([
        "@language slang",
        "@phase opaque",
        "@property Float u_value = 0.7",
        "@stage vertex",
        "void main() {}",
        "@endstage",
        "@stage fragment",
        "void main() {}",
        "@endstage",
        "@endphase",
    ])

    parsed = parse_shader_text(shader_text)
    phase = ShaderPhase.from_tree(parsed.phases[0])

    assert len(parsed.material_properties) == 1
    assert parsed.material_properties[0].name == "u_value"
    assert parsed.material_properties[0].default == 0.7
    assert len(phase.uniforms) == 0


def test_property_outside_phase_accepted():
    """@property вне @phase принимается без ошибки (глобальное свойство)."""
    result = parse_shader_text("@language slang\n@property Float u_value = 0.5")
    assert len(result.phases) == 0
    assert len(result.material_properties) == 1
    assert result.material_properties[0].name == "u_value"


def test_parse_slang_shader_keeps_source_unrewritten():
    shader_text = "\n".join([
        "@program SlangSample",
        "@language slang",
        "@phase opaque",
        "@stage vertex",
        "struct VertexOutput { float4 position : SV_Position; };",
        "[shader(\"vertex\")] VertexOutput main() { VertexOutput o; o.position = float4(0, 0, 0, 1); return o; }",
        "@endstage",
        "@stage fragment",
        "struct FragmentOutput { float4 color : SV_Target0; };",
        "[shader(\"fragment\")] FragmentOutput main() { FragmentOutput o; o.color = float4(1, 0, 0, 1); return o; }",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    assert program.language == "slang"
    vertex = program.phases[0].stages["vertex"].source
    assert "[shader(\"vertex\")]" in vertex
    assert "#version" not in vertex
    assert "uniform PerFrame" not in vertex


def test_slang_shader_synthesizes_material_params_for_scalar_properties():
    shader_text = "\n".join([
        "@program SlangWithProps",
        "@language slang",
        "@property Color u_color = Color(1, 1, 1, 1)",
        "@phase opaque",
        "@stage vertex",
        "struct VertexOutput { float4 position : SV_Position; };",
        "[shader(\"vertex\")] VertexOutput main() {",
        "    VertexOutput output;",
        "    output.position = float4(0, 0, 0, 1);",
        "    return output;",
        "}",
        "@endstage",
        "@stage fragment",
        "struct FragmentOutput { float4 color : SV_Target0; };",
        "[shader(\"fragment\")] FragmentOutput main() {",
        "    FragmentOutput output;",
        "    output.color = material.u_color;",
        "    return output;",
        "}",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    phase = next(phase for phase in program.phases if phase.phase_mark == "opaque")
    assert [entry.name for entry in phase.material_ubo_layout.entries] == ["u_color"]

    fragment = phase.stages["fragment"].source
    assert "import termin_prelude;" in fragment
    assert "struct MaterialParams" in fragment
    assert "float4 u_color;" in fragment
    assert "[[TerminScope(\"material\")]]" in fragment
    assert "ConstantBuffer<MaterialParams> material;" in fragment
    assert "register(" not in fragment
    assert "output.color = material.u_color;" in fragment
    assert "struct MaterialParams" not in phase.stages["vertex"].source


def test_slang_material_layout_sets_shader_contract_before_sidecar_reflection():
    import tgfx  # noqa: F401  # Registers TcShader before TcMaterialPhase.shader casts it.

    shader_text = "\n".join([
        "@program SlangWithRuntimeLayout",
        "@language slang",
        "@property Color tint = Color(1, 1, 1, 1)",
        "@phase opaque",
        "@stage vertex",
        "struct VertexInput {",
        "    float3 position : POSITION;",
        "    float3 normal : NORMAL;",
        "};",
        "struct VertexOutput { float4 position : SV_Position; };",
        "[shader(\"vertex\")] VertexOutput main(VertexInput input) {",
        "    VertexOutput output;",
        "    output.position = float4(input.position, 1);",
        "    return output;",
        "}",
        "@endstage",
        "@stage fragment",
        "struct FragmentOutput { float4 color : SV_Target0; };",
        "[shader(\"fragment\")] FragmentOutput main() {",
        "    FragmentOutput output;",
        "    output.color = material.tint;",
        "    return output;",
        "}",
        "@endstage",
        "@endphase",
    ])

    material = create_material_from_parsed(parse_shader_text(shader_text))
    shader = material.get_phase(0).shader

    assert shader.material_ubo_entry_count == 1
    assert shader.material_ubo_block_size == 16
    assert shader.find_resource_binding("material") is None
    assert shader.has_contract

    contract = shader.contract
    assert contract["source_kind"] == 4  # TC_SHADER_CONTRACT_SOURCE_DECLARED
    assert "draw_kind" not in contract
    assert {
        input_desc["semantic"]
        for input_desc in contract["vertex_inputs"]
    } == {"position", "normal"}
    assert "material" in {
        resource["name"]
        for resource in contract["resources"]
    }
    material_requirement = next(
        resource
        for resource in contract["resources"]
        if resource["name"] == "material"
    )
    assert material_requirement["kind_name"] == "constant_buffer"
    assert material_requirement["scope_name"] == "material"
    assert material_requirement["size"] == 16
    assert material_requirement["fields"] == [
        {"name": "tint", "type": "Color", "offset": 0, "size": 16}
    ]

    shader.set_resource_layout([])
    assert shader.find_resource_binding("material") is None
    contract_after_layout_clear = shader.contract
    assert {
        resource["name"]
        for resource in contract_after_layout_clear["resources"]
    } == {
        resource["name"]
        for resource in contract["resources"]
    }
    material_after_layout_clear = next(
        resource
        for resource in contract_after_layout_clear["resources"]
        if resource["name"] == "material"
    )
    assert material_after_layout_clear["fields"] == material_requirement["fields"]


def test_parse_shader_text_rejects_explicit_glsl_shader():
    shader_text = "\n".join([
        "@program GlslWithRuntimeLayout",
        "@language glsl",
        "@property Color tint = Color(1, 1, 1, 1)",
        "@phase opaque",
        "@stage vertex",
        "#version 450",
        "layout(location = 0) in vec3 a_position;",
        "void main() { gl_Position = vec4(a_position, 1.0); }",
        "@endstage",
        "@stage fragment",
        "#version 450",
        "layout(location = 0) out vec4 out_color;",
        "void main() { out_color = tint; }",
        "@endstage",
        "@endphase",
    ])

    with pytest.raises(RuntimeError, match="GLSL .shader programs are no longer supported"):
        parse_shader_text(shader_text)


def test_slang_shader_synthesizes_engine_scope_blocks_for_compact_names():
    shader_text = "\n".join([
        "@program SlangEngineScopes",
        "@language slang",
        "@phase opaque",
        "@stage vertex",
        "struct VertexInput { float3 position : POSITION; };",
        "struct VertexOutput { float4 position : SV_Position; };",
        "[shader(\"vertex\")] VertexOutput main(VertexInput input) {",
        "    VertexOutput output;",
        "    float4 world = mul(u_model, float4(input.position, 1.0));",
        "    output.position = mul(u_projection, mul(u_view, world));",
        "    return output;",
        "}",
        "@endstage",
        "@stage fragment",
        "struct FragmentOutput { float4 color : SV_Target0; };",
        "[shader(\"fragment\")] FragmentOutput main() {",
        "    FragmentOutput output;",
        "    output.color = float4(1, 1, 1, 1);",
        "    return output;",
        "}",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    vertex = program.phases[0].stages["vertex"].source

    assert "import termin_prelude;" in vertex
    assert "[[TerminScope(\"frame\")]]" in vertex
    assert "ConstantBuffer<PerFrame> per_frame;" in vertex
    assert "#define u_view per_frame.u_view" in vertex
    assert "#define u_projection per_frame.u_projection" in vertex
    assert "[[TerminScope(\"draw\")]]" in vertex
    assert "ConstantBuffer<DrawData> draw_data;" in vertex
    assert "#define u_model draw_data._u_model" in vertex
    assert vertex.count("ConstantBuffer<PerFrame> per_frame;") == 1
    assert vertex.count("ConstantBuffer<DrawData> draw_data;") == 1


def test_slang_shader_synthesizes_sampler2d_for_texture_properties():
    shader_text = "\n".join([
        "@program SlangWithTexture",
        "@language slang",
        "@property Texture2D albedo = \"white\"",
        "@phase opaque",
        "@stage fragment",
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return albedo.Sample(float2(0)); }",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    phase = program.phases[0]
    fragment = phase.stages["fragment"].source

    assert phase.uniforms[0].name == "albedo"
    assert phase.uniforms[0].property_type == "Texture"
    assert phase.material_ubo_layout.entries == []
    assert "import termin_prelude;" in fragment
    assert "[[TerminScope(\"material\")]]" in fragment
    assert "Sampler2D albedo;" in fragment
    assert "register(" not in fragment
    assert fragment.index("Sampler2D albedo;") < fragment.index("[shader(\"fragment\")]")


def test_slang_texture_property_does_not_duplicate_existing_sampler2d_declaration():
    shader_text = "\n".join([
        "@program SlangWithTexture",
        "@language slang",
        "@property Texture2D albedo = \"white\"",
        "@phase opaque",
        "@stage fragment",
        "Sampler2D albedo;",
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 { return albedo.Sample(uv); }",
        "@endstage",
        "@endphase",
    ])

    program = parse_shader_text(shader_text)
    fragment = program.phases[0].stages["fragment"].source

    assert fragment.count("Sampler2D albedo;") == 1
    assert fragment.index("Sampler2D albedo;") < fragment.index("[shader(\"fragment\")]")


def test_stdlib_blinn_phong_uses_slang_scope_model():
    from termin.default_assets.render.shader_asset import ShaderAsset

    shader_path = stdlib_root() / "shaders" / "BlinnPhong.shader"
    shader_asset = ShaderAsset.from_file(shader_path, name="BlinnPhong")
    program = shader_asset.program
    assert program is not None

    phase = program.phases[0]
    vertex = phase.stages["vertex"].source
    assert "[[TerminScope(\"frame\")]]" in vertex
    assert "ConstantBuffer<PerFrame> per_frame;" in vertex
    assert "[[TerminScope(\"draw\")]]" in vertex
    assert "ConstantBuffer<DrawData> draw_data;" in vertex
    assert "#version" not in vertex
    assert "layout(" not in vertex

    fragment = phase.stages["fragment"].source
    assert "import termin_lighting;" in fragment
    assert "import termin_shadows;" in fragment
    assert "[[TerminScope(\"material\")]]" in fragment
    assert "ConstantBuffer<MaterialParams> material;" in fragment
    assert "Sampler2D u_diffuse_texture;" in fragment
    assert "#version" not in fragment
    assert "layout(" not in fragment


def test_string_shader_uuid_produces_readable_distinct_phase_ids():
    from termin.default_assets.render.shader_asset import make_phase_uuid

    assert (
        make_phase_uuid("termin-stdlib-shader-blinn-phong", "opaque")
        == "stdlib-blinn-phong-opaque"
    )
    assert (
        make_phase_uuid("termin-stdlib-shader-blinn-phong", "shadow")
        == "stdlib-blinn-phong-shadow"
    )


def test_stdlib_slang_material_creates_slang_tc_shader():
    from tgfx import ShaderLanguage
    from termin.default_assets.render.material_asset import MaterialAsset
    from termin.default_assets.render.shader_asset import ShaderAsset
    from termin.default_assets.resource_manager import DefaultResourceManager

    DefaultResourceManager._reset_for_testing()
    rm = DefaultResourceManager.instance()
    stdlib = stdlib_root()

    shader_asset = ShaderAsset.from_file(
        stdlib / "shaders" / "SlangNormalColor.shader",
        name="SlangNormalColor",
    )
    rm.register_shader(
        "SlangNormalColor",
        shader_asset.program,
        source_path=str(shader_asset.source_path),
        uuid="00000000-0000-0000-0001-000000000007",
    )

    material_asset = MaterialAsset.from_file(
        stdlib / "materials" / "SlangNormalColor.material",
        name="SlangNormalColor",
    )
    material = material_asset.material

    assert material is not None
    assert material.phase_count == 1
    phase = material.get_phase(0)
    assert phase is not None
    assert phase.shader.language == ShaderLanguage.SLANG
    assert "import termin_prelude;" in phase.shader.vertex_source
    assert "[[TerminScope(\"frame\")]]" in phase.shader.vertex_source
    assert "[[TerminScope(\"draw\")]]" in phase.shader.vertex_source
    assert "[shader(\"vertex\")]" in phase.shader.vertex_source
    assert "#version" not in phase.shader.vertex_source
    assert "[[vk::" not in phase.shader.vertex_source
    assert "register(" not in phase.shader.vertex_source
    assert "draw_data.u_model" in phase.shader.vertex_source


def test_stdlib_slang_textured_normal_material_uses_texture_property():
    from tgfx import ShaderLanguage
    from termin.default_assets.render.material_asset import MaterialAsset
    from termin.default_assets.render.shader_asset import ShaderAsset
    from termin.default_assets.resource_manager import DefaultResourceManager

    DefaultResourceManager._reset_for_testing()
    rm = DefaultResourceManager.instance()
    stdlib = stdlib_root()

    shader_asset = ShaderAsset.from_file(
        stdlib / "shaders" / "SlangTexturedNormal.shader",
        name="SlangTexturedNormal",
    )
    rm.register_shader(
        "SlangTexturedNormal",
        shader_asset.program,
        source_path=str(shader_asset.source_path),
        uuid="00000000-0000-0000-0001-000000000008",
    )

    material_asset = MaterialAsset.from_file(
        stdlib / "materials" / "SlangTexturedNormal.material",
        name="SlangTexturedNormal",
    )
    material = material_asset.material

    assert material is not None
    assert material.phase_count == 1
    phase = material.get_phase(0)
    assert phase is not None
    assert phase.shader.language == ShaderLanguage.SLANG
    assert "register(" not in phase.shader.vertex_source
    assert "register(" not in phase.shader.fragment_source
    assert "import termin_prelude;" in phase.shader.vertex_source
    assert "[[TerminScope(\"frame\")]]" in phase.shader.vertex_source
    assert "[[TerminScope(\"draw\")]]" in phase.shader.vertex_source
    assert "import termin_prelude;" in phase.shader.fragment_source
    assert "ConstantBuffer<MaterialParams> material;" in phase.shader.fragment_source
    assert "[[TerminScope(\"material\")]]" in phase.shader.fragment_source
    assert "material.u_tint_color" in phase.shader.fragment_source
    assert "Sampler2D u_tint_texture;" in phase.shader.fragment_source
    assert "u_tint_texture.Sample(input.uv)" in phase.shader.fragment_source
    assert phase.uniform_count == 1
    assert phase.texture_count == 1


def test_builtin_pbr_shader_uses_slang_scope_model():
    from termin.default_assets.render.shader_asset import ShaderAsset

    stdlib = stdlib_root()
    shader_asset = ShaderAsset.from_file(
        stdlib / "shaders" / "CookTorrancePBR.shader",
        name="CookTorrancePBR",
    )
    program = shader_asset.program
    assert program is not None
    assert program.program == "CookTorrancePBR"
    assert program.language == "slang"
    assert "lighting_ubo" in program.features
    assert len(program.phases) == 2

    phase = next(phase for phase in program.phases if phase.phase_mark == "opaque")
    assert phase.phase_mark == "opaque"
    assert phase.available_marks == ["opaque", "transparent"]
    assert not phase.material_ubo_layout.empty()

    vertex = phase.stages["vertex"].source
    assert "import termin_prelude;" in vertex
    assert "[[TerminScope(\"frame\")]]" in vertex
    assert "ConstantBuffer<PerFrame> per_frame;" in vertex
    assert "[[TerminScope(\"draw\")]]" in vertex
    assert "ConstantBuffer<DrawData> draw_data;" in vertex
    assert "#version" not in vertex
    assert "layout(" not in vertex

    fragment = phase.stages["fragment"].source
    assert "import termin_prelude;" in fragment
    assert "[[TerminScope(\"material\")]]" in fragment
    assert "ConstantBuffer<MaterialParams> material;" in fragment
    assert "Sampler2D u_albedo_texture;" in fragment
    assert "Sampler2D u_normal_texture;" in fragment
    assert "Sampler2D u_metallic_roughness_texture;" in fragment
    assert "import termin_lighting;" in fragment
    assert "import termin_shadows;" in fragment
    assert "struct LightingBlock" not in fragment
    assert "struct ShadowBlock" not in fragment
    assert "get_camera_position() - input.world_pos" in fragment
    assert "material.u_metallic" in fragment
    assert "#version" not in fragment
    assert "layout(" not in fragment

    shadow_phase = next(phase for phase in program.phases if phase.phase_mark == "shadow")
    assert shadow_phase.phase_mark == "shadow"
    assert "[[TerminScope(\"frame\")]]" in shadow_phase.stages["vertex"].source
    assert "[[TerminScope(\"draw\")]]" in shadow_phase.stages["vertex"].source
