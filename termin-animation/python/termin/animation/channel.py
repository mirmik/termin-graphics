# Animation channel helpers for creating TcAnimationClip from FBX/GLB data
from __future__ import annotations

import math

from termin.geombase import Pose3, Quat, Vec3


def _vec3(value) -> Vec3:
    return Vec3(float(value[0]), float(value[1]), float(value[2]))


def _quat(value) -> Quat:
    return Quat(float(value[0]), float(value[1]), float(value[2]), float(value[3]))


def _mean3(value) -> float:
    return (float(value[0]) + float(value[1]) + float(value[2])) / 3.0


def channel_data_from_fbx(ch) -> dict:
    """
    Create channel data dict from FBXAnimationChannel.

    Args:
        ch: FBXAnimationChannel with pos_keys, rot_keys, scale_keys in ticks
            rot_keys contain Euler angles (in degrees) which are converted to quaternions

    Returns:
        dict with target_name, translation_keys, rotation_keys, scale_keys
    """
    tr_keys = []
    for (t, v) in ch.pos_keys:
        tr_keys.append((t, _vec3(v)))

    rot_keys = []
    for (t, v) in ch.rot_keys:
        rad = (
            math.radians(float(v[0])),
            math.radians(float(v[1])),
            math.radians(float(v[2])),
        )
        pose = Pose3.from_euler(rad[0], rad[1], rad[2], order='xyz')
        rot_keys.append((t, pose.ang))

    sc_keys = [(t, _mean3(v)) for (t, v) in ch.scale_keys]

    return {
        "target_name": ch.node_name,
        "translation_keys": tr_keys,
        "rotation_keys": rot_keys,
        "scale_keys": sc_keys,
    }


def channel_data_from_glb(ch) -> dict:
    """
    Create channel data dict from GLBAnimationChannel.

    Args:
        ch: GLBAnimationChannel with pos_keys, rot_keys, scale_keys
            Time is in seconds, quaternions in XYZW format

    Returns:
        dict with target_name, translation_keys, rotation_keys, scale_keys
    """
    tr_keys = [(t, _vec3(v)) for (t, v) in ch.pos_keys]
    rot_keys = [(t, _quat(v)) for (t, v) in ch.rot_keys]
    sc_keys = [(t, _mean3(v)) for (t, v) in ch.scale_keys]

    return {
        "target_name": ch.node_name,
        "translation_keys": tr_keys,
        "rotation_keys": rot_keys,
        "scale_keys": sc_keys,
    }


__all__ = ["channel_data_from_fbx", "channel_data_from_glb"]
