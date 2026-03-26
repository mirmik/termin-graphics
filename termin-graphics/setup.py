#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt


import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_tgfx_native"]
    upstream_packages = {"tcbase": "libtermin_base", "tmesh": "libtermin_mesh", "termin_nanobind": "libnanobind"}
    bundle_includes = True
    source_dir = _DIR


setup(
    name="tgfx",
    version="0.1.0",
    license="MIT",
    description="Graphics backend library with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tgfx"],
    package_dir={"tgfx": "python/tgfx"},
    install_requires=["tcbase", "tmesh", "termin-nanobind", "numpy"],
    package_data={
        "tgfx": [
            "lib/*.so*",
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
        ],
    },
    ext_modules=[
        Extension("tgfx._tgfx_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
