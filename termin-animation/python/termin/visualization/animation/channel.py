# Animation channel helpers for creating TcAnimationClip from FBX/GLB data
from __future__ import annotations

import numpy as np


def channel_data_from_fbx(ch) -> dict:
    """
    Create channel data dict from FBXAnimationChannel.

    Args:
        ch: FBXAnimationChannel with pos_keys, rot_keys, scale_keys in ticks
            rot_keys contain Euler angles (in degrees) which are converted to quaternions

    Returns:
        dict with target_name, translation_keys, rotation_keys, scale_keys
    """
    from termin.geombase import Pose3

    tr_keys = []
    for (t, v) in ch.pos_keys:
        tr_keys.append((t, np.array(v, dtype=np.float64)))

    rot_keys = []
    for (t, v) in ch.rot_keys:
        rad = np.radians(v)
        pose = Pose3.from_euler(rad[0], rad[1], rad[2], order='xyz')
        rot_keys.append((t, pose.ang))

    sc_keys = [(t, float(np.mean(v))) for (t, v) in ch.scale_keys]

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
    tr_keys = [(t, np.array(v, dtype=np.float64)) for (t, v) in ch.pos_keys]
    rot_keys = [(t, np.array(v, dtype=np.float64)) for (t, v) in ch.rot_keys]
    sc_keys = [(t, float(np.mean(v))) for (t, v) in ch.scale_keys]

    return {
        "target_name": ch.node_name,
        "translation_keys": tr_keys,
        "rotation_keys": rot_keys,
        "scale_keys": sc_keys,
    }


__all__ = ["channel_data_from_fbx", "channel_data_from_glb"]
