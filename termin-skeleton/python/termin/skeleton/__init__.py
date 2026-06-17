"""Skeleton module for skeletal animation support."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_skeleton")

from termin.skeleton._skeleton_native import (
    TcSkeleton,
    SkeletonInstance,
)


def __getattr__(name: str):
    if name == "SkeletonAsset":
        from termin.skeleton.asset import SkeletonAsset

        globals()["SkeletonAsset"] = SkeletonAsset
        return SkeletonAsset
    raise AttributeError(f"module 'termin.skeleton' has no attribute {name!r}")


__all__ = [
    "TcSkeleton",
    "SkeletonInstance",
    "SkeletonAsset",
]
