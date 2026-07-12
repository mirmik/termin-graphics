import importlib.util
import json
from pathlib import Path


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _catalog_module():
    path = (
        _repo_root()
        / "termin-graphics"
        / "cmake"
        / "compile_builtin_shader_artifacts.py"
    )
    spec = importlib.util.spec_from_file_location("compile_builtin_shader_artifacts", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _source_root() -> Path:
    return _repo_root() / "termin-graphics" / "resources" / "builtin_shaders"


def test_builtin_shader_manifest_is_complete_and_resolves_every_source() -> None:
    source_root = _source_root()
    catalog = json.loads((source_root / "engine-shader-catalog.json").read_text())

    assert _catalog_module().validate_catalog(catalog, source_root) == []
    assert len(catalog["shaders"]) == 58


def test_builtin_shader_manifest_rejects_duplicate_uuid_and_missing_source(tmp_path: Path) -> None:
    catalog = {
        "shaders": [
            {
                "uuid": "duplicate",
                "name": "First",
                "language": "slang",
                "stages": {"vertex": {"path": "missing.slang", "entry": "vs_main"}},
            },
            {
                "uuid": "duplicate",
                "name": "Second",
                "language": "slang",
                "stages": {"fragment": {"path": "also-missing.slang", "entry": ""}},
            },
        ]
    }

    errors = _catalog_module().validate_catalog(catalog, tmp_path)

    assert "duplicate shader uuid: duplicate" in errors
    assert any("source does not exist" in error for error in errors)
    assert any("has no entry" in error for error in errors)
