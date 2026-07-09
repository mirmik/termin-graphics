import pytest

from termin.materials import GlslPreprocessor


def test_glsl_preprocessor_resolves_only_registered_includes() -> None:
    pp = GlslPreprocessor()
    pp.register_include("common.glsl", "vec4 tint(){ return vec4(1.0); }\n")

    processed = pp.preprocess('#include "common.glsl"\nvoid main(){}\n', "test.glsl")

    assert "BEGIN INCLUDE: common.glsl" in processed
    assert "vec4 tint()" in processed


def test_glsl_preprocessor_missing_include_is_hard_error() -> None:
    pp = GlslPreprocessor()

    with pytest.raises(RuntimeError, match="GLSL include not found"):
        pp.preprocess('#include "missing.glsl"\n', "test.glsl")


def test_glsl_preprocessor_has_no_python_fallback_loader_api() -> None:
    pp = GlslPreprocessor()

    assert not hasattr(pp, "set_fallback_loader")
