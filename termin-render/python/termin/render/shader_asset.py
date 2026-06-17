"""ShaderAsset - Asset for shader programs."""

from __future__ import annotations

import uuid
import re
from pathlib import Path
from typing import TYPE_CHECKING

from termin_assets import DataAsset
from tcbase import log
from tgfx import TcShader

if TYPE_CHECKING:
    from termin.materials import ShaderMultyPhaseProgramm

_GLSL_MATERIAL_BINDING = 1
_GLSL_PER_FRAME_BINDING = 2
_GLSL_DRAW_DATA_BINDING = 24
_GLSL_MATERIAL_TEXTURE_BINDING_BASE = 4

_RESOURCE_CONSTANT_BUFFER = 1
_RESOURCE_TEXTURE = 2
_SCOPE_FRAME = 1
_SCOPE_MATERIAL = 3
_SCOPE_DRAW = 4
_SET_DEFAULT = 0
_STAGE_ALL_GRAPHICS = 0x7


def _phase_slug(phase_mark: str) -> str:
    slug = re.sub(r"[^A-Za-z0-9_.-]+", "-", phase_mark.strip()).strip("-")
    return slug or "default"


def make_phase_uuid(shader_uuid: str, phase_mark: str) -> str:
    """Generate deterministic UUID for shader phase.

    RFC UUID assets keep the historical UUID5 phase derivation. Named engine
    and stdlib shader ids use readable phase ids instead.
    """
    if not phase_mark:
        return shader_uuid
    try:
        base = uuid.UUID(shader_uuid)
        return str(uuid.uuid5(base, phase_mark))
    except ValueError:
        slug = _phase_slug(phase_mark)
        stdlib_prefix = "termin-stdlib-shader-"
        if shader_uuid.startswith(stdlib_prefix):
            stdlib_name = shader_uuid[len(stdlib_prefix):]
            candidate = f"stdlib-{stdlib_name}-{slug}"
            if len(candidate) < 40:
                return candidate
        else:
            candidate = f"{shader_uuid}-{slug}"
            if len(candidate) < 40:
                return candidate
        digest = uuid.uuid5(uuid.NAMESPACE_URL, f"{shader_uuid}:{phase_mark}").hex[:6]
        suffix = f"-{slug}-{digest}"
        prefix_size = max(1, 39 - len(suffix))
        return f"{shader_uuid[:prefix_size]}{suffix}"


def shader_language_enum(language: str):
    table = {
        "glsl": 0,
        "slang": 1,
        "hlsl": 2,
    }
    key = language.lower()
    if key not in table:
        log.error(f"[ShaderAsset] Unsupported shader language: {language}")
        raise ValueError(f"Unsupported shader language: {language}")
    return table[key]


def update_material_shader(material, program, shader_name: str, shader_uuid: str) -> None:
    """Update material's shader with proper phase UUIDs for hot-reload.

    Args:
        material: TcMaterial to update
        program: ShaderMultyPhaseProgramm - new shader program
        shader_name: Shader name
        shader_uuid: Shader asset UUID
    """
    from termin.render.material_asset import _build_render_state, _apply_uniform_defaults, _apply_texture_defaults
    from termin.assets.resources import ResourceManager

    if not program.phases:
        raise ValueError("Program has no phases")

    # Preserve existing material-level uniforms/textures.
    old_uniforms = dict(material.uniforms)
    old_textures = dict(material.textures)

    # Clear existing phases and rebuild
    material.clear_phases()
    material.shader_name = shader_name or program.program

    rm = ResourceManager.instance()

    for shader_phase in program.phases:
        if "vertex" not in shader_phase.stages or "fragment" not in shader_phase.stages:
            log.warning("[update_material_shader] Phase missing vertex or fragment shader")
            continue

        # Build render state
        state = _build_render_state(shader_phase, shader_phase.phase_mark)

        # Generate phase uuid for hot-reload support
        phase_uuid = make_phase_uuid(shader_uuid, shader_phase.phase_mark) if shader_uuid else ""
        shader = TcShader.from_uuid(phase_uuid) if phase_uuid else TcShader()
        if not shader.is_valid:
            log.error(f"[update_material_shader] Shader phase not found: {phase_uuid}")
            continue

        phase = material.add_phase(shader, shader_phase.phase_mark, shader_phase.priority)

        if phase is None:
            log.error("[update_material_shader] Failed to add phase")
            continue
        phase.state = state

        # Set available marks
        if shader_phase.available_marks:
            phase.set_available_marks(shader_phase.available_marks)

        # Apply uniform defaults from shader, then restore old values
        _apply_uniform_defaults(phase, shader_phase, old_uniforms)

        # Apply texture defaults, then restore old textures
        _apply_texture_defaults(phase, shader_phase, rm)
        for key, tex in old_textures.items():
            if key in phase.textures and tex is not None and tex.is_valid:
                phase.set_texture(key, tex)


