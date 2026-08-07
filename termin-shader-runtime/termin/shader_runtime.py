"""Shared shader runtime configuration for source-project hosts."""

from __future__ import annotations

import os
from pathlib import Path

from tcbase import Settings, log

from termin.shader_tools import existing_executable, resolve_path_tool, resolve_sdk_tool


def _configured_tool(env_name: str, label: str) -> tuple[bool, Path | None]:
    configured = os.environ.get(env_name)
    if not configured:
        return False, None

    path = Path(configured)
    resolved = existing_executable(path)
    if resolved is not None:
        return True, resolved

    log.error(f"[ShaderRuntime] {env_name} points to missing {label}: {configured}")
    return True, None


def _settings_tool(key: str, label: str) -> tuple[bool, Path | None]:
    configured = Settings("termin").get(key, "")
    if not isinstance(configured, str) or not configured.strip():
        return False, None

    resolved = existing_executable(Path(configured.strip()))
    if resolved is not None:
        return True, resolved

    log.error(
        f"[ShaderRuntime] {key} points to missing {label}: {configured}"
    )
    return True, None


def resolve_termin_shaderc(anchor_file: Path | None = None) -> Path | None:
    configured_in_environment, environment_tool = _configured_tool(
        "TERMIN_SHADERC", "file"
    )
    if configured_in_environment:
        return environment_tool

    configured_in_settings, settings_tool = _settings_tool(
        "Build/shaderCompiler", "file"
    )
    if configured_in_settings:
        return settings_tool

    anchor = anchor_file if anchor_file is not None else Path(__file__)
    sdk_tool = resolve_sdk_tool("termin_shaderc", anchor)
    if sdk_tool is not None:
        return sdk_tool

    return resolve_path_tool("termin_shaderc")


def resolve_slangc(anchor_file: Path | None = None) -> Path | None:
    configured_in_environment, environment_tool = _configured_tool(
        "TERMIN_SLANGC", "slangc"
    )
    if configured_in_environment:
        return environment_tool

    configured_in_settings, settings_tool = _settings_tool(
        "Shader/slangCompiler", "slangc"
    )
    if configured_in_settings:
        return settings_tool

    anchor = anchor_file if anchor_file is not None else Path(__file__)
    sdk_tool = resolve_sdk_tool("slangc", anchor)
    if sdk_tool is not None:
        return sdk_tool

    return resolve_path_tool("slangc")


def slangc_unavailable_message(label: str, *, editor: bool = False) -> str:
    """Describe the actionable reason why ``resolve_slangc`` returned ``None``."""

    configured_environment = os.environ.get("TERMIN_SLANGC")
    if configured_environment:
        detail = (
            "TERMIN_SLANGC points to a missing or non-executable slangc: "
            f"'{configured_environment}'. Correct or unset TERMIN_SLANGC."
        )
    else:
        configured_setting = Settings("termin").get("Shader/slangCompiler", "")
        if isinstance(configured_setting, str) and configured_setting.strip():
            detail = (
                "Shader/slangCompiler points to a missing or non-executable slangc: "
                f"'{configured_setting.strip()}'. Correct the configured path."
            )
        else:
            detail = (
                "slangc was not found. Configure Shader/slangCompiler, set "
                "TERMIN_SLANGC, add slangc to PATH, or install it under TERMIN_SDK/bin."
            )

    guidance = " Slang source shaders cannot be compiled."
    if editor:
        guidance += (
            " Open Edit > Settings... > Slang Compiler "
            "(Shader/slangCompiler) to configure it."
        )
    return f"{label}: {detail}{guidance}"


def configure_project_shader_runtime(project_root: Path, *, label: str, render_engine) -> bool:
    """Configure dev shader compilation for source project rendering."""

    artifact_root = project_root / ".termin" / "shader-artifacts"
    cache_root = project_root / ".termin" / "shader-cache"

    compiler = resolve_termin_shaderc(Path(__file__))
    if compiler is None:
        log.error(
            f"[ShaderRuntime] termin_shaderc not found; {label} Slang shaders cannot compile"
        )
        return False

    slangc = resolve_slangc(Path(__file__))
    if slangc is None:
        log.warning(f"[ShaderRuntime] {slangc_unavailable_message(label)}")
        return False

    try:
        artifact_root.mkdir(parents=True, exist_ok=True)
        cache_root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        log.error(f"[ShaderRuntime] {label} failed to create shader cache directories: {exc}")
        return False

    os.environ["TERMIN_SLANGC"] = str(slangc)

    try:
        render_engine.configure_shader_artifacts(
            artifact_root=str(artifact_root),
            cache_root=str(cache_root),
            compiler_path=str(compiler),
            dev_compile_enabled=True,
        )
    except Exception as exc:
        log.error(f"[ShaderRuntime] {label} render engine shader configuration failed: {exc}")
        return False

    log.info(
        f"[ShaderRuntime] {label} configured: "
        f"artifact_root='{artifact_root}' cache_root='{cache_root}' "
        f"compiler='{compiler}' slangc='{slangc}' dev_compile=True"
    )
    return True
