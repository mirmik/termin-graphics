#!/usr/bin/env python3

import os

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source


_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    source_dir = _DIR


setup(
    name="termin-window",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Framework-neutral native window ownership and Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=["termin.window"],
    package_dir={"termin.window": "python/termin/window"},
    install_requires=["termin-nanobind", "tcbase", "tgfx"],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
