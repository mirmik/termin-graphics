"""Compatibility re-export for material assets.

Canonical module: :mod:`termin.default_assets.render.material_asset`.
"""

from termin.default_assets.render.material_asset import (
    MaterialAsset,
    _apply_texture_defaults,
    _apply_uniform_defaults,
    _build_render_state,
    _classify_render_target_texture,
    _iter_render_targets,
    _load_material_file,
    _parse_material_content,
    _resolve_texture_ref,
    _save_material_file,
)

__all__ = [
    "MaterialAsset",
    "_apply_texture_defaults",
    "_apply_uniform_defaults",
    "_build_render_state",
    "_classify_render_target_texture",
    "_iter_render_targets",
    "_load_material_file",
    "_parse_material_content",
    "_resolve_texture_ref",
    "_save_material_file",
]
