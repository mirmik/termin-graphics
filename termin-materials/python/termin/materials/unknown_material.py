"""Missing-material runtime fallback."""

from __future__ import annotations

from termin.materials import TcMaterial, TcRenderState


UNKNOWN_VERT = """#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;

out vec3 v_normal;
out vec3 v_world_pos;

void main() {
    vec4 world = u_model * vec4(a_position, 1.0);
    v_world_pos = world.xyz;
    v_normal = mat3(transpose(inverse(u_model))) * a_normal;
    gl_Position = u_projection * u_view * world;
}
"""

UNKNOWN_FRAG = """#version 330 core

in vec3 v_normal;
in vec3 v_world_pos;

uniform vec3 u_camera_position;

out vec4 FragColor;

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_camera_position - v_world_pos);

    float up = dot(N, vec3(0.0, 0.0, 1.0)) * 0.5 + 0.5;
    vec3 base_color = vec3(1.0, 0.0, 1.0);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);

    vec3 color = base_color * (0.3 + 0.7 * up) + vec3(1.0) * fresnel * 0.3;
    FragColor = vec4(color, 1.0);
}
"""


def create_unknown_material(
    name: str = "UnknownMaterial",
    error_message: str | None = None,
) -> TcMaterial:
    """Create a visible fallback material for missing or broken materials."""
    mat_name = name
    if error_message:
        mat_name = f"{name}: {error_message[:50]}"

    mat = TcMaterial.create(mat_name, "")
    mat.shader_name = "UnknownShader"

    phase = mat.add_phase_from_sources(
        vertex_source=UNKNOWN_VERT,
        fragment_source=UNKNOWN_FRAG,
        geometry_source="",
        shader_name="UnknownShader",
        phase_mark="opaque",
        priority=0,
        state=TcRenderState.opaque(),
    )

    if phase is not None:
        phase.set_color(1.0, 0.0, 1.0, 1.0)

    return mat


class UnknownMaterial(TcMaterial):
    """Fallback material for missing or broken shaders and materials."""

    def __new__(
        cls,
        original_shader: str | None = None,
        original_material: str | None = None,
        error_message: str | None = None,
    ) -> TcMaterial:
        msg = error_message
        if not msg:
            if original_shader:
                msg = f"Missing shader: {original_shader}"
            elif original_material:
                msg = f"Missing material: {original_material}"
        return create_unknown_material(error_message=msg)

    @classmethod
    def for_missing_shader(cls, shader_name: str) -> TcMaterial:
        """Create an UnknownMaterial for a missing shader."""
        return create_unknown_material(error_message=f"Missing shader: {shader_name}")

    @classmethod
    def for_missing_material(
        cls,
        material_name: str,
    ) -> TcMaterial:
        """Create an UnknownMaterial for a missing material."""
        return create_unknown_material(error_message=f"Missing material: {material_name}")

    @classmethod
    def for_error(
        cls,
        error: Exception,
    ) -> TcMaterial:
        """Create an UnknownMaterial for a material loading error."""
        return create_unknown_material(error_message=str(error))


def material_or_unknown(material: TcMaterial | None, material_name: str) -> TcMaterial:
    """Return a material or a visible fallback for missing material references."""
    if material is not None:
        return material
    return UnknownMaterial.for_missing_material(material_name)
