#!/usr/bin/env python3

from setuptools import find_namespace_packages, setup


setup(
    name="termin-glb",
    version="0.1.0",
    license="MIT",
    description="GLB/glTF importer, asset, and runtime instantiation support for Termin",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.10",
    packages=find_namespace_packages(where="python", include=["termin.glb", "termin.glb.*"]),
    package_dir={"": "python"},
    install_requires=[
        "tcbase",
        "termin-assets",
        "termin-scene",
        "termin-default-assets",
        "termin-components-render",
        "tmesh",
        "termin-graphics",
        "termin-materials",
        "termin-skeleton",
        "termin-animation",
        "numpy",
        "Pillow>=9.0",
    ],
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
