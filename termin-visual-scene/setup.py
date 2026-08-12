#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source
import os

_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    upstream_packages = {
        "tcbase": "libtermin_base",
        "tgfx": "libtermin_graphics2",
        "tmesh": "libtermin_mesh",
        "termin_nanobind": "libnanobind",
    }
    source_dir = _DIR


setup(
    name="termin-visual-scene",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Retained 2D and 3D visual scene core",
    python_requires=">=3.14",
    packages=["termin.visual_scene"],
    package_dir={"termin.visual_scene": "python/termin/visual_scene"},
    install_requires=[
        "tcbase",
        "tgfx",
        "tmesh",
        "termin-nanobind",
    ],
    package_data={
        "termin.visual_scene": [
            "*.dll",
            "lib/*.dll",
            "lib/*.so*",
            "lib/*.lib",
        ],
    },
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
