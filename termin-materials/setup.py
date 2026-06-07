#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    upstream_packages = {"tcbase": "libtermin_base", "tgfx": "libtermin_graphics", "termin_nanobind": "libnanobind"}
    source_dir = _DIR


setup(
    name="termin-materials",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Termin material and shader-format runtime",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.materials"],
    package_dir={"termin.materials": "python/termin/materials"},
    install_requires=["tcbase", "tgfx", "termin-nanobind"],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
