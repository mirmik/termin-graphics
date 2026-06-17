"""MaterialAsset - Asset for material configuration.

NOTE: This class has some deviations from the standard DataAsset pattern:

1. from_file() reads content immediately instead of lazy loading.
   Standard pattern defers reading until .data is accessed.

2. UUID is stored inside the .material JSON file, not in a separate .meta file.
   This requires manual UUID extraction in _parse_content().

3. _on_loaded() auto-saves the file if UUID was missing.
   This ensures all materials get persistent UUIDs.

These deviations exist because materials are self-contained JSON documents
that embed their own metadata. Since materials are typically small files
(a few KB), eager loading does not significantly impact engine performance.

See also: PrefabAsset (same pattern).
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING, Any, Dict

from termin_assets import DataAsset
from termin.render.shader_asset import make_phase_uuid
from tcbase import log

if TYPE_CHECKING:
    from termin.materials import TcMaterial


class MaterialAsset(DataAsset["TcMaterial"]):
    """
    Asset for material configuration.

    IMPORTANT: Create through ResourceManager, not directly.
    This ensures proper registration and avoids duplicates.

    Stores TcMaterial (shader reference, uniforms, textures).
    """

    _uses_binary = False  # JSON text format

    def __init__(
        self,
        material: "TcMaterial | None" = None,
        name: str = "material",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        super().__init__(data=material, name=name, source_path=source_path, uuid=uuid)

    # --- Convenience property ---

    @property
    def material(self) -> "TcMaterial | None":
        """Material configuration (lazy-loaded)."""
        return self.data

    @material.setter
    def material(self, value: "TcMaterial | None") -> None:
        """Set material and bump version."""
        self.data = value

    # --- Content parsing ---

    def _parse_content(self, content: str) -> "TcMaterial | None":
        """Parse JSON content into TcMaterial."""
        material, file_uuid = _parse_material_content(
            content,
            name=self._name,
            source_path=str(self._source_path) if self._source_path else None,
        )

        # Adopt UUID from file if present
        if file_uuid:
            self._uuid = file_uuid
            self._runtime_id = hash(self._uuid) & 0xFFFFFFFFFFFFFFFF
            # Mark that UUID was in the file, so we don't re-save
            self._has_uuid_in_spec = True

        return material

    def _on_loaded(self) -> None:
        """After loading, save file if it didn't have UUID."""
        # Check if file has UUID by re-reading (not ideal but simple)
        if self._source_path is not None:
            try:
                with open(self._source_path, "r", encoding="utf-8") as f:
                    data = json.load(f)
                if "uuid" not in data:
                    self.save_to_file()
            except Exception:
                log.warning(f"[MaterialAsset] Failed to re-read material file for UUID check: {self._source_path}")

    # --- Saving (materials save to their own file, not spec) ---

    def save_spec_file(self) -> bool:
        """Materials don't use spec files - save to the material file instead."""
        return self.save_to_file()

    def save_to_file(self, path: str | Path | None = None) -> bool:
        """
        Save material to .material file.

        Args:
            path: Path to save. If None, uses source_path.

        Returns:
            True if saved successfully.
        """
        if self._data is None:
            return False

        save_path = Path(path) if path else self._source_path
        if save_path is None:
            return False

        try:
            _save_material_file(self._data, save_path, uuid=self.uuid)
            self._source_path = Path(save_path)
            self.mark_just_saved()
            return True
        except Exception:
            log.error(f"[MaterialAsset] Failed to save material to file: {save_path}", exc_info=True)
            return False

    def update_from(self, other: "MaterialAsset") -> None:
        """
        Update material data from another asset (hot-reload).

        For TcMaterial, we replace entirely (hot-reload will recreate phases).
        """
        if other._data is not None:
            self._data = other._data
            self._loaded = True
            self._bump_version()

    # --- Factory methods ---

    @classmethod
    def from_file(cls, path: str | Path, name: str | None = None) -> "MaterialAsset":
        """Create MaterialAsset from .material file."""
        path = Path(path)
        material, file_uuid = _load_material_file(str(path))
        return cls(
            material=material,
            name=name or path.stem,
            source_path=path,
            uuid=file_uuid,
        )

    @classmethod
    def from_material(
        cls,
        material: "TcMaterial",
        name: str | None = None,
        source_path: str | Path | None = None,
        uuid: str | None = None,
    ) -> "MaterialAsset":
        """Create MaterialAsset from existing TcMaterial."""
        return cls(
            material=material,
            name=name or material.name or "material",
            source_path=source_path or material.source_path,
            uuid=uuid,
        )


