# Animation clip helpers
from __future__ import annotations

from ._animation_native import TcAnimationClip
from .channel import channel_data_from_fbx, channel_data_from_glb


def clip_from_fbx(fbx_clip, uuid_hint: str = "") -> TcAnimationClip:
    """
    Create TcAnimationClip from FBXAnimationClip.

    Args:
        fbx_clip: FBXAnimationClip from fbx_loader
        uuid_hint: Optional UUID for the clip

    Returns:
        TcAnimationClip
    """
    channels_data = []
    for ch in fbx_clip.channels:
        channels_data.append(channel_data_from_fbx(ch))

    clip = TcAnimationClip.create(fbx_clip.name, uuid_hint)
    clip.set_tps(fbx_clip.ticks_per_second or 30.0)
    clip.set_loop(True)
    clip.set_channels(channels_data)

    return clip


def clip_from_glb(glb_clip, uuid_hint: str = "") -> TcAnimationClip:
    """
    Create TcAnimationClip from GLBAnimationClip.

    Args:
        glb_clip: GLBAnimationClip from glb_loader
        uuid_hint: Optional UUID for the clip

    Returns:
        TcAnimationClip
    """
    channels_data = []
    for ch in glb_clip.channels:
        channels_data.append(channel_data_from_glb(ch))

    clip = TcAnimationClip.create(glb_clip.name, uuid_hint)
    clip.set_tps(1.0)  # GLB uses seconds directly
    clip.set_loop(True)
    clip.set_channels(channels_data)

    return clip


__all__ = ["TcAnimationClip", "clip_from_fbx", "clip_from_glb"]
