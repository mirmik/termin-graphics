#!/usr/bin/env python3

from setuptools import setup, Extension
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    module_names = ["_render_framework_native", "_render_native"]
    source_dir = _DIR


setup(
    name="termin-render",
    version="0.1.0",
    license="MIT",
    description="Rendering framework Python bindings (thin; requires termin SDK at runtime)",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["termin.render", "termin.render_framework"],
    package_dir={
        "termin.render": "python/termin/render",
        "termin.render_framework": "python/termin/render_framework",
    },
    install_requires=["termin-nanobind"],
    ext_modules=[
        Extension("termin.render_framework._render_framework_native", sources=[]),
        Extension("termin.render._render_native", sources=[]),
    ],
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    zip_safe=False,
)
