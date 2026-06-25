import re
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def test_builtin_vertex_outputs_use_native_clip_helper() -> None:
    source_root = _repo_root() / "termin-graphics" / "resources" / "builtin_shaders"
    shader_paths = sorted(source_root.glob("*.slang")) + sorted(source_root.glob("*.shader"))
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
                    location = f"{path.relative_to(source_root)}:{line_base + offset}"
                    failures.append(f"{location}: {line.strip()}")

    assert not failures, (
        "SV_Position assignments must use termin_to_native_clip:\n"
        + "\n".join(failures)
    )
