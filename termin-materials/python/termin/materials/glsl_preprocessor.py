from . import (
    GlslPreprocessor,
    glsl_preprocessor,
    register_glsl_preprocessor,
)


class GlslPreprocessorError(Exception):
    """Error during GLSL preprocessing."""


def preprocess_glsl(source: str, source_name: str = "<unknown>") -> str:
    return glsl_preprocessor().preprocess(source, source_name)


def has_includes(source: str) -> bool:
    return GlslPreprocessor.has_includes(source)


__all__ = [
    "GlslPreprocessor",
    "GlslPreprocessorError",
    "glsl_preprocessor",
    "has_includes",
    "preprocess_glsl",
    "register_glsl_preprocessor",
]
