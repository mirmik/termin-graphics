import numpy as np
import pytest

from termin.geombase import LinearColor, Mat44, Mat44f, SrgbColor, Vec4
from termin.materials import TcMaterial
from tgfx import ShaderLanguage


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


def _phase():
    material = TcMaterial.create("TypedMatrixParamMaterial", "")
    phase = material.add_phase_from_sources(
        VERTEX,
        FRAGMENT,
        "",
        "TypedMatrixParamShader",
        "opaque",
        0,
        language=ShaderLanguage.GLSL.value,
    )
    assert phase is not None
    return phase


@pytest.mark.parametrize("matrix_type", [Mat44f, Mat44])
def test_set_param_accepts_typed_column_major_mat4(matrix_type) -> None:
    phase = _phase()
    matrix = matrix_type.zero()
    for column in range(4):
        for row in range(4):
            matrix[column, row] = column * 10 + row

    phase.set_param("u_transform", matrix)

    stored = phase.uniforms["u_transform"]
    assert isinstance(stored, Mat44f)
    for column in range(4):
        for row in range(4):
            assert stored[column, row] == pytest.approx(column * 10 + row)


def test_set_param_rejects_ambiguous_raw_mat4_buffer() -> None:
    phase = _phase()
    c_order_matrix = np.arange(16, dtype=np.float32).reshape(4, 4)

    with pytest.raises(RuntimeError, match="raw 16-element buffers"):
        phase.set_param("u_transform", c_order_matrix)

    assert "u_transform" not in phase.uniforms


def test_set_param_preserves_distinct_color_kinds() -> None:
    phase = _phase()

    phase.set_param("u_authored", SrgbColor(0.25, 0.5, 0.75, 1.0))
    phase.set_param("u_radiance", LinearColor(2.0, 1.5, 0.5, 0.75))
    phase.set_param("u_numeric", Vec4(0.25, 0.5, 0.75, 1.0))

    assert isinstance(phase.uniforms["u_authored"], SrgbColor)
    assert isinstance(phase.uniforms["u_radiance"], LinearColor)
    assert isinstance(phase.uniforms["u_numeric"], Vec4)
    assert phase.get_uniform_srgb_color("u_authored").g == pytest.approx(0.5)
    assert phase.get_uniform_linear_color("u_radiance").r == pytest.approx(2.0)


def test_color_uniform_kind_cannot_change_in_place() -> None:
    phase = _phase()
    phase.set_uniform_srgb_color("u_color", SrgbColor(0.1, 0.2, 0.3, 1.0))

    with pytest.raises(RuntimeError, match="linear color uniform"):
        phase.set_uniform_linear_color("u_color", LinearColor(0.1, 0.2, 0.3, 1.0))

    stored = phase.uniforms["u_color"]
    assert isinstance(stored, SrgbColor)
    assert stored.r == pytest.approx(0.1)
