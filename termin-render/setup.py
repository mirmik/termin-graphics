#!/usr/bin/env python3

from setuptools import setup
from termin_build.cmake_ext import TerminCMakeBuild, TerminCMakeBuildExt
from termin_build.setup_helpers import native_extensions_for_source

import os
_DIR = os.path.dirname(os.path.realpath(__file__))


class BuildExt(TerminCMakeBuildExt):
    source_dir = _DIR


setup(
    name="termin-render",
    version=BuildExt.compute_local_version("0.1.0"),
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
    install_requires=[
        "termin-nanobind",
        "termin-assets",
        "tcbase",
        "tgfx",
        "termin-materials",
        "termin-scene",
        "termin-inspect",
        "numpy",
        "Pillow>=9.0",
    ],
    ext_modules=native_extensions_for_source(_DIR),
    cmdclass={"build": TerminCMakeBuild, "build_ext": BuildExt},
    entry_points={
        "termin.asset_import_plugins": [
            "glsl = termin.render.glsl_plugin:GlslImportPlugin",
            "material = termin.render.material_plugin:MaterialImportPlugin",
            "pipeline = termin.render.pipeline_plugin:PipelineImportPlugin",
            "scene_pipeline = termin.render.scene_pipeline_plugin:ScenePipelineImportPlugin",
            "shader = termin.render.shader_plugin:ShaderImportPlugin",
            "texture = termin.render.texture_plugin:TextureImportPlugin",
        ],
        "termin.asset_runtime_plugins": [
            "glsl = termin.render.glsl_plugin:GlslRuntimePlugin",
            "material = termin.render.material_plugin:MaterialRuntimePlugin",
            "pipeline = termin.render.pipeline_plugin:PipelineRuntimePlugin",
            "scene_pipeline = termin.render.scene_pipeline_plugin:ScenePipelineRuntimePlugin",
            "shader = termin.render.shader_plugin:ShaderRuntimePlugin",
            "texture = termin.render.texture_plugin:TextureRuntimePlugin",
        ],
    },
    zip_safe=False,
)
