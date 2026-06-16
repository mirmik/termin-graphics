import importlib.util
import os
from pathlib import Path
import sys
import types


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _load_shader_runtime_module():
    sys.modules["tcbase"] = types.SimpleNamespace(
        log=types.SimpleNamespace(error=lambda message: None, info=lambda message: None)
    )
    path = _repo_root() / "termin-graphics" / "python" / "tgfx" / "shader_runtime.py"
    spec = importlib.util.spec_from_file_location("tgfx_shader_runtime_under_test", path)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _executable_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def test_tgfx_shader_runtime_resolves_sdk_windows_exe_suffix(
    monkeypatch,
    tmp_path: Path,
) -> None:
    sdk = tmp_path / "sdk"
    tool = sdk / "bin" / _executable_name("termin_shaderc")
    tool.parent.mkdir(parents=True)
    tool.write_text("#!/bin/sh\n", encoding="utf-8")
    tool.chmod(0o755)

    monkeypatch.setenv("TERMIN_SDK", str(sdk))
    monkeypatch.setenv("PATH", "")

    shader_runtime = _load_shader_runtime_module()

    assert shader_runtime._resolve_tool("termin_shaderc", "TERMIN_SHADERC") == tool


def test_tgfx_shader_runtime_uses_local_app_data_cache_root_on_windows(
    monkeypatch,
    tmp_path: Path,
) -> None:
    if os.name != "nt":
        return

    local_app_data = tmp_path / "LocalAppData"
    monkeypatch.setenv("LOCALAPPDATA", str(local_app_data))
    monkeypatch.delenv("TERMIN_SDK_SHADER_CACHE_ROOT", raising=False)
    monkeypatch.delenv("XDG_CACHE_HOME", raising=False)

    shader_runtime = _load_shader_runtime_module()

    assert (
        shader_runtime._cache_root("python")
        == local_app_data / "Termin" / "Cache" / "python-shaders"
    )
