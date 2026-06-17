#!/usr/bin/env python3

from setuptools import find_namespace_packages, setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source


import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
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
    packages=[
        "tmesh",
        *find_namespace_packages(
            where="python",
            include=["termin.mesh", "termin.mesh.*"],
        ),
    ],
    package_dir={
        "": "python",
    },
    install_requires=[
        "tcbase",
        "termin-assets",
        "termin-nanobind",
        "numpy",
    ],
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
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    entry_points={
        "termin.asset_import_plugins": [
            "mesh = termin.mesh.asset_plugin:create_import_plugin",
        ],
        "termin.asset_runtime_plugins": [
            "mesh = termin.mesh.asset_plugin:create_runtime_plugin",
        ],
    },
    zip_safe=False,
)
