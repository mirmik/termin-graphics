"""Shader interface comparison helpers for hot-reload decisions."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any


PropertySignature = tuple[str, str]
UboSignature = tuple[tuple[str, str, int, int], int]
PhaseSignature = tuple[
    str,
    tuple[str, ...],
    int,
    bool | None,
    bool | None,
    bool | None,
    bool | None,
    tuple[PropertySignature, ...],
    tuple[PropertySignature, ...],
    UboSignature,
]
GraphInputSignature = tuple[PropertySignature, ...]


@dataclass(frozen=True)
class ShaderInterfaceChange:
    """Describes which parts of shader interface changed."""

    material_changed: bool
    graph_inputs_changed: bool


def _property_signature(prop: Any) -> PropertySignature:
    return (prop.name, prop.property_type)


def _property_list_signature(props: list[Any]) -> tuple[PropertySignature, ...]:
    return tuple(_property_signature(prop) for prop in props)


def _ubo_signature(layout: Any | None) -> UboSignature:
    if layout is None:
        return ((), 0)
    entries = tuple(
        (entry.name, entry.property_type, entry.offset, entry.size)
        for entry in layout.entries
    )
    return (entries, layout.block_size)


def shader_material_interface_signature(program: Any | None) -> tuple[Any, ...]:
    """Return the material-facing interface signature of a shader program."""
    if program is None:
        return ()

    result: list[Any] = [
        ("material_properties", _property_list_signature(list(program.material_properties))),
    ]
    for phase in program.phases:
        result.append(
            (
                phase.phase_mark,
                tuple(phase.available_marks),
                phase.priority,
                phase.gl_depth_mask,
                phase.gl_depth_test,
                phase.gl_blend,
                phase.gl_cull,
                _property_list_signature(list(phase.uniforms)),
                _property_list_signature(list(phase.material_uniforms)),
                _ubo_signature(phase.material_ubo_layout),
            )
        )
    return tuple(result)


def shader_graph_input_signature(program: Any | None) -> GraphInputSignature:
    """Return MaterialPass dynamic texture inputs exposed by the shader."""
    if program is None:
        return ()

    inputs: list[PropertySignature] = []
    for prop in program.material_properties:
        if prop.property_type == "Texture":
            inputs.append(_property_signature(prop))
    return tuple(inputs)


def compare_shader_interface(old_program: Any | None, new_program: Any | None) -> ShaderInterfaceChange:
    """Compare old/new shader programs for hot-reload invalidation."""
    old_material = shader_material_interface_signature(old_program)
    new_material = shader_material_interface_signature(new_program)
    old_graph_inputs = shader_graph_input_signature(old_program)
    new_graph_inputs = shader_graph_input_signature(new_program)
    return ShaderInterfaceChange(
        material_changed=old_material != new_material,
        graph_inputs_changed=old_graph_inputs != new_graph_inputs,
    )
