"""GLBAsset - Asset for GLB 3D model files."""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Dict, Protocol

from tcbase import log
from termin_assets import DataAsset, get_resource_manager

if TYPE_CHECKING:
    from termin.glb.loader import GLBSceneData
    from termin.default_assets.mesh.asset import MeshAsset
    from termin.skeleton.asset import SkeletonAsset
    from termin.animation.asset import AnimationClipAsset


class GLBResourceManager(Protocol):
    """Resource-manager surface needed by GLB child asset registration."""

    def get_or_create_mesh_asset(self, name: str, source_path: str | None = None, uuid: str | None = None, parent=None, parent_key: str | None = None):
        ...

    def get_or_create_skeleton_asset(self, name: str, source_path: str | None = None, uuid: str | None = None, parent=None, parent_key: str | None = None):
        ...

    def get_or_create_animation_clip_asset(self, name: str, source_path: str | None = None, uuid: str | None = None, parent=None, parent_key: str | None = None):
        ...


class GLBAsset(DataAsset["GLBSceneData"]):
    """
    Asset for GLB model files.

    IMPORTANT: Create through ResourceManager, not directly.
    This ensures proper registration and avoids duplicates.

    GLBAsset is a container that holds:
    - GLBSceneData (raw loaded data)
    - Child MeshAssets for each mesh in the GLB
    - Child SkeletonAssets for each skin
    - Child AnimationClipAssets for each animation

    Child assets are requested from ResourceManager during spec parsing
    (via get_or_create_*_asset methods). GLBAsset never creates assets
    directly - only ResourceManager is allowed to create assets.
    """

    _uses_binary = True  # GLB is binary format

    def __init__(
        self,
        scene_data: "GLBSceneData | None" = None,
        name: str = "glb",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        super().__init__(data=scene_data, name=name, source_path=source_path, uuid=uuid)

        # Spec settings
        self._normalize_scale: bool = False
        self._convert_to_z_up: bool = True
        self._blender_z_up_fix: bool = False

        # Child assets (created during spec parsing)
        self._mesh_assets: Dict[str, "MeshAsset"] = {}
        self._skeleton_assets: Dict[str, "SkeletonAsset"] = {}
        self._animation_assets: Dict[str, "AnimationClipAsset"] = {}
        self._resource_manager: GLBResourceManager | None = None

    def set_resource_manager(self, resource_manager: GLBResourceManager) -> None:
        """Set the manager used to create/register GLB child assets."""
        self._resource_manager = resource_manager

    def _child_resource_manager(self) -> GLBResourceManager:
        if self._resource_manager is not None:
            return self._resource_manager
        resource_manager = get_resource_manager()
        if resource_manager is None:
            log.error(
                f"[GLBAsset] Resource manager is not configured; "
                f"cannot register child assets for '{self.name}'"
            )
            raise RuntimeError("Resource manager is not configured for GLB child assets")
        return resource_manager

    # --- Convenience property ---

    @property
    def scene_data(self) -> "GLBSceneData | None":
        """GLB scene data (lazy-loaded)."""
        return self.data

    # --- Spec parsing ---

    def _parse_spec_fields(self, spec_data: dict) -> None:
        """Parse GLB-specific spec fields and create child assets."""
        # Parse settings
        self._normalize_scale = spec_data.get("normalize_scale", False)
        self._convert_to_z_up = spec_data.get("convert_to_z_up", True)
        self._blender_z_up_fix = spec_data.get("blender_z_up_fix", False)

        # Create child assets from resources section
        resources = spec_data.get("resources", {})
        self._create_mesh_assets(resources.get("meshes", {}))
        self._create_skeleton_assets(resources.get("skeletons", {}))
        self._create_animation_assets(resources.get("animations", {}))

    def _create_mesh_assets(self, mesh_uuids: Dict[str, str]) -> None:
        """Get or create child MeshAssets with UUIDs from spec via ResourceManager.

        Also declares meshes in tc_mesh registry with lazy load callback.
        """
        from tmesh import (
            tc_mesh_declare,
            tc_mesh_set_load_callback,
            tc_mesh_is_loaded,
        )

        rm = self._child_resource_manager()
        for mesh_name, mesh_uuid in mesh_uuids.items():
            full_name = f"{self._name}_{mesh_name}"
            asset = rm.get_or_create_mesh_asset(
                name=full_name,
                source_path=str(self._source_path) if self._source_path else None,
                uuid=mesh_uuid,
                parent=self,
                parent_key=mesh_name,
            )
            self._mesh_assets[mesh_name] = asset

            # Declare mesh in tc_mesh registry if not already loaded
            tc_mesh = tc_mesh_declare(mesh_uuid, full_name)
            if tc_mesh.is_valid and not tc_mesh_is_loaded(tc_mesh):
                # Set load callback that will trigger GLBAsset loading
                tc_mesh_set_load_callback(tc_mesh, self._make_mesh_load_callback(mesh_name))
                # Store handle in MeshAsset
                asset._data = tc_mesh
                asset._loaded = False

    def _create_skeleton_assets(self, skeleton_uuids: Dict[str, str]) -> None:
        """Get or create child SkeletonAssets with UUIDs from spec via ResourceManager.

        Also declares skeletons in tc_skeleton registry with lazy load callback.
        """
        from termin.skeleton._skeleton_native import (
            tc_skeleton_declare,
            tc_skeleton_set_load_callback,
            tc_skeleton_is_loaded,
        )

        rm = self._child_resource_manager()
        for skeleton_key, skeleton_uuid in skeleton_uuids.items():
            # skeleton_key is "skeleton" or "skeleton_N"
            idx = 0 if skeleton_key == "skeleton" else int(skeleton_key.split("_")[1])
            skeleton_name = f"{self._name}_skeleton" if idx == 0 else f"{self._name}_skeleton_{idx}"

            asset = rm.get_or_create_skeleton_asset(
                name=skeleton_name,
                source_path=str(self._source_path) if self._source_path else None,
                uuid=skeleton_uuid,
                parent=self,
                parent_key=skeleton_key,
            )
            self._skeleton_assets[skeleton_key] = asset

            # Declare skeleton in tc_skeleton registry if not already loaded
            tc_skel = tc_skeleton_declare(skeleton_uuid, skeleton_name)
            if tc_skel.is_valid and not tc_skeleton_is_loaded(tc_skel):
                # Set load callback that will trigger GLBAsset loading
                tc_skeleton_set_load_callback(tc_skel, self._make_skeleton_load_callback(skeleton_key))
                # Store handle in SkeletonAsset
                asset._data = tc_skel
                asset._loaded = False

    def _create_animation_assets(self, animation_uuids: Dict[str, str]) -> None:
        """Get or create child AnimationClipAssets with UUIDs from spec via ResourceManager."""
        rm = self._child_resource_manager()
        for anim_name, anim_uuid in animation_uuids.items():
            asset = rm.get_or_create_animation_clip_asset(
                name=anim_name,
                source_path=str(self._source_path) if self._source_path else None,
                uuid=anim_uuid,
                parent=self,
                parent_key=anim_name,
            )
            self._animation_assets[anim_name] = asset

    def _build_spec_data(self) -> dict:
        """Build spec data with GLB settings and child UUIDs."""
        spec = super()._build_spec_data()

        # Settings (only non-defaults)
        if self._normalize_scale:
            spec["normalize_scale"] = True
        if not self._convert_to_z_up:
            spec["convert_to_z_up"] = False
        if self._blender_z_up_fix:
            spec["blender_z_up_fix"] = True

        # Child asset UUIDs
        resources: Dict[str, Dict[str, str]] = {}

        if self._mesh_assets:
            resources["meshes"] = {
                name: asset.uuid for name, asset in self._mesh_assets.items()
            }
        if self._skeleton_assets:
            resources["skeletons"] = {
                key: asset.uuid for key, asset in self._skeleton_assets.items()
            }
        if self._animation_assets:
            resources["animations"] = {
                name: asset.uuid for name, asset in self._animation_assets.items()
            }

        if resources:
            spec["resources"] = resources

        return spec

    # --- Content parsing ---

    def _parse_content(self, content: bytes) -> "GLBSceneData | None":
        """Parse GLB/glTF content."""
        from termin.glb.loader import load_glb_file_from_buffer, load_glb_file_normalized

        if self._source_path is not None:
            return load_glb_file_normalized(
                self._source_path,
                normalize_scale=self._normalize_scale,
                convert_to_z_up=self._convert_to_z_up,
                blender_z_up_fix=self._blender_z_up_fix,
            )

        return load_glb_file_from_buffer(
            content,
            normalize_scale=self._normalize_scale,
            convert_to_z_up=self._convert_to_z_up,
            blender_z_up_fix=self._blender_z_up_fix,
        )

    def _on_loaded(self) -> None:
        """After loading, create any missing child assets and populate all with data."""
        if self._data is None:
            return

        spec_changed = False

        # Create mesh assets for any meshes not in spec
        for glb_mesh in self._data.meshes:
            if glb_mesh.name not in self._mesh_assets:
                self._create_new_mesh_asset(glb_mesh.name)
                spec_changed = True

        # Create skeleton assets for any skins not in spec
        for i, _skin in enumerate(self._data.skins):
            skeleton_key = "skeleton" if i == 0 else f"skeleton_{i}"
            if skeleton_key not in self._skeleton_assets:
                self._create_new_skeleton_asset(skeleton_key, i)
                spec_changed = True

        # Create animation assets for any animations not in spec
        for glb_anim in self._data.animations:
            if glb_anim.name not in self._animation_assets:
                self._create_new_animation_asset(glb_anim.name)
                spec_changed = True

        # Populate all child assets with data
        self._populate_child_assets()

        # Save spec if new child assets were created
        if spec_changed and self._source_path:
            self.save_spec_file()

    def _make_mesh_load_callback(self, mesh_name: str):
        """Create a load callback that triggers GLBAsset loading for a specific mesh."""
        def load_callback(tc_mesh_data) -> bool:
            # Load the parent GLBAsset if not loaded
            self.ensure_loaded()

            # Find the GLB mesh data and populate tc_mesh
            if self._data is None:
                return False

            for glb_mesh in self._data.meshes:
                if glb_mesh.name == mesh_name:
                    from termin.glb.instantiator import _populate_tc_mesh_from_glb
                    from tmesh import TcMesh

                    tc_mesh = TcMesh.from_uuid(tc_mesh_data.uuid)
                    if tc_mesh.is_valid:
                        return _populate_tc_mesh_from_glb(tc_mesh, glb_mesh)

            return False

        return load_callback

    def _make_skeleton_load_callback(self, skeleton_key: str):
        """Create a load callback that triggers GLBAsset loading for a specific skeleton."""
        def load_callback(tc_skeleton_data) -> bool:
            # Load the parent GLBAsset if not loaded
            self.ensure_loaded()

            if self._data is None:
                return False

            # Parse skeleton_key to get index
            if skeleton_key == "skeleton":
                index = 0
            else:
                index = int(skeleton_key.split("_")[-1])

            if index < len(self._data.skins):
                from termin.glb.instantiator import _populate_tc_skeleton_from_glb
                from termin.skeleton import TcSkeleton

                tc_skel = TcSkeleton.from_uuid(tc_skeleton_data.uuid)
                if tc_skel.is_valid:
                    return _populate_tc_skeleton_from_glb(
                        tc_skel,
                        self._data.skins[index],
                        self._data.nodes,
                    )

            return False

        return load_callback

    def _populate_child_assets(self) -> None:
        """Fill all child assets with extracted data from loaded GLB."""
        from termin.glb.instantiator import (
            _glb_mesh_to_tc_mesh,
            _populate_tc_mesh_from_glb,
            _glb_skin_to_tc_skeleton,
            _populate_tc_skeleton_from_glb,
        )
        from tmesh import tc_mesh_is_loaded
        from termin.skeleton._skeleton_native import tc_skeleton_is_loaded
        from termin.animation import clip_from_glb

        # Populate mesh assets
        for mesh_name, asset in self._mesh_assets.items():
            for glb_mesh in self._data.meshes:
                if glb_mesh.name == mesh_name:
                    if asset._data is not None and asset._data.is_valid:
                        # Mesh was declared, populate existing entry
                        if not tc_mesh_is_loaded(asset._data):
                            _populate_tc_mesh_from_glb(asset._data, glb_mesh)
                    else:
                        # Create new mesh entry with asset's UUID
                        asset._data = _glb_mesh_to_tc_mesh(glb_mesh, asset._uuid)
                    asset._loaded = True
                    break

        # Populate skeleton assets
        for skeleton_key, asset in self._skeleton_assets.items():
            # Parse skeleton_key to get index
            if skeleton_key == "skeleton":
                index = 0
            else:
                index = int(skeleton_key.split("_")[-1])

            if index < len(self._data.skins):
                if asset._data is not None and asset._data.is_valid:
                    # Skeleton was declared, populate existing entry
                    if not tc_skeleton_is_loaded(asset._data):
                        _populate_tc_skeleton_from_glb(
                            asset._data,
                            self._data.skins[index],
                            self._data.nodes,
                        )
                else:
                    # Create new skeleton entry with asset's UUID
                    asset._data = _glb_skin_to_tc_skeleton(
                        self._data.skins[index],
                        self._data.nodes,
                        asset._uuid,
                    )
                asset._loaded = True

        # Populate animation assets
        for anim_name, asset in self._animation_assets.items():
            if asset._data is None:
                for glb_anim in self._data.animations:
                    if glb_anim.name == anim_name:
                        asset._data = clip_from_glb(glb_anim)
                        asset._loaded = True
                        break

    def _create_new_mesh_asset(self, mesh_name: str) -> "MeshAsset":
        """Get or create a MeshAsset for a mesh discovered during load via ResourceManager."""
        rm = self._child_resource_manager()
        full_name = f"{self._name}_{mesh_name}"
        asset = rm.get_or_create_mesh_asset(
            name=full_name,
            source_path=str(self._source_path) if self._source_path else None,
            uuid=None,  # Will auto-generate
            parent=self,
            parent_key=mesh_name,
        )
        self._mesh_assets[mesh_name] = asset
        return asset

    def _create_new_skeleton_asset(self, skeleton_key: str, index: int) -> "SkeletonAsset":
        """Get or create a SkeletonAsset for a skeleton discovered during load via ResourceManager."""
        rm = self._child_resource_manager()
        skeleton_name = f"{self._name}_skeleton" if index == 0 else f"{self._name}_skeleton_{index}"
        asset = rm.get_or_create_skeleton_asset(
            name=skeleton_name,
            source_path=str(self._source_path) if self._source_path else None,
            uuid=None,  # Will auto-generate
            parent=self,
            parent_key=skeleton_key,
        )
        self._skeleton_assets[skeleton_key] = asset
        return asset

    def _create_new_animation_asset(self, anim_name: str) -> "AnimationClipAsset":
        """Get or create an AnimationClipAsset for an animation discovered during load via ResourceManager."""
        rm = self._child_resource_manager()
        asset = rm.get_or_create_animation_clip_asset(
            name=anim_name,
            source_path=str(self._source_path) if self._source_path else None,
            uuid=None,  # Will auto-generate
            parent=self,
            parent_key=anim_name,
        )
        self._animation_assets[anim_name] = asset
        return asset

    # --- Child asset access ---

    def get_mesh_assets(self) -> Dict[str, "MeshAsset"]:
        """Get all child mesh assets."""
        return self._mesh_assets

    def get_skeleton_assets(self) -> Dict[str, "SkeletonAsset"]:
        """Get all child skeleton assets."""
        return self._skeleton_assets

    def get_animation_assets(self) -> Dict[str, "AnimationClipAsset"]:
        """Get all child animation clip assets."""
        return self._animation_assets

    # --- Content info methods ---

    def get_mesh_count(self) -> int:
        """Get number of meshes in the GLB."""
        if self._data is None:
            return len(self._mesh_assets)  # Return spec count if not loaded
        return len(self._data.meshes)

    def get_animation_count(self) -> int:
        """Get number of animations in the GLB."""
        if self._data is None:
            return len(self._animation_assets)
        return len(self._data.animations)

    def has_skeleton(self) -> bool:
        """Check if GLB has skeleton data."""
        if self._data is None:
            return len(self._skeleton_assets) > 0
        return len(self._data.skins) > 0

    def get_bone_count(self) -> int:
        """Get total bone count from all skins."""
        if self._data is None:
            return 0
        return sum(s.joint_count for s in self._data.skins)
