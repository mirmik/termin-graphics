"""Deterministic animated, skinned GLB used by the standalone showcase."""

from __future__ import annotations

import json
from pathlib import Path
import struct


def _append_aligned(parts: list[bytes], payload: bytes) -> tuple[int, int]:
    offset = sum(len(part) for part in parts)
    padding = (-offset) % 4
    if padding:
        parts.append(b"\0" * padding)
        offset += padding
    parts.append(payload)
    return offset, len(payload)


def write_animated_skinned_glb(path: Path) -> None:
    """Write a tiny mesh with two joints and a one-second translation track."""

    chunks: list[bytes] = []
    views: list[dict[str, int]] = []

    def view(payload: bytes, target: int | None = None) -> int:
        offset, length = _append_aligned(chunks, payload)
        item = {"buffer": 0, "byteOffset": offset, "byteLength": length}
        if target is not None:
            item["target"] = target
        views.append(item)
        return len(views) - 1

    positions = view(struct.pack("<9f", -0.7, 0, 0, 0.7, 0, 0, 0, 1.2, 0), 34962)
    joints = view(bytes((0, 1, 0, 0) * 3), 34962)
    weights = view(struct.pack("<12f", *(0.5, 0.5, 0, 0) * 3), 34962)
    indices = view(struct.pack("<3H", 0, 1, 2), 34963)
    identity = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    inverse_bind = view(struct.pack("<32f", *(identity * 2)))
    times = view(struct.pack("<2f", 0, 1))
    translations = view(struct.pack("<6f", 0, 1, 0, 0.35, 1, 0))
    binary = b"".join(chunks)
    document = {
        "asset": {"version": "2.0", "generator": "Termin Graphics showcase"},
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
