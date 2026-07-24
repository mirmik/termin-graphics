#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    upstream_packages = {"termin_nanobind": "libnanobind"}
    bundle_includes = True
    source_dir = _DIR


setup(
    name="termin-image",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Native image codecs for Termin texture and tooling paths",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=["termin.image"],
    package_dir={"termin.image": "python/termin/image"},
    install_requires=[
        "termin-nanobind",
        "numpy",
    ],
    package_data={
        "termin.image": [
            "include/**/*.h",
            "include/**/*.hpp",
            "lib/*.so*",
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
            "lib/cmake/termin_image/*.cmake",
        ],
    },
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
