#!/usr/bin/env python3

import os

from setuptools import find_namespace_packages, setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source


_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    source_dir = _DIR


setup(
    name="termin-glb",
    version=BuildExt.compute_local_version("0.1.0"),
    license="MIT",
    description="GLB/glTF importer, asset, and runtime instantiation support for Termin",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=find_namespace_packages(where="python", include=["termin.glb", "termin.glb.*"]),
    package_dir={"": "python"},
    install_requires=[
        "tcbase",
        "termin-assets",
        "termin-image",
        "termin-scene",
        "termin-default-assets",
        "termin-components-render",
        "tmesh",
        "tgfx",
        "termin-materials",
        "termin-skeleton",
        "termin-animation",
        "termin-nanobind",
        "numpy",
    ],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    entry_points={
        "termin.asset_import_plugins": [
            "glb = termin.glb.asset_plugin:create_import_plugin",
        ],
        "termin.asset_runtime_plugins": [
            "glb = termin.glb.asset_plugin:create_runtime_plugin",
        ],
    },
    zip_safe=False,
)
