"""Animation clip primitives (thin wrapper over _animation_native)."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_animation")

from termin.animation._animation_native import TcAnimationClip  # noqa: F401
from termin.animation.channel import channel_data_from_fbx, channel_data_from_glb
from termin.animation.clip import clip_from_fbx, clip_from_glb
from termin.animation.clip_io import load_animation_clip, save_animation_clip


def __getattr__(name: str):
    if name == "AnimationClipAsset":
        from termin.animation.asset import AnimationClipAsset

        globals()["AnimationClipAsset"] = AnimationClipAsset
        return AnimationClipAsset
    raise AttributeError(f"module 'termin.animation' has no attribute {name!r}")


__all__ = [
    "TcAnimationClip",
    "AnimationClipAsset",
    "clip_from_fbx",
    "clip_from_glb",
    "channel_data_from_fbx",
    "channel_data_from_glb",
    "save_animation_clip",
    "load_animation_clip",
]
