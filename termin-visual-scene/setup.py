#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
import os


class BuildExt(TerminCMakeBuildExt):
    source_dir = os.path.dirname(os.path.realpath(__file__))


setup(
    name="termin-visual-scene",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Retained 2D visual scene core",
    python_requires=">=3.14",
    packages=["termin.visual_scene"],
    package_dir={"termin.visual_scene": "python/termin/visual_scene"},
    install_requires=["tgfx"],
    ext_modules=[],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
