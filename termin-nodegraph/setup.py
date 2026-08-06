#!/usr/bin/env python3

from setuptools import setup, find_packages


setup(
    name="termin-nodegraph",
    version="0.1.0",
    license="MIT",
    description="Abstract node graph engine and native UI projection",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=find_packages(where="python"),
    package_dir={"": "python"},
    install_requires=[
        "termin-gui-native",
        "termin-visual-scene",
    ],
    zip_safe=False,
)