# --- File I/O functions ---

def _build_render_state(shader_phase, phase_mark: str | None = None):
    """Build tc_render_state from shader phase flags."""
    from termin.materials import TcRenderState

    # Start with default based on phase mark
    mark = phase_mark or shader_phase.phase_mark
    if mark == "transparent":
        state = TcRenderState.transparent()
    elif mark == "wireframe":
        state = TcRenderState.wireframe()
    else:
        state = TcRenderState.opaque()

    # Check for per-mark settings first
    if phase_mark:
        mark_settings = shader_phase.mark_settings.get(phase_mark)
        if mark_settings:
            if mark_settings.gl_depth_mask is not None:
                state.depth_write = 1 if mark_settings.gl_depth_mask else 0
            if mark_settings.gl_depth_test is not None:
                state.depth_test = 1 if mark_settings.gl_depth_test else 0
            if mark_settings.gl_blend is not None:
                state.blend = 1 if mark_settings.gl_blend else 0
            if mark_settings.gl_cull is not None:
                state.cull = 1 if mark_settings.gl_cull else 0
            return state

    # Apply phase-level overrides
    if shader_phase.gl_depth_mask is not None:
        state.depth_write = 1 if shader_phase.gl_depth_mask else 0
    if shader_phase.gl_depth_test is not None:
        state.depth_test = 1 if shader_phase.gl_depth_test else 0
    if shader_phase.gl_blend is not None:
        state.blend = 1 if shader_phase.gl_blend else 0
    if shader_phase.gl_cull is not None:
        state.cull = 1 if shader_phase.gl_cull else 0

    return state


def _apply_uniform_defaults(phase, shader_phase, uniforms: dict):
    """Apply uniform defaults from shader phase and extra uniforms."""
    from termin.geombase import Vec3, Vec4

    shader_uniforms = list(shader_phase.uniforms) + list(shader_phase.material_uniforms)

    # Apply defaults from shader phase properties and non-inspector uniforms
    for prop in shader_uniforms:
        name = prop.name
        default = prop.default  # Note: binding exposes as 'default', not 'default_value'

        if default is None:
            continue

        prop_type = prop.property_type
        if prop_type == "Float":
            phase.set_uniform_float(name, float(default))
        elif prop_type == "Int":
            phase.set_uniform_int(name, int(default))
        elif prop_type == "Bool":
            phase.set_uniform_int(name, 1 if default else 0)
        elif prop_type == "Vec3" and isinstance(default, (list, tuple)) and len(default) >= 3:
            phase.set_uniform_vec3(name, Vec3(default[0], default[1], default[2]))
        elif prop_type in ("Vec4", "Color") and isinstance(default, (list, tuple)) and len(default) >= 4:
            phase.set_uniform_vec4(name, Vec4(default[0], default[1], default[2], default[3]))

    # Apply extra uniforms (from .material file)
    for name, value in uniforms.items():
        if isinstance(value, Vec3):
            phase.set_uniform_vec3(name, value)
        elif isinstance(value, Vec4):
            phase.set_uniform_vec4(name, value)
        elif isinstance(value, float):
            phase.set_uniform_float(name, value)
        elif isinstance(value, bool):
            phase.set_uniform_int(name, 1 if value else 0)
        elif isinstance(value, int):
            phase.set_uniform_int(name, value)


def _apply_texture_defaults(phase, shader_phase, rm):
    """Apply default textures from shader phase properties."""
    from termin.render.texture_handle import get_white_texture_handle, get_normal_texture_handle

    shader_uniforms = list(shader_phase.uniforms) + list(shader_phase.material_uniforms)
    for prop in shader_uniforms:
        if prop.property_type != "Texture":
            continue

        name = prop.name
        default = prop.default

        # Get default texture based on property default value
        if isinstance(default, str) and default == "normal":
            tex_handle = get_normal_texture_handle()
        else:
            tex_handle = get_white_texture_handle()

        tc_tex = tex_handle.get()
        if tc_tex is not None:
            phase.set_texture(name, tc_tex)
        else:
            log.warn(f"[MaterialAsset] Failed to get default texture for '{name}'")


