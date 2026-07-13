import re
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def test_builtin_vertex_outputs_use_native_clip_helper() -> None:
    source_roots = [
        _repo_root() / "termin-graphics" / "resources" / "builtin_shaders",
        _repo_root() / "termin-stdlib" / "python" / "termin" / "stdlib" / "resources" / "slang",
    ]
    shader_paths = [
        path
        for source_root in source_roots
        for pattern in ("*.slang", "*.shader")
        for path in sorted(source_root.glob(pattern))
    ]
    assignment_re = re.compile(r"\b(?:output|o)\.(?:position|pos)\s*=")
    vertex_attr_re = re.compile(r'\[shader\("vertex"\)\]')
    stage_attr_re = re.compile(r'\[shader\("(?:vertex|fragment|geometry|compute)"\)\]')
    shader_stage_re = re.compile(r"^@stage\s+\w+", re.MULTILINE)

    failures: list[str] = []
    for path in shader_paths:
        text = path.read_text(encoding="utf-8")
        for vertex_match in vertex_attr_re.finditer(text):
            stage_start = vertex_match.start()
            next_stage_attr = stage_attr_re.search(text, vertex_match.end())
            next_shader_stage = shader_stage_re.search(text, vertex_match.end())
            stage_end_candidates = [
                match.start()
                for match in (next_stage_attr, next_shader_stage)
                if match is not None
            ]
            stage_end = min(stage_end_candidates) if stage_end_candidates else len(text)
            stage_text = text[stage_start:stage_end]
            if "SV_Position" not in stage_text:
                continue

            line_base = text[:stage_start].count("\n") + 1
            for offset, line in enumerate(stage_text.splitlines()):
                if assignment_re.search(line) and "termin_to_native_clip" not in line:
                    location = f"{path.relative_to(_repo_root())}:{line_base + offset}"
                    failures.append(f"{location}: {line.strip()}")

    assert not failures, (
        "SV_Position assignments must use termin_to_native_clip:\n"
        + "\n".join(failures)
    )


def test_ground_grid_uses_zero_to_one_depth_contract() -> None:
    path = (
        _repo_root()
        / "termin-graphics"
        / "resources"
        / "builtin_shaders"
        / "termin-engine-ground-grid.slang"
    )
    text = path.read_text(encoding="utf-8")

    assert "return clip.z / clip.w;" in text
    assert "(clip.z / clip.w) * 0.5 + 0.5" not in text
    assert "(GridParams.u_near * GridParams.u_far)" in text
    assert "GridParams.u_far - ndc_depth * (GridParams.u_far - GridParams.u_near)" in text


def test_opengl_clip_control_is_validated_and_centralized() -> None:
    root = _repo_root() / "termin-graphics" / "src" / "tgfx2" / "opengl"
    device_source = (root / "opengl_render_device.cpp").read_text(encoding="utf-8")
    command_source = (root / "opengl_command_list.cpp").read_text(encoding="utf-8")

    assert "if (s_glClipControl) s_glClipControl" not in device_source
    assert "OpenGL backend requires OpenGL 4.5 or ARB_clip_control" in device_source
    assert "glGetIntegerv(GL_CLIP_ORIGIN, &origin)" in device_source
    assert "glGetIntegerv(GL_CLIP_DEPTH_MODE, &depth_mode)" in device_source
    assert "throw std::runtime_error" in device_source

    stale_drain = device_source.index("for (GLenum stale_error = glGetError()")
    clip_control_call = device_source.index(
        "s_glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE)"
    )
    apply_error_check = device_source.index(
        "const GLenum apply_error = glGetError()"
    )
    assert stale_drain < clip_control_call < apply_error_check
    assert "pre-existing GL error" in device_source

    assert "wglGetProcAddress" not in command_source
    assert "device_.enforce_clip_space_contract();" in command_source
