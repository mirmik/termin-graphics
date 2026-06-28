#!/usr/bin/env python3

from setuptools import setup


setup(
    name="termin-shader-runtime",
    version="0.1.0",
    license="MIT",
    description="Shared shader tool resolution and source-project runtime helpers",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.10",
    py_modules=[
        "termin.shader_runtime",
        "termin.shader_tools",
    ],
    package_dir={"": "."},
    install_requires=[
        "tcbase",
        "termin-materials",
        "tgfx",
    ],
    zip_safe=False,
)
