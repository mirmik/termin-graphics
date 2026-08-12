from __future__ import annotations

import importlib
from pathlib import Path
import sys

import numpy as np

from tcnodegraph import (
    DictSchemaProvider,
    Graph,
    GraphController,
    NodeBodyContent,
    NodeBodyLayout,
    NodeTemplate,
    build_native_node_graph_view,
)
from tcplot import SurfaceColorMap
from tcplot_gui_native import Plot2D, Plot3D
from termin.gui_native import Size, build_python_showcase

from .model import Section, SectionContent


_REQUIRED_IMPORTS = (
    ("termin-build-tools", "termin_build"),
    ("termin-nanobind-sdk", "termin_nanobind"),
    ("termin-base", "tcbase"),
    ("termin-dispatch", "termin.dispatch"),
    ("termin-image", "termin.image"),
    ("termin-assets", "termin_assets"),
    ("termin-tween", "termin.tween"),
    ("termin-mesh", "tmesh"),
    ("termin-graphics", "tgfx"),
    ("termin-visual-scene", "termin.visual_scene"),
    ("termin-inspect", "termin.inspect"),
    ("termin-shader-runtime", "termin.shader_runtime"),
    ("termin-gui-native", "termin.gui_native"),
    ("termin-nodegraph", "tcnodegraph"),
    ("tcplot", "tcplot"),
    ("tcplot-gui-native", "tcplot_gui_native"),
)


def import_profile_surface() -> dict[str, object]:
    """Import the unconditional Python surface promised by the graphics profile."""

    imported: dict[str, str] = {}
    for distribution, module_name in _REQUIRED_IMPORTS:
        module = importlib.import_module(module_name)
        imported[distribution] = module.__name__
    forbidden = sorted(
        name
        for name in sys.modules
        if name == "termin.display" or name.startswith("termin.display.")
    )
    if forbidden:
        raise RuntimeError(
            "graphics showcase loaded full-only termin.display modules: "
            + ", ".join(forbidden)
        )
    return {
        "imported": imported,
        "conditional": {
            "termin-window": "available only in SDL-enabled graphics builds",
        },
        "forbidden_modules_loaded": forbidden,
    }


def _native_ui(application) -> SectionContent:
    showcase = build_python_showcase(application.document)
    return SectionContent(
        facts={
            "root": showcase.root.stable_id,
            "widget_groups": sorted(showcase.widgets),
            "model_count": len(showcase.models),
        }
    )


def _plot_2d(application) -> SectionContent:
    plot = Plot2D(application.document)
    plot.widget.preferred_size = Size(960.0, 640.0)
    x = np.linspace(0.0, 4.0 * np.pi, 480)
    first = plot.plot(x, np.sin(x), thickness=2.5)
    plot.plot(x, 0.55 * np.cos(2.0 * x), thickness=2.0)
    plot.append_line_data(first, [4.0 * np.pi + 0.05], [0.05])
    plot.set_title("Graphics profile — retained Plot2D")
    plot.set_axis_labels("phase", "amplitude")
    if not application.document.add_root(plot.handle):
        raise RuntimeError("failed to add Plot2D showcase root")
    return SectionContent(facts={"line_count": plot.line_count, "samples": 961})


def _populate_plot_3d(plot: Plot3D) -> dict[str, object]:
    phase = np.linspace(0.0, 6.0 * np.pi, 360)
    line = plot.plot(
        np.cos(phase),
        np.sin(phase),
        phase * 0.07 - 0.65,
        thickness=2.5,
    )
    scatter_phase = np.linspace(0.0, 2.0 * np.pi, 40, endpoint=False)
    scatter = plot.scatter(
        1.35 * np.cos(scatter_phase),
        1.35 * np.sin(scatter_phase),
        np.zeros_like(scatter_phase),
        size=5.0,
    )
    coordinates = np.linspace(-1.4, 1.4, 28)
    x, y = np.meshgrid(coordinates, coordinates)
    z = 0.35 * np.sin(2.2 * x) * np.cos(2.2 * y)
    surface = plot.surface(x, y, z, colormap=SurfaceColorMap.Viridis)
    plot.set_axis_labels("x", "y", "z")
    plot.set_surface_shading(True, 0.4)
    plot.fit_camera()
    return {
        "line_item": [line.scene_id, line.index, line.generation],
        "scatter_item": [scatter.scene_id, scatter.index, scatter.generation],
        "surface_item": [surface.scene_id, surface.index, surface.generation],
        "item_count": plot.item_count,
    }


def _plot_3d(application) -> SectionContent:
    plot = Plot3D(application.document)
    plot.widget.preferred_size = Size(960.0, 640.0)
    facts = _populate_plot_3d(plot)
    if not application.document.add_root(plot.handle):
        raise RuntimeError("failed to add Plot3D showcase root")
    return SectionContent(facts=facts)


