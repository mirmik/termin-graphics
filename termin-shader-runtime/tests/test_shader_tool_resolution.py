import os
from pathlib import Path

from tcbase import Settings
from termin import shader_runtime, shader_tools


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


def test_resolve_slangc_reads_common_termin_setting(monkeypatch, tmp_path: Path) -> None:
    config_home = tmp_path / "config"
    slangc = tmp_path / _executable_name("slangc")
    _write_tool(slangc)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config_home))
    monkeypatch.setenv("APPDATA", str(config_home))
    monkeypatch.delenv("TERMIN_SLANGC", raising=False)
    Settings("termin").set("Shader/slangCompiler", str(slangc))

    assert shader_runtime.resolve_slangc(Path(__file__)) == slangc


def test_environment_slangc_overrides_common_setting(monkeypatch, tmp_path: Path) -> None:
    config_home = tmp_path / "config"
    environment_slangc = tmp_path / _executable_name("environment-slangc")
    _write_tool(environment_slangc)
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config_home))
    monkeypatch.setenv("APPDATA", str(config_home))
    monkeypatch.setenv("TERMIN_SLANGC", str(environment_slangc))
    Settings("termin").set("Shader/slangCompiler", str(tmp_path / "missing-slangc"))

    assert shader_runtime.resolve_slangc(Path(__file__)) == environment_slangc


def test_invalid_common_slang_setting_is_authoritative(monkeypatch, tmp_path: Path) -> None:
    config_home = tmp_path / "config"
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config_home))
    monkeypatch.setenv("APPDATA", str(config_home))
    monkeypatch.delenv("TERMIN_SLANGC", raising=False)
    Settings("termin").set(
        "Shader/slangCompiler", str(tmp_path / "missing-slangc")
    )

    assert shader_runtime.resolve_slangc(Path(__file__)) is None


def test_missing_slangc_message_is_actionable_for_editor(monkeypatch, tmp_path: Path) -> None:
    config_home = tmp_path / "config"
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config_home))
    monkeypatch.setenv("APPDATA", str(config_home))
    monkeypatch.delenv("TERMIN_SLANGC", raising=False)

    message = shader_runtime.slangc_unavailable_message(
        "Editor project",
        editor=True,
    )

    assert "slangc was not found" in message
    assert "Edit > Settings... > Slang Compiler" in message
    assert "Shader/slangCompiler" in message
    assert "TERMIN_SLANGC" in message


def test_invalid_slangc_setting_message_names_configured_path(
    monkeypatch,
    tmp_path: Path,
) -> None:
    config_home = tmp_path / "config"
    missing = tmp_path / "missing-slangc"
    monkeypatch.setenv("XDG_CONFIG_HOME", str(config_home))
    monkeypatch.setenv("APPDATA", str(config_home))
    monkeypatch.delenv("TERMIN_SLANGC", raising=False)
    Settings("termin").set("Shader/slangCompiler", str(missing))

    message = shader_runtime.slangc_unavailable_message("Source player")

    assert "Shader/slangCompiler points to a missing or non-executable slangc" in message
    assert str(missing) in message