def _parse_material_content(
    content: str,
    name: str | None = None,
    source_path: str | None = None,
) -> tuple["TcMaterial", str | None]:
    """
    Parse material from JSON content string.

    Args:
        content: JSON content of .material file
        name: Material name (defaults to "material")
        source_path: Source path for the material

    Returns:
        Tuple of (TcMaterial, uuid or None)
    """
    from termin.assets.resources import ResourceManager
    from termin.render.texture_asset import TextureAsset
    from termin.geombase import Vec3, Vec4
    from termin.materials import TcMaterial

    data = json.loads(content)

    shader_uuid = data.get("shader_uuid")
    shader_name = data.get("shader", "BlinnPhong")
    file_uuid = data.get("uuid")
    phase_marks = data.get("phase_marks", [])  # Per-phase mark overrides

    rm = ResourceManager.instance()

    # Try to load shader by UUID first, fallback to name
    program = None
    shader_asset_uuid = shader_uuid  # The actual asset uuid for phase uuid generation
    shader_asset = None
    if shader_uuid:
        from termin.render.shader_asset import ShaderAsset

        candidate = rm._assets_by_uuid.get(shader_uuid)
        if isinstance(candidate, ShaderAsset):
            shader_asset = candidate
        program = rm.get_shader_by_uuid(shader_uuid)
    if program is None:
        program = rm.get_shader(shader_name)
        # Get shader asset uuid from registry
        shader_asset = rm.get_shader_asset(shader_name)
        if shader_asset is not None:
            shader_asset_uuid = shader_asset.uuid

    if program is None:
        log.error(f"[MaterialAsset] Shader not found (uuid={shader_uuid}, name={shader_name}), creating empty material")
        mat = TcMaterial.create(name or "unknown", file_uuid or "")
        mat.shader_name = shader_name
        if source_path:
            mat.source_path = source_path
        return mat, file_uuid

    # Convert uniforms
    uniforms_data = data.get("uniforms", {})
    uniforms: Dict[str, Any] = {}
    for uname, value in uniforms_data.items():
        if isinstance(value, list):
            if len(value) == 3:
                uniforms[uname] = Vec3(value[0], value[1], value[2])
            elif len(value) == 4:
                uniforms[uname] = Vec4(value[0], value[1], value[2], value[3])
            else:
                uniforms[uname] = [float(v) for v in value]
        else:
            uniforms[uname] = value

    # Load textures by asset UUID
    textures_data = data.get("textures", {})
    textures = {}
    for uniform_name, tex_asset_uuid in textures_data.items():
        asset = rm._assets_by_uuid.get(tex_asset_uuid)
        if isinstance(asset, TextureAsset):
            if asset.texture_data is None and not asset.ensure_loaded():
                log.warning(f"[MaterialAsset] Texture asset failed to load by UUID: {tex_asset_uuid}")
                continue
            if asset.texture_data is None:
                log.warning(f"[MaterialAsset] Texture asset loaded without data: {tex_asset_uuid}")
                continue
            textures[uniform_name] = asset.texture_data
        else:
            log.warning(f"[MaterialAsset] Texture asset not found by UUID: {tex_asset_uuid}")

    # Load non-asset texture references (Phase 8): render-target color/depth
    # bindings serialized as `texture_refs: {uniform: {kind, target, channel}}`.
    # Resolved by render-target name; ensure_textures() is called on the
    # target so its color/depth handles exist before we bind them.
    texture_refs = data.get("texture_refs", {})
    for uniform_name, ref in texture_refs.items():
        tc_tex = _resolve_texture_ref(ref)
        if tc_tex is not None:
            textures[uniform_name] = tc_tex

    # Create TcMaterial
    mat = TcMaterial.create(name or "material", file_uuid or "")
    mat.shader_name = shader_name
    if source_path:
        mat.source_path = source_path

    for i, shader_phase in enumerate(program.phases):
        if "vertex" not in shader_phase.stages or "fragment" not in shader_phase.stages:
            log.warning(f"[MaterialAsset] Phase {i} missing vertex or fragment shader")
            continue

        # Determine phase mark (apply override if specified)
        phase_mark = shader_phase.phase_mark
        if i < len(phase_marks) and phase_marks[i]:
            phase_mark = phase_marks[i]

        # Build render state
        state = _build_render_state(shader_phase, phase_mark)

        # Generate phase uuid for hot-reload support
        phase_uuid = ""
        if shader_asset_uuid:
            phase_uuid = make_phase_uuid(shader_asset_uuid, shader_phase.phase_mark)

        shader = shader_asset.get_tc_shader_for_phase(shader_phase) if shader_asset is not None else None
        if shader is None or not shader.is_valid:
            log.error(f"[MaterialAsset] Shader phase not found: {phase_uuid}")
            continue

        phase = mat.add_phase(shader, phase_mark, shader_phase.priority)

        if phase is None:
            log.error(f"[MaterialAsset] Failed to add phase {i}")
            continue
        phase.state = state

        # Set available marks
        if shader_phase.available_marks:
            phase.set_available_marks(shader_phase.available_marks)

        # Apply uniform defaults
        _apply_uniform_defaults(phase, shader_phase, uniforms)

        # Set default textures from shader properties
        _apply_texture_defaults(phase, shader_phase, rm)

        # Apply textures from .material file (override defaults)
        for tex_name, tc_tex in textures.items():
            if tc_tex is not None and tc_tex.is_valid:
                phase.set_texture(tex_name, tc_tex)

    return mat, file_uuid


