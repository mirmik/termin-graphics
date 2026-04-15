#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt


import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_tmesh_native"]
    upstream_packages = {"tcbase": "libtermin_base", "termin_nanobind": "libnanobind"}
    bundle_includes = True
    source_dir = _DIR


setup(
    name="tmesh",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="Mesh library with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tmesh"],
    package_dir={"tmesh": "python/tmesh"},
    install_requires=["tcbase", "termin-nanobind", "numpy"],
    package_data={
        "tmesh": [
            "include/**/*.h",
            "include/**/*.hpp",
            "lib/*.so*",
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
            "lib/cmake/termin_mesh/*.cmake",
        ],
    },
    ext_modules=[
        Extension("tmesh._tmesh_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
