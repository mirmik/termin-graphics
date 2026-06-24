#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    source_dir = _DIR


setup(
    name="termin-render",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Rendering framework Python bindings (thin; requires termin SDK at runtime)",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.render", "termin.render_framework", "termin_render_framework_specs"],
    package_dir={
        "termin.render": "python/termin/render",
        "termin.render_framework": "python/termin/render_framework",
        "termin_render_framework_specs": "python/termin_render_framework_specs",
    },
    install_requires=[
        "termin-nanobind",
        "tcbase",
        "tgfx",
        "termin-materials",
        "termin-scene",
        "termin-inspect",
        "numpy",
        "Pillow>=9.0",
    ],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
