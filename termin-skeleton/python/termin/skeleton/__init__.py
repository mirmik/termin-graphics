"""Skeleton module for skeletal animation support."""

from termin_nanobind.runtime import preload_sdk_libs

preload_sdk_libs("termin_skeleton")

from termin.skeleton._skeleton_native import (
    TcSkeleton,
    SkeletonInstance,
    tc_skeleton_get_all_info,
)


__all__ = [
    "TcSkeleton",
    "SkeletonInstance",
    "tc_skeleton_get_all_info",
]
