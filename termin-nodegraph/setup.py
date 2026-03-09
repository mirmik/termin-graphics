#!/usr/bin/env python3

from setuptools import setup, find_packages


setup(
    name="termin-nodegraph",
    version="0.1.0",
    license="MIT",
    description="Abstract node graph engine and tcgui adapter",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    install_requires=[
        "tcbase",
        "tgfx",
        "tcgui",
    ],
    zip_safe=False,
)
