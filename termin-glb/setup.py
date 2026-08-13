#!/usr/bin/env python3

from setuptools import find_namespace_packages, setup


setup(
    name="termin-glb",
    version="0.1.0",
    license="MIT",
    description="Portable GLB/glTF decoding and runtime publication",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.14",
    packages=find_namespace_packages(where="python", include=["termin.glb", "termin.glb.*"]),
    package_dir={"": "python"},
    install_requires=[
        "termin-glb-native",
        "tcbase",
        "tmesh",
        "termin-skeleton",
        "termin-animation",
        "termin-nanobind",
        "numpy",
    ],
    zip_safe=False,
)
