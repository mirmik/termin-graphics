import os
from pathlib import Path

from termin import shader_tools


def _executable_name(name: str) -> str:
    return f"{name}.exe" if os.name == "nt" else name


def _write_tool(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/bin/sh\n", encoding="utf-8")
    path.chmod(0o755)


def test_shader_tools_resolves_sdk_windows_exe_suffix(monkeypatch, tmp_path: Path) -> None:
    sdk = tmp_path / "sdk"
    tool = sdk / "bin" / _executable_name("termin_shaderc")
    _write_tool(tool)

    monkeypatch.setenv("TERMIN_SDK", str(sdk))

    assert shader_tools.existing_executable(sdk / "bin" / "termin_shaderc") == tool
    assert shader_tools.resolve_sdk_tool("termin_shaderc", Path(__file__)) == tool
