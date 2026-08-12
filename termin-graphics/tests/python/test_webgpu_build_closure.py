from pathlib import Path
import re


REPO_ROOT = Path(__file__).resolve().parents[3]
GRAPHICS_CMAKE = REPO_ROOT / "termin-graphics" / "CMakeLists.txt"
LINE_RENDERER_COMMON = (
    REPO_ROOT / "termin-graphics" / "src" / "tgfx2" / "line_renderer_common.cpp"
)


def _emscripten_tgfx2_sources() -> set[str]:
    cmake = GRAPHICS_CMAKE.read_text(encoding="utf-8")
    match = re.search(
        r"if\(EMSCRIPTEN\)\s+.*?set\(TGFX2_SOURCES\s+(.*?)\s+\)\s+endif\(\)",
        cmake,
        flags=re.DOTALL,
    )
    assert match is not None, "Emscripten TGFX2 source closure is missing"
    return set(match.group(1).split())


def test_emscripten_tgfx2_closure_contains_high_level_line_renderers() -> None:
    assert {
        "src/tgfx2/line_renderer_common.cpp",
        "src/tgfx2/screen_space_line_renderer.cpp",
        "src/tgfx2/world_space_line_renderer.cpp",
        "src/tgfx2/world_tube_line_renderer.cpp",
    } <= _emscripten_tgfx2_sources()


def test_static_line_vertex_buffers_are_uploadable() -> None:
    source = LINE_RENDERER_COMMON.read_text(encoding="utf-8")
    function = re.search(
        r"BufferHandle create_static_vertex_buffer\(.*?\n    \}",
        source,
        flags=re.DOTALL,
    )
    assert function is not None
    assert "BufferUsage::Vertex | BufferUsage::CopyDst" in function.group(0)
