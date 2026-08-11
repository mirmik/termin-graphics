"""Standalone interactive showcase for the native node graph projection."""

from __future__ import annotations

import argparse
import logging
import os
from pathlib import Path
import sys
import time

import tgfx
from termin.gui_native import (
    OffscreenGuiComposition,
    tc_ui_document_create,
    tc_ui_document_destroy,
)

from tcnodegraph.controller import GraphController
from tcnodegraph.model import Graph
from tcnodegraph.native_view import build_native_node_graph_view


_log = logging.getLogger(__name__)


def make_demo_graph() -> Graph:
    """Build a graph that exercises every native inline parameter editor."""

    graph = Graph(data={"example": "native-nodegraph"})
    controller = GraphController(graph)

    scene_color = controller.create_node("resource", title="Scene Color", x=-360.0, y=-230.0)
    scene_color.params.update({"format": "RGBA16F", "width": 1920, "height": 1080})
    scene_color.width = 240.0
    scene_color.data["param_specs"] = {
        "format": {
            "kind": "enum",
            "label": "Format",
            "items": ["RGBA8", "RGBA16F", "RGBA32F"],
        },
        "width": {
            "kind": "int",
            "label": "Width",
            "min": 64,
            "max": 8192,
            "step": 1,
        },
        "height": {
            "kind": "int",
            "label": "Height",
            "min": 64,
            "max": 8192,
            "step": 1,
        },
    }

    color = controller.create_node("pass", title="Color Pass", x=-300.0, y=20.0)
    color.params.update(
        {
            "enabled": True,
            "samples": 4,
            "exposure": 1.1,
            "quality": "High",
            "label": "Main Color",
        }
    )
    color.width = 240.0
    color.data["param_specs"] = {
        "enabled": {"kind": "bool", "label": "Enabled"},
        "samples": {
            "kind": "int",
            "label": "MSAA Samples",
            "min": 1,
            "max": 16,
            "step": 1,
        },
        "exposure": {
            "kind": "float",
            "label": "Exposure",
            "min": 0.05,
            "max": 8.0,
            "step": 0.05,
            "decimals": 2,
        },
        "quality": {
            "kind": "enum",
            "label": "Quality",
            "items": ["Low", "Medium", "High", "Ultra"],
        },
        "label": {"kind": "string", "label": "Debug Label"},
    }

    bloom = controller.create_node("effect", title="Bloom", x=30.0, y=40.0)
    bloom.params.update({"enabled": True, "threshold": 1.25, "iterations": 5})
    bloom.width = 240.0
    bloom.data["param_specs"] = {
        "enabled": {"kind": "bool", "label": "Enabled"},
        "threshold": {
            "kind": "float",
            "label": "Threshold",
            "min": 0.0,
            "max": 4.0,
            "step": 0.05,
            "decimals": 2,
        },
        "iterations": {
            "kind": "int",
            "label": "Iterations",
            "min": 1,
            "max": 16,
            "step": 1,
        },
    }

    present = controller.create_node("output", title="Present", x=340.0, y=70.0)
    present.params.update({"vsync": True, "gamma": 2.2, "output": "sRGB"})
    present.width = 240.0
    present.data["param_specs"] = {
        "vsync": {"kind": "bool", "label": "VSync"},
        "gamma": {
            "kind": "float",
            "label": "Gamma",
            "min": 1.0,
            "max": 3.0,
            "step": 0.01,
            "decimals": 2,
        },
        "output": {
            "kind": "enum",
            "label": "Output",
            "items": ["Linear", "sRGB", "HDR10"],
        },
    }

    controller.add_output_socket(scene_color.id, "fbo", "fbo", multi=False)
    controller.add_input_socket(color.id, "input", "fbo")
    controller.add_output_socket(color.id, "output", "fbo")
    controller.add_input_socket(bloom.id, "input", "fbo")
    controller.add_output_socket(bloom.id, "output", "fbo")
    controller.add_input_socket(present.id, "input", "fbo")
    controller.connect(scene_color.id, "fbo", color.id, "input")
    controller.connect(color.id, "output", bloom.id, "input")
    controller.connect(bloom.id, "output", present.id, "input")
    controller.add_group("Main Viewport", -410.0, -280.0, 1020.0, 570.0)
    return graph


def _font_path() -> Path:
    configured = os.environ.get("TERMIN_UI_FONT")
    candidates = [Path(configured)] if configured else []
    sdk_root = Path(sys.executable).resolve().parent.parent
    candidates.extend(
        (
            sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf",
            Path.cwd() / "termin-thirdparty" / "recastnavigation" / "RecastDemo" / "Bin" / "DroidSans.ttf",
        )
    )
    font = next((candidate for candidate in candidates if candidate.is_file()), None)
    if font is None:
        raise FileNotFoundError(
            "native nodegraph example could not find DroidSans.ttf; "
            "set TERMIN_UI_FONT or run it from a built SDK/source checkout"
        )
    return font


