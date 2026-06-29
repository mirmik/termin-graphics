"""Test that animation modules are accessible via canonical paths."""

import importlib

import pytest


def test_animation_native_via_canonical_path():
    """_animation_native should be importable via termin.animation."""
    mod = importlib.import_module("termin.animation._animation_native")
    assert hasattr(mod, "TcAnimationClip")


def test_components_animation_native_via_canonical_path():
    """_components_animation_native is shipped in the termin.animation namespace."""
    try:
        mod = importlib.import_module("termin.animation._components_animation_native")
        assert hasattr(mod, "AnimationPlayer")
    except ImportError as e:
        if "libtermin_skeleton" in str(e) or "initializing the extension" in str(e):
            pytest.skip("native skeleton libs not available in test environment")
        raise


def test_canonical_clip_import():
    """termin.animation.clip should import TcAnimationClip from canonical path."""
    from termin.animation.clip import TcAnimationClip
    assert TcAnimationClip is not None


def test_canonical_clip_io_import():
    """termin.animation.clip_io should work with canonical native imports."""
    from termin.animation.clip_io import load_animation_clip, save_animation_clip
    assert load_animation_clip is not None
    assert save_animation_clip is not None


def test_canonical_animation_components_import():
    """termin.animation_components should import AnimationPlayer from canonical path."""
    try:
        from termin.animation_components import AnimationPlayer
        assert AnimationPlayer is not None
    except ImportError as e:
        if "libtermin_skeleton" in str(e) or "initializing the extension" in str(e):
            pytest.skip("native skeleton libs not available in test environment")
        raise


def test_canonical_animation_clip_handle_import():
    """TcAnimationClip should be available through the canonical native module."""
    from termin.animation._animation_native import TcAnimationClip

    assert TcAnimationClip is not None
