#!/usr/bin/env python3

from setuptools import setup


setup(
    name="termin-graphics-mcp",
    version="0.1.0",
    license="MIT",
    description="Graphics-owned MCP adapters for Termin render consumers",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=["termin.graphics.mcp"],
    package_dir={"termin.graphics.mcp": "termin/graphics/mcp"},
    install_requires=[
        "termin-mcp",
        "termin-image",
        "tgfx",
        "numpy",
    ],
    zip_safe=False,
)
