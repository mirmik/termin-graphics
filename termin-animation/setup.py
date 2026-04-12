#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_animation_native", "_components_animation_native"]
    source_dir = _DIR


setup(
    name="termin-animation",
    version="0.1.0",
    license="MIT",
    description="Animation clip Python bindings (thin; requires termin SDK at runtime)",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.animation"],
    package_dir={"termin.animation": "python/termin/animation"},
    install_requires=["termin-nanobind"],
    ext_modules=[
        Extension("termin.animation._animation_native", sources=[]),
        Extension("termin.animation._components_animation_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
