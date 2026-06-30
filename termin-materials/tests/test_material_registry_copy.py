from termin.materials import TcMaterial


VERTEX = """
#version 450
layout(location=0) in vec3 in_position;
void main() { gl_Position = vec4(in_position, 1.0); }
"""


FRAGMENT = """
#version 450
layout(location=0) out vec4 out_color;
void main() { out_color = vec4(1.0); }
"""


def test_raw_glsl_phase_records_rewritten_engine_resource_layout() -> None:
    import tgfx  # noqa: F401  # Registers TcShader before TcMaterialPhase.shader casts it.

    material = TcMaterial.create("RawGlslEngineLayoutMaterial", "")
    phase = material.add_phase_from_sources(
        """
#version 450
layout(location=0) in vec3 in_position;
uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
void main() {
    gl_Position = u_projection * u_view * u_model * vec4(in_position, 1.0);
}
""",
        FRAGMENT,
        "",
        "RawGlslEngineLayoutShader",
        "opaque",
        0,
    )
    assert phase is not None

    shader = phase.shader
    assert shader.resource_binding_count == 2
    per_frame = shader.find_resource_binding("per_frame")
    draw_data = shader.find_resource_binding("draw_data")
    assert per_frame is not None
    assert draw_data is not None
    assert per_frame["kind_name"] == "constant_buffer"
    assert per_frame["scope_name"] == "frame"
    assert draw_data["kind_name"] == "constant_buffer"
    assert draw_data["scope_name"] == "draw"


def test_material_copy_survives_registry_growth() -> None:
    source = TcMaterial.create("PoolGrowthSource", "")
    assert source.is_valid
    phase = source.add_phase_from_sources(
        VERTEX,
        FRAGMENT,
        "",
        "PoolGrowthShader",
        "opaque",
        0,
    )
    assert phase is not None

    copies = []
    for index in range(160):
        copied = source.copy("")
        assert copied.is_valid
        assert copied.phase_count == 1
        assert copied.get_phase(0).phase_mark == "opaque"
        copied.name = f"PoolGrowthCopy_{index}"
        copies.append(copied)