def _make_graph() -> tuple[Graph, GraphController, dict[str, str]]:
    graph = Graph(data={"example": "graphics-showcase"})
    schema = DictSchemaProvider(
        {
            "signal": NodeTemplate(
                kind="signal",
                title="Retained Plot2D",
                outputs=[("samples", "signal")],
                width=470.0,
                height=370.0,
            ),
            "surface": NodeTemplate(
                kind="surface",
                title="Retained Plot3D",
                inputs=[("samples", "signal")],
                width=520.0,
                height=430.0,
            ),
        }
    )
    controller = GraphController(graph, schema=schema)
    signal = controller.create_node("signal", x=-480.0, y=-190.0)
    surface = controller.create_node("surface", x=40.0, y=-220.0)
    result = controller.connect(signal.id, "samples", surface.id, "samples")
    if not result.ok:
        raise RuntimeError(f"failed to connect showcase nodes: {result.reason}")
    controller.add_group("Graphics profile composition", -510.0, -250.0, 1110.0, 540.0)
    return graph, controller, {signal.id: "plot2d", surface.id: "plot3d"}


def _visual_scene_nodegraph(application) -> SectionContent:
    graph, controller, _ = _make_graph()
    view = build_native_node_graph_view(
        application.document,
        graph,
        controller=controller,
        request_render=application.request_repaint,
    )
    if not application.document.add_root(view.root.handle):
        view.close()
        raise RuntimeError("failed to add visual-scene nodegraph showcase root")
    return SectionContent(
        cleanup=view.close,
        facts={"nodes": len(graph.nodes), "edges": len(graph.edges), "visual_scene": True},
    )


def _plot_nodegraph_composition(application) -> SectionContent:
    graph, controller, kinds = _make_graph()
    plots: dict[str, object] = {}

    def body_provider(document, node):
        kind = kinds[node.id]
        if kind == "plot2d":
            plot = Plot2D(document)
            phase = np.linspace(0.0, 3.0 * np.pi, 320)
            plot.plot(phase, np.sin(phase), thickness=2.2)
            plot.plot(phase, np.cos(phase), thickness=1.8)
            plot.set_axis_labels("time", "signal")
            plots[node.id] = plot
            return NodeBodyContent(plot.widget, NodeBodyLayout(height=250.0))
        plot = Plot3D(document)
        _populate_plot_3d(plot)
        plots[node.id] = plot
        return NodeBodyContent(plot.widget, NodeBodyLayout(height=310.0))

    view = build_native_node_graph_view(
        application.document,
        graph,
        controller=controller,
        request_render=application.request_repaint,
        body_content_provider=body_provider,
    )
    if not application.document.add_root(view.root.handle):
        view.close()
        raise RuntimeError("failed to add plot/nodegraph composition root")
    return SectionContent(
        cleanup=view.close,
        facts={
            "nodes": len(graph.nodes),
            "edges": len(graph.edges),
            "embedded_plots": len(plots),
            "plot_types": sorted(type(plot).__name__ for plot in plots.values()),
        },
    )


def section_registry() -> tuple[Section, ...]:
    return (
        Section(
            name="native_ui",
            description="Retained native controls, models, text and layout",
            capabilities=("termin-gui-native", "text", "collections", "layout"),
            build=_native_ui,
        ),
        Section(
            name="plot_2d",
            description="Retained 2D lines, updates, axes and labels",
            capabilities=("tcplot", "tcplot-gui-native", "visual-scene", "plot2d"),
            build=_plot_2d,
        ),
        Section(
            name="plot_3d",
            description="Retained 3D line, scatter, surface and colormap",
            capabilities=("tcplot", "tcplot-gui-native", "tgfx", "plot3d"),
            build=_plot_3d,
        ),
        Section(
            name="visual_scene_nodegraph",
            description="Visual-scene primitives projected as a native node graph",
            capabilities=("termin-visual-scene", "termin-nodegraph", "interaction"),
            build=_visual_scene_nodegraph,
        ),
        Section(
            name="plot_nodegraph_composition",
            description="Plot2D and Plot3D embedded as node body widgets",
            capabilities=("composition", "plot2d", "plot3d", "nodegraph"),
            build=_plot_nodegraph_composition,
            artifact=True,
        ),
    )


def sdk_font_path() -> Path:
    sdk_root = Path(sys.executable).resolve().parent.parent
    font = sdk_root / "share" / "termin" / "fonts" / "DroidSans.ttf"
    if not font.is_file():
        raise FileNotFoundError(
            f"graphics showcase requires the SDK font artifact: {font}"
        )
    return font
