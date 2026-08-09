"""Explicit native cgltf GLB import boundary.

This module intentionally does not fall back to the Python loader. The caller
first discovers mesh identities, resolves embedded asset UUIDs, and only then
publishes selected meshes into the native registry.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from termin.glb import _glb_native


@dataclass(frozen=True)
class NativeMeshInfo:
    name: str
    primitive_count: int
    vertex_count: int
    index_count: int
    skinned: bool


@dataclass(frozen=True)
class NativeMeshBuildDiagnostics:
    payload_bytes: int
    payload_hash: int


@dataclass(frozen=True)
class NativeImageInfo:
    index: int
    name: str
    has_name: bool
    mime_type: str
    uri: str
    embedded: bool
    encoded_size: int


@dataclass(frozen=True)
class NativeTextureInfo:
    index: int
    name: str
    has_name: bool
    image_index: int
    sampler_index: int | None
    selected_webp: bool
    selected_basisu: bool
    mag_filter: int
    min_filter: int
    wrap_s: int
    wrap_t: int


@dataclass(frozen=True)
class NativeTextureViewInfo:
    texture_index: int
    texcoord: int
    scale: float
    has_transform: bool


@dataclass(frozen=True)
class NativeMaterialInfo:
    name: str
    base_color_factor: tuple[float, float, float, float]
    metallic_factor: float
    roughness_factor: float
    base_color_texture: NativeTextureViewInfo | None
    metallic_roughness_texture: NativeTextureViewInfo | None
    normal_texture: NativeTextureViewInfo | None
    occlusion_texture: NativeTextureViewInfo | None
    emissive_texture: NativeTextureViewInfo | None
    emissive_factor: tuple[float, float, float]
    alpha_mode: int
    alpha_cutoff: float
    double_sided: bool
    unlit: bool
    ior: float
    specular_factor: float
    specular_color_factor: tuple[float, float, float]


class NativeGLBDocument:
    """Mapped GLB document with compact discovery and transactional mesh build."""

    def __init__(self, path: Path | str):
        self._path = Path(path)
        self._document = _glb_native.NativeDocument(str(self._path))
        self._meshes = tuple(
            NativeMeshInfo(
                name=entry["name"],
                primitive_count=entry["primitive_count"],
                vertex_count=entry["vertex_count"],
                index_count=entry["index_count"],
                skinned=entry["skinned"],
            )
            for entry in self._document.meshes
        )
        self._images = tuple(
            NativeImageInfo(
                index=index,
                name=entry["name"],
                has_name=entry["has_name"],
                mime_type=entry["mime_type"],
                uri=entry["uri"],
                embedded=entry["embedded"],
                encoded_size=entry["encoded_size"],
            )
            for index, entry in enumerate(self._document.images)
        )
        self._textures = tuple(
            NativeTextureInfo(
                index=index,
                name=entry["name"],
                has_name=entry["has_name"],
                image_index=entry["image_index"],
                sampler_index=(entry["sampler_index"] if entry["has_sampler"] else None),
                selected_webp=entry["selected_webp"],
                selected_basisu=entry["selected_basisu"],
                mag_filter=entry["mag_filter"],
                min_filter=entry["min_filter"],
                wrap_s=entry["wrap_s"],
                wrap_t=entry["wrap_t"],
            )
            for index, entry in enumerate(self._document.textures)
        )
        self._materials = tuple(
            self._material_info(entry) for entry in self._document.materials
        )
        self._rig_snapshot: dict | None = None

    @property
    def path(self) -> Path:
        return self._path

    @property
    def meshes(self) -> tuple[NativeMeshInfo, ...]:
        return self._meshes

    @property
    def images(self) -> tuple[NativeImageInfo, ...]:
        return self._images

    @property
    def textures(self) -> tuple[NativeTextureInfo, ...]:
        return self._textures

    @property
    def materials(self) -> tuple[NativeMaterialInfo, ...]:
        return self._materials

    def image_payload(self, image_index: int) -> bytes:
        """Copy one encoded embedded image from the mapped GLB on demand."""
        return self._document.image_payload(image_index)

    def rig_data(self) -> dict:
        """Materialize one bulk rig snapshot without per-keyframe binding calls.

        The snapshot keeps node/skin/animation semantics exact, including
        interpolation, target paths, and vec3 scale values. It is a migration
        and differential-test boundary; runtime publication will consume the
        same flat payload through native resource builders.
        """
        if self._rig_snapshot is None:
            self._rig_snapshot = self._document.rig_data()
        return self._rig_snapshot

    def build_animation_clip(
        self,
        animation_index: int,
        animation_uuid: str,
        *,
        name: str = "",
        convert_to_z_up: bool = False,
    ):
        """Publish one animation as exact bulk tracks.

        This is the runtime bridge for the native discovery format: all keys
        cross the Python/native boundary in one ``set_tracks`` call.  STEP and
        non-uniform vec3 scale remain exact. CUBICSPLINE and morph weights are
        retained by TcAnimationClip but are deliberately rejected by the
        player until their runtime lowering exists.
        """
        import numpy as np

        from termin.animation import TcAnimationClip

        rig = self.rig_data()
        animations = rig["animations"]
        if animation_index < 0 or animation_index >= len(animations):
            raise IndexError(
                f"animation index {animation_index} is out of range for {self._path}"
            )

        animation = animations[animation_index]
        times = np.frombuffer(rig["times"], dtype=np.float32)
        values = np.frombuffer(rig["values"], dtype=np.float32)
        path_names = {1: "translation", 2: "rotation", 3: "scale", 4: "weights"}
        interpolation_names = {0: "linear", 1: "step", 2: "cubic_spline"}
        tracks = []
        seen_targets: set[tuple[int, str]] = set()
        for channel_index, channel in enumerate(animation["channels"]):
            target_node_index = channel["target_node_index"]
            if target_node_index is None:
                raise RuntimeError(
                    f"{self._path}: animation {animation_index} channel "
                    f"{channel_index} has no target node"
                )
            path = path_names.get(channel["target_path"])
            if path is None:
                raise RuntimeError(
                    f"{self._path}: animation {animation_index} channel "
                    f"{channel_index} has unsupported target path {channel['target_path']}"
                )
            identity = (target_node_index, path)
            if identity in seen_targets:
                raise RuntimeError(
                    f"{self._path}: animation {animation_index} has duplicate "
                    f"{path} tracks for node {target_node_index}"
                )
            seen_targets.add(identity)

            sampler = animation["samplers"][channel["sampler_index"]]
            interpolation = interpolation_names.get(sampler["interpolation"])
            if interpolation is None:
                raise RuntimeError(
                    f"{self._path}: animation {animation_index} sampler has "
                    f"unsupported interpolation {sampler['interpolation']}"
                )
            time_first = sampler["time_first"]
            value_first = sampler["value_first"]
            track_times = times[
                time_first : time_first + sampler["time_count"]
            ].astype(np.float64)
            track_values = values[
                value_first : value_first + sampler["value_count"]
            ].astype(np.float64)

            components = sampler["components"]
            if path == "weights":
                multiplier = 3 if interpolation == "cubic_spline" else 1
                denominator = sampler["time_count"] * multiplier
                if denominator == 0 or sampler["value_count"] % denominator:
                    raise RuntimeError(
                        f"{self._path}: animation {animation_index} channel "
                        f"{channel_index} has malformed morph-weight output"
                    )
                components = sampler["value_count"] // denominator
            expected_components = {"translation": 3, "rotation": 4, "scale": 3}
            if path in expected_components and components != expected_components[path]:
                raise RuntimeError(
                    f"{self._path}: animation {animation_index} channel "
                    f"{channel_index} has {components} components for {path}"
                )

            if convert_to_z_up and path != "weights":
                vectors = track_values.reshape(-1, components)
                if path in ("translation", "rotation"):
                    old_y = vectors[:, 1].copy()
                    vectors[:, 1] = -vectors[:, 2]
                    vectors[:, 2] = old_y
                elif path == "scale":
                    old_y = vectors[:, 1].copy()
                    vectors[:, 1] = vectors[:, 2]
                    vectors[:, 2] = old_y

            tracks.append(
                {
                    "target_node_index": target_node_index,
                    "path": path,
                    "interpolation": interpolation,
                    "components": components,
                    "times": track_times,
                    "values": track_values,
                }
            )

        clip_name = name or animation["name"]
        clip = TcAnimationClip.create(clip_name, animation_uuid)
        clip.set_tps(1.0)
        clip.set_loop(True)
        clip.set_tracks(tracks)
        return clip

    def build_material_texture_data(self):
        """Bridge native metadata to the existing Python resource-layer records."""
        import numpy as np

        from termin.glb.loader import GLBMaterialData, GLBTcTexture

        payloads: dict[int, bytes] = {}
        textures = []
        for texture in self._textures:
            image = self._images[texture.image_index]
            payload = payloads.get(image.index)
            if payload is None:
                payload = self.image_payload(image.index)
                payloads[image.index] = payload
            mime_type = image.mime_type
            if not mime_type:
                if texture.selected_webp:
                    mime_type = "image/webp"
                elif texture.selected_basisu:
                    mime_type = "image/ktx2"
                else:
                    mime_type = "image/png"
            textures.append(
                GLBTcTexture(
                    index=texture.index,
                    name=image.name if image.has_name else f"Texture_{texture.index}",
                    data=payload,
                    mime_type=mime_type,
                    image_index=image.index,
                    sampler_index=texture.sampler_index,
                    sampler={
                        "magFilter": texture.mag_filter,
                        "minFilter": texture.min_filter,
                        "wrapS": texture.wrap_s,
                        "wrapT": texture.wrap_t,
                    },
                )
            )

        materials = []
        for material in self._materials:
            views = (
                material.base_color_texture,
                material.metallic_roughness_texture,
                material.normal_texture,
                material.occlusion_texture,
                material.emissive_texture,
            )
            if any(view is not None and view.has_transform for view in views):
                raise RuntimeError(
                    f"{self._path}: material '{material.name}' uses "
                    "KHR_texture_transform, which is not supported by the native adapter"
                )
            materials.append(
                GLBMaterialData(
                    name=material.name,
                    base_color=np.asarray(material.base_color_factor, dtype=np.float32),
                    base_color_texture=self._view_index(material.base_color_texture),
                    base_color_texcoord=self._view_texcoord(material.base_color_texture),
                    metallic_factor=material.metallic_factor,
                    roughness_factor=material.roughness_factor,
                    metallic_roughness_texture=self._view_index(
                        material.metallic_roughness_texture
                    ),
                    metallic_roughness_texcoord=self._view_texcoord(
                        material.metallic_roughness_texture
                    ),
                    normal_texture=self._view_index(material.normal_texture),
                    normal_texcoord=self._view_texcoord(material.normal_texture),
                    normal_scale=self._view_scale(material.normal_texture, 1.0),
                    occlusion_texture=self._view_index(material.occlusion_texture),
                    occlusion_texcoord=self._view_texcoord(material.occlusion_texture),
                    occlusion_strength=self._view_scale(material.occlusion_texture, 1.0),
                    emissive_texture=self._view_index(material.emissive_texture),
                    emissive_texcoord=self._view_texcoord(material.emissive_texture),
                    emissive_factor=np.asarray(material.emissive_factor, dtype=np.float32),
                    alpha_mode=material.alpha_mode,
                    alpha_cutoff=material.alpha_cutoff,
                    double_sided=material.double_sided,
                    unlit=material.unlit,
                    ior=material.ior,
                    specular_factor=material.specular_factor,
                    specular_color_factor=np.asarray(
                        material.specular_color_factor, dtype=np.float32
                    ),
                )
            )
        return tuple(materials), tuple(textures)

    def build_mesh(
        self,
        mesh_index: int,
        mesh_uuid: str,
        *,
        name: str = "",
        convert_to_z_up: bool = True,
    ):
        """Build one discovered mesh and return its existing native handle."""
        self._document.build_mesh(
            mesh_index, mesh_uuid, name, convert_to_z_up, False
        )
        return self._mesh_handle(mesh_uuid)

    def build_mesh_with_diagnostics(
        self,
        mesh_index: int,
        mesh_uuid: str,
        *,
        name: str = "",
        convert_to_z_up: bool = True,
    ):
        """Build one mesh and return its handle plus native hash/size oracle."""
        values = self._document.build_mesh(
            mesh_index, mesh_uuid, name, convert_to_z_up, True
        )

        return self._mesh_handle(mesh_uuid), NativeMeshBuildDiagnostics(
            payload_bytes=values["payload_bytes"],
            payload_hash=values["payload_hash"],
        )

    @staticmethod
    def _mesh_handle(mesh_uuid: str):
        from tmesh import tc_mesh_get

        mesh = tc_mesh_get(mesh_uuid)
        if mesh is None or not mesh.is_valid:
            raise RuntimeError(
                f"Native GLB build published no tc_mesh for UUID '{mesh_uuid}'"
            )
        return mesh

    @staticmethod
    def _texture_view(entry) -> NativeTextureViewInfo | None:
        if not entry["present"]:
            return None
        return NativeTextureViewInfo(
            texture_index=entry["texture_index"],
            texcoord=entry["texcoord"],
            scale=entry["scale"],
            has_transform=entry["has_transform"],
        )

    @classmethod
    def _material_info(cls, entry) -> NativeMaterialInfo:
        return NativeMaterialInfo(
            name=entry["name"],
            base_color_factor=tuple(entry["base_color_factor"]),
            metallic_factor=entry["metallic_factor"],
            roughness_factor=entry["roughness_factor"],
            base_color_texture=cls._texture_view(entry["base_color_texture"]),
            metallic_roughness_texture=cls._texture_view(
                entry["metallic_roughness_texture"]
            ),
            normal_texture=cls._texture_view(entry["normal_texture"]),
            occlusion_texture=cls._texture_view(entry["occlusion_texture"]),
            emissive_texture=cls._texture_view(entry["emissive_texture"]),
            emissive_factor=tuple(entry["emissive_factor"]),
            alpha_mode=entry["alpha_mode"],
            alpha_cutoff=entry["alpha_cutoff"],
            double_sided=entry["double_sided"],
            unlit=entry["unlit"],
            ior=entry["ior"],
            specular_factor=entry["specular_factor"],
            specular_color_factor=tuple(entry["specular_color_factor"]),
        )

    @staticmethod
    def _view_index(view: NativeTextureViewInfo | None) -> int | None:
        return None if view is None else view.texture_index

    @staticmethod
    def _view_texcoord(view: NativeTextureViewInfo | None) -> int:
        return 0 if view is None else view.texcoord

    @staticmethod
    def _view_scale(view: NativeTextureViewInfo | None, default: float) -> float:
        return default if view is None else view.scale


# Compatibility name for the stage-1 static-mesh API.
NativeStaticMeshDocument = NativeGLBDocument