class ShaderAsset(DataAsset["ShaderMultyPhaseProgramm"]):
    """
    Asset for shader program definition.

    IMPORTANT: Create through ResourceManager, not directly.
    This ensures proper registration and avoids duplicates.

    Stores ShaderMultyPhaseProgramm (parsed shader with phases, uniforms).
    GPU compilation is handled by TcShader inside MaterialPhase.
    """

    _uses_binary = False  # Shader text format

    def __init__(
        self,
        program: "ShaderMultyPhaseProgramm | None" = None,
        name: str = "shader",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        super().__init__(data=program, name=name, source_path=source_path, uuid=uuid)
        if program is not None:
            self._update_tc_shaders()

    # --- Convenience property ---

    @property
    def program(self) -> "ShaderMultyPhaseProgramm | None":
        """Shader program definition (lazy-loaded)."""
        return self.data

    @program.setter
    def program(self, value: "ShaderMultyPhaseProgramm | None") -> None:
        """Set program and bump version."""
        self.data = value

    # --- Content parsing ---

    def _parse_content(self, content: str) -> "ShaderMultyPhaseProgramm | None":
        """Parse shader source text into ShaderMultyPhaseProgramm."""
        from termin.materials import (
            parse_shader_text,
            ShaderMultyPhaseProgramm,
        )

        tree = parse_shader_text(content)
        return ShaderMultyPhaseProgramm.from_tree(tree)

    def get_tc_shader_for_phase(self, phase) -> TcShader:
        """Return the registry shader for a parsed phase, creating it if missing."""
        return self._ensure_tc_shader_for_phase(phase)

    def _ensure_tc_shader_for_phase(self, phase) -> TcShader:
        if self._data is None or not self._uuid:
            return TcShader()

        phase_uuid = make_phase_uuid(self._uuid, phase.phase_mark)
        tc = TcShader.get_or_create(phase_uuid)
        if not tc.is_valid:
            log.error(f"[ShaderAsset] Failed to get/create phase shader: {phase_uuid}")
            return TcShader()

        self._configure_tc_shader_for_phase(tc, phase)
        return tc

    def _configure_tc_shader_for_phase(self, tc: TcShader, phase) -> None:
        if self._data is None:
            return

        vs = phase.stages.get("vertex")
        fs = phase.stages.get("fragment")
        gs = phase.stages.get("geometry")
        if vs is None or fs is None:
            log.error(
                f"[ShaderAsset] Phase '{phase.phase_mark}' in shader '{self._name}' "
                "has no complete vertex/fragment stage pair"
            )
            return

        vertex_src = vs.source
        fragment_src = fs.source
        geometry_src = gs.source if gs else ""
        source_path = str(self._source_path) if self._source_path else ""

        tc.set_language(shader_language_enum(self._data.language))
        tc.set_sources_with_entries(
            vertex_src,
            fragment_src,
            geometry_src,
            self._name,
            source_path,
            vs.entry,
            fs.entry,
            gs.entry if gs else "",
        )

        tc.set_features(0)
        for feature in self._data.features:
            if feature == "lighting_ubo":
                tc.set_feature(1)

        layout = phase.material_ubo_layout
        if self._data.language.lower() == "glsl" and layout is not None and layout.block_size > 0:
            entries = [
                (e.name, e.property_type, e.offset, e.size)
                for e in layout.entries
            ]
            tc.set_material_ubo_layout(entries, layout.block_size)
            resource_layout = [
                (
                    "material",
                    _RESOURCE_CONSTANT_BUFFER,
                    _SCOPE_MATERIAL,
                    _SET_DEFAULT,
                    _GLSL_MATERIAL_BINDING,
                    _STAGE_ALL_GRAPHICS,
                    layout.block_size,
                )
            ]
        else:
            tc.set_material_ubo_layout([], 0)
            resource_layout = []

        if self._data.language.lower() == "glsl":
            if phase.uses_engine_per_frame:
                resource_layout.append((
                    "per_frame",
                    _RESOURCE_CONSTANT_BUFFER,
                    _SCOPE_FRAME,
                    _SET_DEFAULT,
                    _GLSL_PER_FRAME_BINDING,
                    _STAGE_ALL_GRAPHICS,
                    0,
                ))
            if phase.uses_engine_draw_data:
                resource_layout.append((
                    "draw_data",
                    _RESOURCE_CONSTANT_BUFFER,
                    _SCOPE_DRAW,
                    _SET_DEFAULT,
                    _GLSL_DRAW_DATA_BINDING,
                    _STAGE_ALL_GRAPHICS,
                    64,
                ))
            for index, name in enumerate(phase.material_texture_resources):
                resource_layout.append((
                    name,
                    _RESOURCE_TEXTURE,
                    _SCOPE_MATERIAL,
                    _SET_DEFAULT,
                    _GLSL_MATERIAL_TEXTURE_BINDING_BASE + index,
                    _STAGE_ALL_GRAPHICS,
                    0,
                ))
        tc.set_resource_layout(resource_layout)

    def _update_tc_shaders(self) -> None:
        """Update tc_shader registry for hot-reload.

        Runs at initial shader asset load AND on hot-reload from disk.
        ShaderAsset owns phase shader creation and source/layout updates.
        """
        if self._data is None or not self._uuid:
            return

        for phase in self._data.phases:
            self._ensure_tc_shader_for_phase(phase)

    def _on_loaded(self) -> None:
        """After loading/reloading, update tc_shader registry for hot-reload."""
        self._update_tc_shaders()

    # --- Factory methods ---

    @classmethod
    def from_file(cls, path: str | Path, name: str | None = None) -> "ShaderAsset":
        """Create ShaderAsset from .shader file."""
        from termin.materials import (
            parse_shader_text,
            ShaderMultyPhaseProgramm,
        )

        path = Path(path)
        with open(path, "r", encoding="utf-8") as f:
            shader_text = f.read()

        tree = parse_shader_text(shader_text)
        program = ShaderMultyPhaseProgramm.from_tree(tree)

        return cls(
            program=program,
            name=name or path.stem,
            source_path=path,
        )

    @classmethod
    def from_program(
        cls,
        program: "ShaderMultyPhaseProgramm",
        name: str | None = None,
        source_path: str | Path | None = None,
        uuid: str | None = None,
    ) -> "ShaderAsset":
        """Create ShaderAsset from existing ShaderMultyPhaseProgramm."""
        return cls(
            program=program,
            name=name or program.program or "shader",
            source_path=source_path,
            uuid=uuid,
        )
