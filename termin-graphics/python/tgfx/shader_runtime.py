"""Shader runtime configuration for standalone tgfx examples/tools."""

from __future__ import annotations

import os
from pathlib import Path
import shutil

from tcbase import log


_configured = False


def _candidate_tool_paths(path: Path) -> tuple[Path, ...]:
    if os.name == "nt" and path.suffix == "":
        return (path.with_name(f"{path.name}.exe"), path)
    return (path,)


def _existing_tool(path: Path) -> Path | None:
    for candidate in _candidate_tool_paths(path):
        if candidate.is_file():
            return candidate
    return None


def _sdk_tool_dirs() -> tuple[Path, ...]:
    dirs: list[Path] = []

    sdk = os.environ.get("TERMIN_SDK")
    if sdk:
        dirs.append(Path(sdk) / "bin")

    for parent in Path(__file__).resolve().parents:
        dirs.append(parent / "bin")
        dirs.append(parent / "sdk" / "bin")

    return tuple(dirs)


def _resolve_tool(name: str, env_name: str) -> Path | None:
    configured = os.environ.get(env_name)
    if configured:
        path = Path(configured)
        resolved = _existing_tool(path)
        if resolved is not None:
            return resolved
        log.error(f"[ShaderRuntime] {env_name} points to missing file: {configured}")
        return None

    for directory in _sdk_tool_dirs():
        resolved = _existing_tool(directory / name)
        if resolved is not None:
            return resolved

    found = shutil.which(name)
    if found:
        return Path(found)
    return None


def _cache_root(label: str) -> Path:
    configured = os.environ.get("TERMIN_SDK_SHADER_CACHE_ROOT")
    if configured:
        return Path(configured) / label

    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        base = Path(local_app_data) if local_app_data else Path.home() / "AppData" / "Local"
        return base / "Termin" / "Cache" / f"{label}-shaders"

    xdg_cache = os.environ.get("XDG_CACHE_HOME")
    base = Path(xdg_cache) if xdg_cache else Path.home() / ".cache"
    return base / "termin" / f"{label}-shaders"


def configure_default_shader_runtime(label: str = "python") -> bool:
    """Configure Slang shader compilation for standalone tgfx hosts."""

    global _configured
    if _configured:
        return True

    import tgfx

    if tgfx.get_shader_dev_compile_enabled() and tgfx.get_shader_compiler_path():
        _configured = True
        return True

    compiler = _resolve_tool("termin_shaderc", "TERMIN_SHADERC")
    if compiler is None:
        log.error(
            f"[ShaderRuntime] termin_shaderc not found; {label} Slang shaders cannot compile"
        )
        return False

    slangc = _resolve_tool("slangc", "TERMIN_SLANGC")
    if slangc is None:
        log.error(f"[ShaderRuntime] slangc not found; {label} Slang shaders cannot compile")
        return False

    root = _cache_root(label)
    artifact_root = root / "artifacts"
    cache_root = root / "cache"
    try:
        artifact_root.mkdir(parents=True, exist_ok=True)
        cache_root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        log.error(
            f"[ShaderRuntime] failed to create {label} shader cache directories: {exc}"
        )
        return False
    os.environ["TERMIN_SLANGC"] = str(slangc)

    tgfx.configure_shader_runtime(
        artifact_root=str(artifact_root),
        cache_root=str(cache_root),
        shader_compiler=str(compiler),
        dev_compile=True,
    )
    _configured = True
    log.info(
        f"[ShaderRuntime] {label} configured: "
        f"artifact_root='{artifact_root}' cache_root='{cache_root}' "
        f"compiler='{compiler}' slangc='{slangc}' dev_compile=True"
    )
    return True
