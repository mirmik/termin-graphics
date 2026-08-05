from pathlib import Path

from shaderc_test_helpers import _run_shaderc

def test_termin_shaderc_rejects_glsl_opengl_until_generation_exists(tmp_path: Path) -> None:
    shader = tmp_path / "test.glsl"
    shader.write_text("#version 450\nvoid main() {}\n", encoding="utf-8")

    result = _run_shaderc([
        "compile",
        "--language",
        "glsl",
        "--target",
        "opengl",
        "--stage",
        "vertex",
        "--input",
        str(shader),
        "--output",
        str(tmp_path / "out.glsl"),
    ])

    assert result.returncode == 1
    assert "GLSL input currently supports only --target vulkan" in result.stderr
