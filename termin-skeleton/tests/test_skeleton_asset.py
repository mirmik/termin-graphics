from termin.assets.skeleton_asset import SkeletonAsset as AppLegacySkeletonAsset
from termin.skeleton import SkeletonAsset as PackageSkeletonAsset
from termin.skeleton.asset import SkeletonAsset
from termin.skeleton.skeleton_asset import SkeletonAsset as LegacySkeletonAsset


class FakeSkeleton:
    is_valid = True
    bone_count = 7
    uuid = "skeleton-resource-uuid"


def test_skeleton_asset_wraps_tc_skeleton() -> None:
    skeleton = FakeSkeleton()

    asset = SkeletonAsset.from_tc_skeleton(
        skeleton,
        name="humanoid",
        source_path="/tmp/humanoid.skel",
        uuid="asset-uuid",
    )

    assert asset.name == "humanoid"
    assert asset.skeleton_data is skeleton
    assert asset.get_bone_count() == 7
    assert asset.serialize() == {
        "uuid": "asset-uuid",
        "name": "humanoid",
        "skeleton_uuid": "skeleton-resource-uuid",
    }


def test_skeleton_asset_legacy_modules_reexport_canonical_class() -> None:
    assert PackageSkeletonAsset is SkeletonAsset
    assert LegacySkeletonAsset is SkeletonAsset
    assert AppLegacySkeletonAsset is SkeletonAsset