def run_windowed(*, frame_limit: int = 0, second_limit: float = 0.0) -> int:
    """Run the interactive example using the full SDK window host."""

    from termin.window import WindowManager, WindowedGraphicsSession, quit_sdl
    from termin.gui_native.window import GuiWindowAdapter

    graphics_session = None
    window_manager = None
    window_handle = None
    document = None
    adapter = None
    graph_view = None
    try:
        graphics_session = WindowedGraphicsSession.create_native()
        window_manager = WindowManager(graphics_session)
        window_handle = window_manager.create_window("termin-nodegraph native example", 1280, 820)
        document = tc_ui_document_create()
        adapter = GuiWindowAdapter(
            window_manager,
            window_handle,
            document,
            font_path=str(_font_path()),
            font_size=15,
            enable_text_input=True,
        )
        graph_view = build_native_node_graph_view(
            document,
            make_demo_graph(),
            request_render=adapter.request_repaint,
        )
        if not document.add_root(graph_view.root.handle):
            raise RuntimeError("failed to attach the native nodegraph example root")

        adapter.request_repaint()
        started = time.monotonic()
        frame_count = 0
        warmup_frames = 3
        while not adapter.should_close:
            window_manager.pump_events()
            adapter.consume_pending_events(window_manager, window_handle, None)
            if adapter.should_close:
                break
            if adapter.repaint_requested:
                adapter.render_frame()
                frame_count += 1
                if frame_count < warmup_frames or (frame_limit and frame_count < frame_limit):
                    adapter.request_repaint()
            else:
                time.sleep(0.01)
            if frame_limit and frame_count >= frame_limit:
                break
            if second_limit and time.monotonic() - started >= second_limit:
                break
        return 0
    except Exception:
        _log.exception("termin-nodegraph native example failed")
        raise
    finally:
        if graph_view is not None:
            graph_view.close()
        if adapter is not None:
            adapter.close()
        if document is not None:
            tc_ui_document_destroy(document)
        if window_manager is not None:
            window_manager.close()
        if graphics_session is not None:
            graphics_session.close()
        quit_sdl()


def run_offscreen(output_path: str | Path) -> int:
    """Render one inspectable PNG using the graphics-profile composition host."""

    import numpy as np
    from termin.image import write_png_rgba8_file

    output = Path(output_path).resolve()
    composition = OffscreenGuiComposition(
        width=1280,
        height=820,
        font_path=str(_font_path()),
        continuous_rendering=False,
    )
    graph_view = None
    try:
        graph_view = build_native_node_graph_view(
            composition.document,
            make_demo_graph(),
            request_render=composition.request_repaint,
        )
        if not composition.document.add_root(graph_view.root.handle):
            raise RuntimeError("failed to attach the native nodegraph example root")
        for _ in range(3):
            composition.request_repaint()
            if not composition.render_frame():
                raise RuntimeError("failed to render the native nodegraph example")
        composition.wait_idle()
        rgba = composition.read_frame_rgba_float()
        rgba8 = np.clip(rgba * 255.0, 0.0, 255.0).astype(np.uint8)
        output.parent.mkdir(parents=True, exist_ok=True)
        write_png_rgba8_file(output, rgba8)
        print(f"Native nodegraph example rendered to {output}")
        return 0
    except Exception:
        _log.exception("termin-nodegraph offscreen example failed")
        raise
    finally:
        if graph_view is not None:
            graph_view.close()
        composition.close()


def run(
    *,
    frame_limit: int = 0,
    second_limit: float = 0.0,
    offscreen: bool = False,
    output_path: str | Path = "nodegraph-example.png",
) -> int:
    """Run the explicitly selected interactive or offscreen host."""

    if frame_limit < 0:
        raise ValueError("frame_limit must be non-negative")
    if second_limit < 0.0:
        raise ValueError("second_limit must be non-negative")
    if not tgfx.configure_default_shader_runtime("termin-nodegraph-example"):
        return 77
    if offscreen:
        return run_offscreen(output_path)
    return run_windowed(frame_limit=frame_limit, second_limit=second_limit)


def _parse_args(argv: list[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", type=int, default=0, help="exit after this many rendered frames")
    parser.add_argument("--seconds", type=float, default=0.0, help="exit after this many seconds")
    parser.add_argument(
        "--offscreen",
        action="store_true",
        help="render a PNG instead of opening a window",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("nodegraph-example.png"),
        help="offscreen PNG path (default: nodegraph-example.png)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(argv)
    return run(
        frame_limit=args.frames,
        second_limit=args.seconds,
        offscreen=args.offscreen,
        output_path=args.output,
    )


if __name__ == "__main__":
    raise SystemExit(main())
