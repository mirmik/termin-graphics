from pathlib import Path

from termin.animation import AnimationClipAsset as PackageAnimationClipAsset
from termin.animation.asset import AnimationClipAsset
from termin.animation.animation_clip_asset import AnimationClipAsset as LegacyAnimationClipAsset


class FakeAnimationClip:
    name = "walk"
    duration = 1.25


def test_animation_clip_asset_wraps_clip() -> None:
    clip = FakeAnimationClip()

    asset = AnimationClipAsset.from_clip(clip, source_path="/tmp/walk.anim")

    assert asset.name == "walk"
    assert asset.clip is clip
    assert asset.duration == 1.25
    assert asset.source_path == Path("/tmp/walk.anim")


def test_animation_clip_asset_domain_legacy_modules_reexport_canonical_class() -> None:
    assert PackageAnimationClipAsset is AnimationClipAsset
    assert LegacyAnimationClipAsset is AnimationClipAsset