def _load_material_file(path: str) -> tuple["TcMaterial", str | None]:
    """
    Load material from .material file.

    Args:
        path: Path to .material file

    Returns:
        Tuple of (TcMaterial, uuid or None)
    """
    path = Path(path)

    with open(path, "r", encoding="utf-8") as f:
        content = f.read()

    return _parse_material_content(content, name=path.stem, source_path=str(path))


def _save_material_file(material, path: str | Path, uuid: str) -> None:
    """
    Save material to .material file.

    Args:
        material: TcMaterial to save
        path: Path to save to
        uuid: UUID to include in file
    """
    from termin.materials import TcMaterial
    from termin.geombase import Vec3, Vec4
    from termin.assets.resources import ResourceManager

    shader_name = material.shader_name

    # Get shader UUID from ResourceManager
    shader_uuid = ""
    if shader_name:
        rm = ResourceManager.instance()
        shader_asset = rm.get_shader_asset(shader_name)
        if shader_asset is not None:
            shader_uuid = shader_asset.uuid

    result: Dict[str, Any] = {
        "uuid": uuid,
        "shader": shader_name,
    }
    if shader_uuid:
        result["shader_uuid"] = shader_uuid

    # For TcMaterial, save phase marks, uniforms, and textures
    if isinstance(material, TcMaterial):
        phase_marks = []
        has_overrides = False
        for i in range(material.phase_count):
            phase = material.get_phase(i)
            if phase:
                available = phase.get_available_marks()
                default_mark = available[0] if available else ""
                if phase.phase_mark != default_mark:
                    phase_marks.append(phase.phase_mark)
                    has_overrides = True
                else:
                    phase_marks.append("")
        if has_overrides:
            result["phase_marks"] = phase_marks

        # Save material-level uniforms aggregated across phases.
        uniforms_data: Dict[str, Any] = {}
        for name, value in material.uniforms.items():
            if isinstance(value, Vec3):
                uniforms_data[name] = [value.x, value.y, value.z]
            elif isinstance(value, Vec4):
                uniforms_data[name] = [value.x, value.y, value.z, value.w]
            elif isinstance(value, (int, float, bool)):
                uniforms_data[name] = value
            elif isinstance(value, tuple):
                uniforms_data[name] = list(value)
        if uniforms_data:
            result["uniforms"] = uniforms_data

        # Save material-level textures. Two destinations:
        #   - `textures`: uniform → asset UUID (regular TextureAsset).
        #   - `texture_refs`: uniform → {kind, target, channel} for non-asset
        #     handles (currently render-target color/depth — Phase 8).
        textures_data: Dict[str, str] = {}
        texture_refs_data: Dict[str, Dict[str, str]] = {}
        for name, tex in material.textures.items():
            if tex is None or not tex.is_valid:
                continue
            tex_name = tex.name
            # Skip default placeholder textures (by name)
            if tex_name in ("__white_1x1__", "__normal_1x1__"):
                continue
            # First try regular TextureAsset lookup.
            asset_uuid = None
            for _asset_name, asset in rm._texture_registry.assets.items():
                if asset.uuid and asset.uuid == tex.uuid:
                    asset_uuid = asset.uuid
                    break
                if asset.texture_data is not None and asset.texture_data.uuid == tex.uuid:
                    asset_uuid = asset.uuid
                    break
            if asset_uuid:
                textures_data[name] = asset_uuid
                continue
            # Fallback: maybe it's an RT-owned texture. Walk live RTs
            # and match on tc_texture uuid.
            ref = _classify_render_target_texture(tex)
            if ref is not None:
                texture_refs_data[name] = ref
            else:
                log.warning(
                    f"[MaterialAsset] Texture '{name}' was not saved: "
                    f"no TextureAsset or render-target match for tc_uuid={tex.uuid} tc_name='{tex.name}'"
                )
        if textures_data:
            result["textures"] = textures_data
        if texture_refs_data:
            result["texture_refs"] = texture_refs_data

    with open(path, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)


