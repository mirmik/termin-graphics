# termin/loaders/glb_extractor.py
"""GLB extractor - extracts meshes, textures and animations from GLB files."""

from __future__ import annotations

from pathlib import Path
from typing import List, Tuple

from tcbase import log
from termin.glb.loader import load_glb_file, GLBMeshData


def save_mesh_as_obj(mesh: GLBMeshData, output_path: Path) -> None:
    """Save mesh data as OBJ file."""
    lines = []
    lines.append(f"# Extracted from GLB: {mesh.name}")
    lines.append(f"# Vertices: {len(mesh.vertices)}")
    lines.append("")

    # Vertices
    for v in mesh.vertices:
        lines.append(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}")
    lines.append("")

    # UVs
    if mesh.uvs is not None and len(mesh.uvs) > 0:
        for uv in mesh.uvs:
            lines.append(f"vt {uv[0]:.6f} {uv[1]:.6f}")
        lines.append("")

    # Normals
    if mesh.normals is not None and len(mesh.normals) > 0:
        for n in mesh.normals:
            lines.append(f"vn {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}")
        lines.append("")

    # Faces
    num_triangles = len(mesh.vertices) // 3
    has_uvs = mesh.uvs is not None and len(mesh.uvs) > 0
    has_normals = mesh.normals is not None and len(mesh.normals) > 0

    for i in range(num_triangles):
        base = i * 3
        i1, i2, i3 = base + 1, base + 2, base + 3

        if has_uvs and has_normals:
            lines.append(f"f {i1}/{i1}/{i1} {i2}/{i2}/{i2} {i3}/{i3}/{i3}")
        elif has_uvs:
            lines.append(f"f {i1}/{i1} {i2}/{i2} {i3}/{i3}")
        elif has_normals:
            lines.append(f"f {i1}//{i1} {i2}//{i2} {i3}//{i3}")
        else:
            lines.append(f"f {i1} {i2} {i3}")

    output_path.write_text("\n".join(lines), encoding="utf-8")


def extract_glb(glb_path: Path, output_dir: Path = None) -> Tuple[Path, List[Path]]:
    """Extract meshes, textures and animations from GLB file.

    Args:
        glb_path: Path to the GLB file
        output_dir: Directory to extract to. If None, creates directory
                   next to GLB with same name.

    Returns:
        Tuple of (output_directory, list_of_created_files)
    """
    glb_path = Path(glb_path)

    if output_dir is None:
        output_dir = glb_path.parent / glb_path.stem

    output_dir.mkdir(parents=True, exist_ok=True)

    # Load GLB
    scene_data = load_glb_file(str(glb_path))

    created_files = []

    # Extract meshes
    for mesh in scene_data.meshes:
        safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in mesh.name)
        if not safe_name:
            safe_name = f"mesh_{len(created_files)}"

        obj_path = output_dir / f"{safe_name}.obj"
        save_mesh_as_obj(mesh, obj_path)
        created_files.append(obj_path)
        log.info(f"[GLB Extract] Saved mesh: {obj_path.name}")

    # Extract textures
    textures_dir = None
    for i, tex in enumerate(scene_data.textures):
        if textures_dir is None:
            textures_dir = output_dir / "textures"
            textures_dir.mkdir(exist_ok=True)

        # Determine extension from mime type
        ext = ".png" if "png" in tex.mime_type else ".jpg"
        safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in tex.name)
        if not safe_name:
            safe_name = f"texture_{i}"

        tex_path = textures_dir / f"{safe_name}{ext}"
        tex_path.write_bytes(tex.data)
        created_files.append(tex_path)
        log.info(f"[GLB Extract] Saved texture: {tex_path.name} ({len(tex.data)} bytes)")

    # Save material info
    if scene_data.materials:
        mat_info_path = output_dir / "_materials.txt"
        lines = ["# Materials from GLB", ""]
        for mat in scene_data.materials:
            lines.append(f"Material: {mat.name}")
            if mat.base_color is not None:
                c = mat.base_color
                lines.append(f"  Base Color: ({c[0]:.3f}, {c[1]:.3f}, {c[2]:.3f}, {c[3]:.3f})")
            if mat.base_color_texture is not None:
                lines.append(f"  Base Color Texture Index: {mat.base_color_texture}")
            if mat.metallic_roughness_texture is not None:
                lines.append(f"  Metallic/Roughness Texture Index: {mat.metallic_roughness_texture}")
            if mat.normal_texture is not None:
                lines.append(f"  Normal Texture Index: {mat.normal_texture}")
            if mat.occlusion_texture is not None:
                lines.append(f"  Occlusion Texture Index: {mat.occlusion_texture}")
            if mat.emissive_texture is not None:
                lines.append(f"  Emissive Texture Index: {mat.emissive_texture}")
            lines.append(f"  Metallic Factor: {mat.metallic_factor:.3f}")
            lines.append(f"  Roughness Factor: {mat.roughness_factor:.3f}")
            lines.append(f"  Normal Scale: {mat.normal_scale:.3f}")
            if mat.emissive_factor is not None:
                e = mat.emissive_factor
                lines.append(f"  Emissive Factor: ({e[0]:.3f}, {e[1]:.3f}, {e[2]:.3f})")
            lines.append("")
        mat_info_path.write_text("\n".join(lines), encoding="utf-8")
        created_files.append(mat_info_path)
        log.info(f"[GLB Extract] Saved materials info: {mat_info_path.name}")

    log.info(f"[GLB Extract] Extracted {len(scene_data.meshes)} meshes, {len(scene_data.textures)} textures to {output_dir}")

    # Extract animations
    anim_files = extract_animations(glb_path, output_dir, scene_data)
    created_files.extend(anim_files)

    return output_dir, created_files


def extract_animations(
    glb_path: Path,
    output_dir: Path = None,
    scene_data=None,
) -> List[Path]:
    """Extract animations from GLB file into .tanim files.

    Args:
        glb_path: Path to the GLB file
        output_dir: Directory to extract to
        scene_data: Pre-loaded GLBSceneData (optional)

    Returns:
        List of created animation files
    """
    from termin.animation import clip_from_glb, save_animation_clip

    glb_path = Path(glb_path)

    if output_dir is None:
        output_dir = glb_path.parent / glb_path.stem

    output_dir.mkdir(parents=True, exist_ok=True)

    if scene_data is None:
        scene_data = load_glb_file(str(glb_path))

    created_files = []

    for glb_anim in scene_data.animations:
        clip = clip_from_glb(glb_anim)

        safe_name = "".join(c if c.isalnum() or c in "-_" else "_" for c in clip.name)
        if not safe_name:
            safe_name = f"animation_{len(created_files)}"

        anim_path = output_dir / f"{safe_name}.tanim"
        save_animation_clip(clip, anim_path)
        created_files.append(anim_path)
        log.info(f"[GLB Extract] Saved animation: {anim_path.name} ({clip.duration:.2f}s, {len(clip.channels)} channels)")

    if created_files:
        log.info(f"[GLB Extract] Extracted {len(created_files)} animation(s)")

    return created_files
