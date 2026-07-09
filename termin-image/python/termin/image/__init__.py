from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_image")

from termin.image._image_native import decode_rgba8 as _decode_rgba8_native
from termin.image._image_native import encode_png_rgba8 as _encode_png_rgba8_native


@dataclass(frozen=True)
class DecodedImage:
    width: int
    height: int
    channels: int
    format: str
    data: bytes

    def to_numpy(self, *, copy: bool = True) -> np.ndarray:
        array = np.frombuffer(self.data, dtype=np.uint8).reshape((self.height, self.width, self.channels))
        if copy:
            return np.ascontiguousarray(array.copy())
        return array


def decode_rgba8(content: bytes | bytearray | memoryview, source_hint: str = "") -> DecodedImage:
    result = _decode_rgba8_native(content, source_hint)
    return DecodedImage(
        width=int(result["width"]),
        height=int(result["height"]),
        channels=int(result["channels"]),
        format=str(result["format"]),
        data=bytes(result["data"]),
    )


def decode_rgba8_file(path: str | Path) -> DecodedImage:
    image_path = Path(path)
    return decode_rgba8(image_path.read_bytes(), str(image_path))


def _rgba8_bytes(data: Any) -> tuple[bytes, int, int]:
    array = np.asarray(data, dtype=np.uint8)
    if array.ndim != 3 or array.shape[2] != 4:
        raise ValueError("PNG encode expects an array shaped (height, width, 4)")
    contiguous = np.ascontiguousarray(array)
    height, width = contiguous.shape[:2]
    return contiguous.tobytes(), int(width), int(height)


def encode_png_rgba8(data: Any) -> bytes:
    raw, width, height = _rgba8_bytes(data)
    return bytes(_encode_png_rgba8_native(raw, width, height))


def write_png_rgba8_file(path: str | Path, data: Any) -> None:
    Path(path).write_bytes(encode_png_rgba8(data))
