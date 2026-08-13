"""Test that animation modules are accessible via canonical paths."""

import importlib

def test_animation_native_via_canonical_path():
    """_animation_native should be importable via termin.animation."""
    mod = importlib.import_module("termin.animation._animation_native")
    assert hasattr(mod, "TcAnimationClip")


def test_canonical_clip_import():
    """termin.animation.clip should import TcAnimationClip from canonical path."""
    from termin.animation.clip import TcAnimationClip
    assert TcAnimationClip is not None


def test_canonical_clip_io_import():
    """termin.animation.clip_io should work with canonical native imports."""
    from termin.animation.clip_io import load_animation_clip, save_animation_clip
    assert load_animation_clip is not None
    assert save_animation_clip is not None


def test_canonical_animation_clip_handle_import():
    """TcAnimationClip should be available through the canonical native module."""
    from termin.animation._animation_native import TcAnimationClip

    assert TcAnimationClip is not None
