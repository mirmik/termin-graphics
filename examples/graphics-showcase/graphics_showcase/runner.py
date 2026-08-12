from __future__ import annotations

from datetime import datetime, timezone
import json
import logging
from pathlib import Path
import platform
import sys
import time

import numpy as np
import tgfx
from termin.gui_native import OffscreenGuiComposition
from termin.image import write_png_rgba8_file

from .model import ShowcaseConfig
from .sections import import_profile_surface, sdk_font_path, section_registry


_LOG = logging.getLogger("graphics_showcase")
_CLEAR_RGB = np.asarray([0.03, 0.035, 0.045], dtype=np.float32)


def _frame_metrics(pixels: np.ndarray) -> dict[str, object]:
    if pixels.ndim != 3 or pixels.shape[2] != 4:
        raise RuntimeError(f"unexpected framebuffer shape: {pixels.shape}")
    delta = np.max(np.abs(pixels[..., :3] - _CLEAR_RGB), axis=-1)
    changed = int(np.count_nonzero(delta > 0.025))
    if changed < 256:
        raise RuntimeError(
            f"framebuffer remained effectively clear: only {changed} changed pixels"
        )
    return {
        "shape": list(pixels.shape),
        "changed_pixels": changed,
        "changed_fraction": round(changed / float(delta.size), 6),
        "rgb_min": [round(float(value), 6) for value in pixels[..., :3].min(axis=(0, 1))],
        "rgb_max": [round(float(value), 6) for value in pixels[..., :3].max(axis=(0, 1))],
    }


def _rgba8(pixels: np.ndarray) -> np.ndarray:
    return np.clip(pixels * 255.0, 0.0, 255.0).astype(np.uint8)


def _render_section(section, config: ShowcaseConfig, font_path: Path) -> dict[str, object]:
    started = time.monotonic()
    application = OffscreenGuiComposition(
        width=config.width,
        height=config.height,
        font_path=str(font_path),
        continuous_rendering=False,
        application_graphics_domain=True,
    )
    content = None
    try:
        content = section.build(application)
        for _ in range(3):
            application.request_repaint()
            if not application.render_frame():
                raise RuntimeError("offscreen composition declined a requested frame")
        application.wait_idle()
        pixels = application.read_frame_rgba_float()
        metrics = _frame_metrics(pixels)
        artifact = None
        if section.artifact:
            config.output_path.parent.mkdir(parents=True, exist_ok=True)
            write_png_rgba8_file(config.output_path, _rgba8(pixels))
            artifact = str(config.output_path)
        return {
            "name": section.name,
            "description": section.description,
            "capabilities": list(section.capabilities),
            "status": "passed",
            "duration_seconds": round(time.monotonic() - started, 4),
            "frame": metrics,
            "facts": content.facts,
            "artifact": artifact,
        }
    except Exception:
        _LOG.exception("graphics showcase section '%s' failed", section.name)
        raise
    finally:
        if content is not None:
            try:
                content.cleanup()
            except Exception:
                _LOG.exception(
                    "graphics showcase section '%s' cleanup failed", section.name
                )
                raise
        application.close()


def run_showcase(config: ShowcaseConfig) -> dict[str, object]:
    if config.width < 320 or config.height < 240:
        raise ValueError("showcase framebuffer must be at least 320x240")
    if not tgfx.configure_default_shader_runtime("graphics-profile-showcase"):
        raise RuntimeError("failed to configure the graphics SDK shader runtime")

    imports = import_profile_surface()
    font = sdk_font_path()
    sections = [
        _render_section(section, config, font) for section in section_registry()
    ]
    if not config.output_path.is_file():
        raise RuntimeError("artifact section did not produce the requested PNG")
    report = {
        "schema": 1,
        "showcase": "termin-graphics-profile",
        "status": "passed",
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "runtime": {
            "python": sys.version,
            "executable": sys.executable,
            "platform": platform.platform(),
            "isolated": bool(sys.flags.isolated),
            "font": str(font),
        },
        "imports": imports,
        "sections": sections,
        "artifact": str(config.output_path),
    }
    config.report_path.parent.mkdir(parents=True, exist_ok=True)
    config.report_path.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report
