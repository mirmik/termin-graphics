"""Shared shader runtime configuration for source-project hosts."""

from __future__ import annotations

import atexit
import os
from pathlib import Path

from tcbase import log

from termin.shader_tools import existing_executable, resolve_path_tool, resolve_sdk_tool


_GLSL_PREPROCESSOR_CLEANUP_REGISTERED = False


def configure_glsl_preprocessor_fallback() -> None:
    """Configure process ResourceManager fallback for GLSL ``#include`` loading."""
    global _GLSL_PREPROCESSOR_CLEANUP_REGISTERED

    import tgfx  # noqa: F401

    from termin.materials import glsl_preprocessor, register_glsl_preprocessor

    glsl_preprocessor().set_fallback_loader(_glsl_fallback_loader)
    register_glsl_preprocessor()
    if not _GLSL_PREPROCESSOR_CLEANUP_REGISTERED:
        atexit.register(unregister_glsl_preprocessor_fallback)
        _GLSL_PREPROCESSOR_CLEANUP_REGISTERED = True


def unregister_glsl_preprocessor_fallback() -> None:
    """Release Python callbacks held by the process-wide GLSL preprocessor."""
    global _GLSL_PREPROCESSOR_CLEANUP_REGISTERED

    try:
        from termin.materials import unregister_glsl_preprocessor

        unregister_glsl_preprocessor()
        if _GLSL_PREPROCESSOR_CLEANUP_REGISTERED:
            atexit.unregister(unregister_glsl_preprocessor_fallback)
        _GLSL_PREPROCESSOR_CLEANUP_REGISTERED = False
    except Exception as exc:
        log.error(f"[GlslPreprocessor] cleanup failed: {exc}")


def _glsl_fallback_loader(name: str) -> bool:
    """Load GLSL include from ResourceManager if it is not already registered."""
    from termin_assets import get_resource_manager

    try:
        rm = get_resource_manager()
        if rm is None:
            log.error(
                f"[GlslPreprocessor] Fallback: no ResourceManager configured for GLSL '{name}'"
            )
            return False
        asset = rm.glsl.get_asset(name)
        if asset is None:
            log.error(f"[GlslPreprocessor] Fallback: glsl '{name}' not found in ResourceManager")
            return False

        asset.ensure_loaded()
        from termin.materials import glsl_preprocessor

        return glsl_preprocessor().has_include(name)
    except Exception as exc:
        log.error(f"[GlslPreprocessor] Fallback loader error for GLSL include '{name}': {exc}")
        return False


def _configured_tool(env_name: str, label: str) -> Path | None:
    configured = os.environ.get(env_name)
    if not configured:
        return None

    path = Path(configured)
    resolved = existing_executable(path)
    if resolved is not None:
        return resolved

    log.error(f"[ShaderRuntime] {env_name} points to missing {label}: {configured}")
    return None


def resolve_termin_shaderc(anchor_file: Path | None = None) -> Path | None:
    configured = _configured_tool("TERMIN_SHADERC", "file")
    if configured is not None:
        return configured

    anchor = anchor_file if anchor_file is not None else Path(__file__)
    sdk_tool = resolve_sdk_tool("termin_shaderc", anchor)
    if sdk_tool is not None:
        return sdk_tool

    return resolve_path_tool("termin_shaderc")


def resolve_slangc(anchor_file: Path | None = None) -> Path | None:
    configured = _configured_tool("TERMIN_SLANGC", "slangc")
    if configured is not None:
        return configured

    anchor = anchor_file if anchor_file is not None else Path(__file__)
    sdk_tool = resolve_sdk_tool("slangc", anchor)
    if sdk_tool is not None:
        return sdk_tool

    return resolve_path_tool("slangc")


def configure_project_shader_runtime(project_root: Path, *, label: str) -> bool:
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
        log.error(f"[ShaderRuntime] slangc not found; {label} Slang shaders cannot compile")
        return False

    try:
        artifact_root.mkdir(parents=True, exist_ok=True)
        cache_root.mkdir(parents=True, exist_ok=True)
    except OSError as exc:
        log.error(f"[ShaderRuntime] {label} failed to create shader cache directories: {exc}")
        return False

    os.environ["TERMIN_SLANGC"] = str(slangc)

    try:
        import tgfx

        tgfx.configure_shader_runtime(
            artifact_root=str(artifact_root),
            cache_root=str(cache_root),
            shader_compiler=str(compiler),
            dev_compile=True,
        )
    except Exception as exc:
        log.error(f"[ShaderRuntime] {label} configure_shader_runtime failed: {exc}")
        return False

    log.info(
        f"[ShaderRuntime] {label} configured: "
        f"artifact_root='{artifact_root}' cache_root='{cache_root}' "
        f"compiler='{compiler}' slangc='{slangc}' dev_compile=True"
    )
    return True
