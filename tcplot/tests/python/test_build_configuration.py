from pathlib import Path
import re


TCPLOT_ROOT = Path(__file__).resolve().parents[2]


def test_all_native_binding_sources_participate_in_content_invalidation() -> None:
    cmake_text = (TCPLOT_ROOT / "python" / "CMakeLists.txt").read_text(
        encoding="utf-8"
    )
    source_list_match = re.search(
        r"set\(TCPLOT_PYTHON_BINDING_SOURCES\s+(.*?)\s*\)",
        cmake_text,
        re.DOTALL,
    )
    assert source_list_match is not None

    configured_sources = set(
        re.findall(r"bindings/[A-Za-z0-9_]+\.cpp", source_list_match.group(1))
    )
    binding_sources = {
        path.relative_to(TCPLOT_ROOT / "python").as_posix()
        for path in (TCPLOT_ROOT / "python" / "bindings").glob("*.cpp")
    }

    assert configured_sources == binding_sources
    assert "file(SHA256" in cmake_text
    assert "OBJECT_DEPENDS" in cmake_text
    assert "nanobind_add_module(_tcplot_native NB_SHARED\n    ${TCPLOT_PYTHON_BINDING_SOURCES}" in cmake_text
