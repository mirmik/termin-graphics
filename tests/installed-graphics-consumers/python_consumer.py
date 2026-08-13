#!/usr/bin/env python3
"""Exercise the installed Graphics Python closure without a source overlay."""

from __future__ import annotations

import argparse
import base64
import json
from pathlib import Path
import struct
import subprocess
import uuid

import numpy as np


def _aligned(parts: list[bytes], payload: bytes) -> tuple[int, int]:
    size = sum(len(part) for part in parts)
    padding = (-size) % 4
    if padding:
        parts.append(b"\0" * padding)
        size += padding
    parts.append(payload)
    return size, len(payload)


def write_animated_skinned_glb(path: Path) -> None:
    chunks: list[bytes] = []
    views: list[dict[str, int]] = []

    def view(payload: bytes, target: int | None = None) -> int:
        offset, length = _aligned(chunks, payload)
        entry = {"buffer": 0, "byteOffset": offset, "byteLength": length}
        if target is not None:
            entry["target"] = target
        views.append(entry)
        return len(views) - 1

    positions = view(struct.pack("<9f", -0.7, 0, 0, 0.7, 0, 0, 0, 1.2, 0), 34962)
    joints = view(bytes((0, 1, 0, 0) * 3), 34962)
    weights = view(struct.pack("<12f", *(0.5, 0.5, 0, 0) * 3), 34962)
    indices = view(struct.pack("<3H", 0, 1, 2), 34963)
    inverse_bind = view(struct.pack("<32f", *([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1] * 2)))
    times = view(struct.pack("<2f", 0, 1))
    translations = view(struct.pack("<6f", 0, 1, 0, 0.35, 1, 0))
    binary = b"".join(chunks)
    document = {
        "asset": {"version": "2.0", "generator": "Termin installed Graphics smoke"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": [
            {"name": "SkinnedTriangle", "mesh": 0, "skin": 0},
            {"name": "Root", "children": [2]},
            {"name": "Tip", "translation": [0, 1, 0]},
        ],
        "meshes": [{"name": "Triangle", "primitives": [{"attributes": {
            "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}, "indices": 3}]}],
        "skins": [{"name": "Armature", "joints": [1, 2], "skeleton": 1,
                   "inverseBindMatrices": 4}],
        "animations": [{"name": "Wave", "samplers": [{"input": 5, "output": 6,
            "interpolation": "LINEAR"}], "channels": [{"sampler": 0,
            "target": {"node": 2, "path": "translation"}}]}],
        "accessors": [
            {"bufferView": positions, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": joints, "componentType": 5121, "count": 3, "type": "VEC4"},
            {"bufferView": weights, "componentType": 5126, "count": 3, "type": "VEC4"},
            {"bufferView": indices, "componentType": 5123, "count": 3, "type": "SCALAR"},
            {"bufferView": inverse_bind, "componentType": 5126, "count": 2, "type": "MAT4"},
            {"bufferView": times, "componentType": 5126, "count": 2, "type": "SCALAR",
             "min": [0], "max": [1]},
            {"bufferView": translations, "componentType": 5126, "count": 2, "type": "VEC3"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(binary)}],
    }
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * ((-len(encoded)) % 4)
    binary += b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    path.write_bytes(
        struct.pack("<4sII", b"glTF", 2, total)
        + struct.pack("<I4s", len(encoded), b"JSON") + encoded
        + struct.pack("<I4s", len(binary), b"BIN\0") + binary
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--slangc", type=Path, required=True)
    args = parser.parse_args()
    args.work_root.mkdir(parents=True, exist_ok=True)
    glb_path = args.work_root / "animated-skinned-triangle.glb"
    write_animated_skinned_glb(glb_path)

    from termin.animation import clip_from_glb
    from termin.glb import load_glb_file
    from termin.graphics.mcp import capture_texture_screenshot
    from termin.materials import parse_shader_text
    from termin.skeleton import TcSkeleton

    scene = load_glb_file(glb_path)
    assert len(scene.meshes) == len(scene.skins) == len(scene.animations) == 1
    assert scene.meshes[0].is_skinned
    skeleton = TcSkeleton.create("InstalledGLB", str(uuid.uuid4()))
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    skeleton.set_bones([
        {"name": "Root", "parent_index": -1, "inverse_bind_matrix": identity,
         "bind_translation": (0, 0, 0), "bind_rotation": (0, 0, 0, 1), "bind_scale": (1, 1, 1)},
        {"name": "Tip", "parent_index": 0, "inverse_bind_matrix": identity,
         "bind_translation": (0, 1, 0), "bind_rotation": (0, 0, 0, 1), "bind_scale": (1, 1, 1)},
    ])
    clip = clip_from_glb(scene.animations[0], str(uuid.uuid4()))
    assert skeleton.bone_count == 2 and clip.duration == 1.0

    material_source = "\n".join(("@program installed", "@language slang",
        "@property Float roughness = 0.5 range(0, 1)", "@phase opaque",
        "@stage vertex vs_main", "float4 vs_main(float3 p : POSITION) : SV_Position { return float4(p, 1); }",
        "@endstage", "@endphase"))
    parsed = parse_shader_text(material_source)
    assert parsed.program == "installed" and parsed.material_properties[0].name == "roughness"
    shader = args.work_root / "installed.slang"
    shader.write_text("float4 vs_main(float3 p : POSITION) : SV_Position { return float4(p, 1); }\n")
    artifact = args.work_root / "installed.vert.spv"
    subprocess.run([str(args.sdk_root / "bin" / "termin_shaderc"), "compile",
        "--language", "slang", "--target", "vulkan", "--stage", "vertex",
        "--entry", "vs_main", "--input", str(shader), "--output", str(artifact),
        "--slangc", str(args.slangc)], check=True)
    assert artifact.is_file() and artifact.with_suffix(artifact.suffix + ".layout.json").is_file()

    class Device:
        def read_texture_rgba_float(self, _texture, output):
            np.asarray(output).reshape((-1, 4))[:] = (0.2, 0.4, 0.8, 1.0)
            return True

    shot = capture_texture_screenshot(object(), Device(), width=2, height=2,
        output_path=str(args.work_root / "mcp-readback"), include_image=True,
        default_dir=args.work_root, default_prefix="unused", log_prefix="InstalledGraphics")
    assert base64.b64decode(shot["base64"]).startswith(b"\x89PNG")
    print(json.dumps({"glb": str(glb_path), "bones": skeleton.bone_count,
                      "animation_duration": clip.duration, "shader": str(artifact),
                      "mcp_png": shot["path"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
