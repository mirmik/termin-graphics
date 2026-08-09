"""GLB/glTF importer, asset, and runtime instantiation support."""

from termin.glb.asset import GLBAsset
from termin.glb.asset_plugin import GLBAssetPlugin, GLBImportPlugin, GLBRuntimePlugin
from termin.glb.loader import (
    GLBAnimationChannel,
    GLBAnimationClip,
    GLBMaterialData,
    GLBMeshData,
    GLBNodeData,
    GLBSceneData,
    GLBSkinData,
    GLBSubmeshData,
    GLBTcTexture,
    load_glb_file,
    load_glb_file_from_buffer,
    load_glb_file_normalized,
)
from termin.glb.native import (
    NativeImageInfo,
    NativeGLBDocument,
    NativeMaterialInfo,
    NativeMeshBuildDiagnostics,
    NativeMeshInfo,
    NativeStaticMeshDocument,
    NativeTextureInfo,
    NativeTextureViewInfo,
)

__all__ = [
    "GLBAnimationChannel",
    "GLBAnimationClip",
    "GLBAsset",
    "GLBAssetPlugin",
    "GLBImportPlugin",
    "GLBMaterialData",
    "GLBMeshData",
    "GLBNodeData",
    "GLBRuntimePlugin",
    "GLBSceneData",
    "GLBSkinData",
    "GLBSubmeshData",
    "GLBTcTexture",
    "NativeMeshInfo",
    "NativeMeshBuildDiagnostics",
    "NativeImageInfo",
    "NativeGLBDocument",
    "NativeMaterialInfo",
    "NativeStaticMeshDocument",
    "NativeTextureInfo",
    "NativeTextureViewInfo",
    "load_glb_file",
    "load_glb_file_from_buffer",
    "load_glb_file_normalized",
]
