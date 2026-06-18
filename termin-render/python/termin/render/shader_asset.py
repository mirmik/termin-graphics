"""Compatibility re-export for shader assets.

Canonical module: :mod:`termin.default_assets.render.shader_asset`.
"""

from termin.default_assets.render.shader_asset import (
    ShaderAsset,
    make_phase_uuid,
    shader_language_enum,
    update_material_shader,
)

__all__ = [
    "ShaderAsset",
    "make_phase_uuid",
    "shader_language_enum",
    "update_material_shader",
]
