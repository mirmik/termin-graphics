#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    source_dir = _DIR


setup(
    name="termin-animation",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Animation clip Python bindings (thin; requires termin SDK at runtime)",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.animation", "termin.animation_components", "termin_animation_component_specs"],
    package_dir={
        "termin.animation": "python/termin/animation",
        "termin.animation_components": "python/termin/animation_components",
        "termin_animation_component_specs": "python/termin_animation_component_specs",
    },
    install_requires=[
        "termin-nanobind",
        "tcbase",
        "termin-assets",
    ],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
