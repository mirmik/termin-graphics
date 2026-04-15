#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_skeleton_native", "_components_skeleton_native"]
    source_dir = _DIR


setup(
    name="termin-skeleton",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Skeleton Python bindings (thin; requires termin SDK at runtime)",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.skeleton"],
    package_dir={"termin.skeleton": "python/termin/skeleton"},
    install_requires=["termin-nanobind"],
    ext_modules=[
        Extension("termin.skeleton._skeleton_native", sources=[]),
        Extension("termin.skeleton._components_skeleton_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
