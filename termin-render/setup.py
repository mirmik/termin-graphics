#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_render_framework_native", "_render_native"]
    upstream_packages = {
        "tcbase": "libtermin_base",
        "termin_nanobind": "libnanobind",
        "tmesh": "libtermin_mesh",
        "tgfx": "libtermin_graphics",
        "termin.inspect": "libtermin_inspect",
        "termin.scene": "libtermin_scene",
    }
    bundle_includes = True
    source_dir = _DIR


setup(
    name="termin-render",
    version="0.1.0",
    license="MIT",
    description="Rendering framework with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.render", "termin.render_framework"],
    package_dir={
        "termin.render": "python/termin/render",
        "termin.render_framework": "python/termin/render_framework",
    },
    install_requires=["tcbase", "tgfx", "termin-inspect", "termin-scene", "termin-nanobind"],
    package_data={
        "termin.render": [
            "include/**/*.h",
            "include/**/*.hpp",
            "lib/*.so*",
            "lib/cmake/**/*",
            "*.dll",
            "lib/*.dll",
            "lib/*.lib",
        ],
    },
    ext_modules=[
        Extension("termin.render_framework._render_framework_native", sources=[]),
        Extension("termin.render._render_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
