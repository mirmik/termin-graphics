#!/usr/bin/env python3
import os

from setuptools import setup, Extension

from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt

_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_tcplot_native"]
    upstream_packages = {
        "tcbase": "libtermin_base",
        "tmesh": "libtermin_mesh",
        "tgfx": "libtermin_graphics2",
        "termin_nanobind": "libnanobind",
    }
    source_dir = _DIR


setup(
    name="tcplot",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Lightweight plotting library on top of tgfx2 / tcgui",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tcplot"],
    package_dir={"tcplot": "python/tcplot"},
    install_requires=[
        "tcbase",
        "tmesh",
        "tgfx",
        "tcgui",
        "termin-nanobind",
        "numpy",
    ],
    package_data={
        "tcplot": [
            "*.dll",
            "lib/*.dll",
            "lib/*.so*",
            "lib/*.lib",
        ],
    },
    ext_modules=[
        Extension("tcplot._tcplot_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