# --- Render-target texture references (Phase 8) ---------------------------
#
# Materials can sample render-target color/depth as textures. RT-owned
# tc_textures don't have a TextureAsset behind them, so they're persisted
# in a separate `texture_refs` section keyed by render-target name.

def _iter_render_targets():
    """Yield (handle, name) for every alive render target in the pool."""
    try:
        # Native module — `termin.render_framework.__init__` does not
        # re-export render_target_pool_list in the app package layout.
        from termin.render_framework._render_framework_native import (
            render_target_pool_list,
        )
    except ImportError:
        log.debug("[MaterialAsset] render_target_pool_list not available, skipping RT texture resolution")
        return
    for h in render_target_pool_list():
        if not h.alive:
            continue
        yield h, h.name


def _classify_render_target_texture(tc_tex) -> Dict[str, str] | None:
    """If `tc_tex` is the color or depth channel of a live render target,
    return a serializable ref dict. Otherwise None.
    """
    if tc_tex is None or not tc_tex.is_valid:
        return None
    target_uuid = tc_tex.uuid
    for h, name in _iter_render_targets():
        if not name:
            continue
        color_tex = h.color_texture
        if color_tex is not None and color_tex.is_valid \
                and color_tex.uuid == target_uuid:
            return {"kind": "render_target", "target": name, "channel": "color"}
        depth_tex = h.depth_texture
        if depth_tex is not None and depth_tex.is_valid \
                and depth_tex.uuid == target_uuid:
            return {"kind": "render_target", "target": name, "channel": "depth"}
    return None


def _resolve_texture_ref(ref: Dict[str, Any]):
    """Resolve a `texture_refs` entry into a TcTexture instance.

    Returns the TcTexture or None if the target isn't currently in the pool.
    `ensure_textures()` is called so the channel handles exist before bind.
    """
    if not isinstance(ref, dict):
        return None
    kind = ref.get("kind")
    if kind != "render_target":
        log.warning(f"[MaterialAsset] Unknown texture_ref kind: {kind}")
        return None
    target_name = ref.get("target", "")
    channel = ref.get("channel", "color")
    if not target_name:
        return None
    for h, name in _iter_render_targets():
        if name != target_name:
            continue
        h.ensure_textures()
        if channel == "color":
            return h.color_texture
        if channel == "depth":
            return h.depth_texture
        log.warning(f"[MaterialAsset] Unknown channel '{channel}' for RT '{target_name}'")
        return None
    log.warning(f"[MaterialAsset] Render target '{target_name}' not found in pool")
    return None
