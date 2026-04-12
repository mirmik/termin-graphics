"""Animation clip primitives (thin wrapper over _animation_native)."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_animation")

from termin.animation._animation_native import TcAnimationClip  # noqa: F401
from termin.animation.channel import channel_data_from_fbx, channel_data_from_glb
from termin.animation.clip import clip_from_fbx, clip_from_glb
from termin.animation.clip_io import load_animation_clip, save_animation_clip

__all__ = [
    "TcAnimationClip",
    "clip_from_fbx",
    "clip_from_glb",
    "channel_data_from_fbx",
    "channel_data_from_glb",
    "save_animation_clip",
    "load_animation_clip",
]
