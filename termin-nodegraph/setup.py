#!/usr/bin/env python3

import os

from setuptools import setup, find_packages
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source


_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    upstream_packages = {"tcbase": "libtermin_base", "termin_nanobind": "libnanobind"}
    source_dir = _DIR


setup(
    name="termin-nodegraph",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Abstract node graph engine and native UI projection",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    install_requires=[
        "tcbase",
        "termin-nanobind",
        "termin-gui-native",
        "termin-visual-scene",
    ],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
