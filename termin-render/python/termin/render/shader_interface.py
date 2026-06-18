"""Compatibility re-export for shader interface comparison helpers.

Canonical module: :mod:`termin.default_assets.render.shader_interface`.
"""

from termin.default_assets.render.shader_interface import (
    ShaderInterfaceChange,
    compare_shader_interface,
    shader_graph_input_signature,
    shader_material_interface_signature,
)

__all__ = [
    "ShaderInterfaceChange",
    "compare_shader_interface",
    "shader_graph_input_signature",
    "shader_material_interface_signature",
]
