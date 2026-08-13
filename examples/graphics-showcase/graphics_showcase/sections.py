from __future__ import annotations

import importlib
import importlib.util
from functools import lru_cache
import math
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
from termin.gui_native import Point, Size, build_python_showcase
from termin.visual_scene import (
    tc_visual_scene3d_create,
    tc_visual_scene3d_destroy,
    tc_visual_scene_create,
    tc_visual_scene_destroy,
)

from .model import Section, SectionContent


_REQUIRED_IMPORTS = (
    ("termin-build-tools", "termin_build"),
    ("termin-nanobind-sdk", "termin_nanobind"),
    ("termin-base", "tcbase"),
    ("termin-dispatch", "termin.dispatch"),
    ("termin-image", "termin.image"),
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
        root=showcase.root,
        facts={
            "root": showcase.root.stable_id,
            "widget_groups": sorted(showcase.widgets),
            "model_count": len(showcase.models),
        }
    )


def _graphics_line_gallery(application) -> SectionContent:
    from tcbase._geom_native import LinearColor
    from termin.geombase import OrbitCamera, Vec3
    from tgfx import (
        CULL_NONE,
        LineCapStyle,
        LineJoinStyle,
        LinePoint3,
        LineStyle,
        Tgfx2Context,
        Tgfx2PixelFormat,
        Tgfx2ShaderStage,
        build_line_mesh,
    )

    if application.graphics is None:
        raise RuntimeError("graphics line gallery requires its composition GraphicsHost")
    width, height = 1100, 620
    context = Tgfx2Context.from_runtime(application.graphics)
    color = context.create_color_attachment(width, height, Tgfx2PixelFormat.RGBA8_UNorm)
    depth = context.create_depth_attachment(width, height, Tgfx2PixelFormat.D32F)
    camera = OrbitCamera()
    camera.target = Vec3(0.0, 0.0, 0.2)
    camera.distance = 6.2
    camera.fitted_radius = 2.8
    vertex_source = """#version 450 core
#ifdef VULKAN
layout(push_constant) uniform PCBlock { mat4 u_mvp; vec4 u_color; } pc;
#define U_MVP pc.u_mvp
#else
uniform mat4 u_mvp;
#define U_MVP u_mvp
#endif
layout(location=0) in vec3 a_position;
layout(location=1) in vec4 a_color;
layout(location=0) out vec4 v_color;
void main() { gl_Position = U_MVP * vec4(a_position, 1.0); v_color = a_color; }
"""
    fragment_source = """#version 450 core
#ifdef VULKAN
layout(push_constant) uniform PCBlock { mat4 u_mvp; vec4 u_color; } pc;
#define U_COLOR pc.u_color
#else
uniform vec4 u_color;
#define U_COLOR u_color
#endif
layout(location=0) out vec4 frag_color;
layout(location=0) in vec4 v_color;
void main() { frag_color = U_COLOR * v_color; }
"""
    vertex_shader = context.device.create_shader(Tgfx2ShaderStage.Vertex, vertex_source)
    fragment_shader = context.device.create_shader(Tgfx2ShaderStage.Fragment, fragment_source)

    colors = (
        (0.25, 0.65, 1.0, 1.0),
        (0.35, 0.92, 0.55, 1.0),
        (1.0, 0.68, 0.22, 1.0),
        (0.95, 0.32, 0.48, 1.0),
    )
    lines = []
    for index, angle_degrees in enumerate((20.0, 45.0, 90.0, 145.0)):
        angle = math.radians(angle_degrees)
        y = 1.75 - index * 1.05
        style = LineStyle()
        style.width = 0.045 + index * 0.018
        style.up_hint = LinePoint3(0.0, 0.0, 1.0)
        style.cap = (LineCapStyle.Butt, LineCapStyle.Square, LineCapStyle.Round, LineCapStyle.Round)[index]
        style.join = (LineJoinStyle.Bevel, LineJoinStyle.Bevel, LineJoinStyle.Round, LineJoinStyle.Round)[index]
        style.round_segments = 18
        points = [
            LinePoint3(-2.2, y, 0.0),
            LinePoint3(-0.6, y, 0.0),
            LinePoint3(-0.6 + math.cos(angle), y + math.sin(angle), 0.0),
            LinePoint3(1.9, y + math.sin(angle), 0.0),
        ]
        mesh = build_line_mesh(points, style)
        lines.append((np.asarray(mesh.triangle_vertices, dtype=np.float32), colors[index]))
    spiral = [
        LinePoint3(
            1.15 * math.cos(index / 90.0 * math.tau * 2.4),
            1.15 * math.sin(index / 90.0 * math.tau * 2.4),
            -1.2 + index / 90.0 * 2.4,
        )
        for index in range(91)
    ]
    spiral_style = LineStyle()
    spiral_style.width = 0.055
    spiral_style.up_hint = LinePoint3(0.0, 0.0, 1.0)
    spiral_style.cap = LineCapStyle.Round
    spiral_style.join = LineJoinStyle.Round
    spiral_style.round_segments = 18
    spiral_mesh = build_line_mesh(spiral, spiral_style)
    lines.append((np.asarray(spiral_mesh.triangle_vertices, dtype=np.float32), (0.72, 0.58, 1.0, 1.0)))

    try:
        context.context.begin_frame()
        context.context.begin_pass(
            color,
            depth,
            clear_linear_color=LinearColor(0.025, 0.035, 0.055, 1.0),
            clear_depth_enabled=True,
            clear_depth=1.0,
        )
        context.context.set_viewport(0, 0, width, height)
        context.context.set_depth_test(True)
        context.context.set_depth_write(True)
        context.context.set_cull(CULL_NONE)
        context.context.bind_shader(vertex_shader, fragment_shader)
        mvp = np.asarray(camera.mvp(width / height), dtype=np.float32)
        for vertices, rgba in lines:
            if vertices.size == 0:
                continue
            packed = np.concatenate((mvp, np.ones(4, dtype=np.float32))).view(np.uint8)
            context.context.set_push_constants(np.ascontiguousarray(packed, dtype=np.uint8))
            expanded = np.zeros((vertices.shape[0], 7), dtype=np.float32)
            expanded[:, :3] = vertices
            expanded[:, 3:] = np.asarray(rgba, dtype=np.float32)
            context.context.draw_immediate_triangles(expanded, int(expanded.shape[0]))
        context.context.end_pass()
        context.context.end_frame()
    except Exception:
        context.device.destroy_shader(fragment_shader)
        context.device.destroy_shader(vertex_shader)
        context.destroy_texture(depth)
        context.destroy_texture(color)
        raise

    canvas = application.document.create_canvas()
    canvas.widget.preferred_size = Size(float(width), float(height))
    canvas.set_texture(color, Size(float(width), float(height)))
    canvas.fit_in_view()
    if not application.document.add_root(canvas.handle):
        canvas.clear_texture()
        context.device.destroy_shader(fragment_shader)
        context.device.destroy_shader(vertex_shader)
        context.destroy_texture(depth)
        context.destroy_texture(color)
        raise RuntimeError("failed to add graphics line gallery root")

    def cleanup() -> None:
        canvas.clear_texture()
        context.device.destroy_shader(fragment_shader)
        context.device.destroy_shader(vertex_shader)
        context.destroy_texture(depth)
        context.destroy_texture(color)

    return SectionContent(
        root=canvas.widget,
        cleanup=cleanup,
        facts={
            "renderer": "build_line_mesh",
            "polylines": len(lines),
            "caps": ["butt", "square", "round"],
            "joins": ["bevel", "round"],
            "world_width": [0.045, 0.063, 0.081, 0.099],
        },
    )


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


@lru_cache(maxsize=1)
def _tcplot_gallery_module():
    source = Path(__file__).resolve().parents[3] / "tcplot" / "examples" / "_gallery.py"
    spec = importlib.util.spec_from_file_location("termin_graphics_showcase_tcplot_gallery", source)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load restored tcplot gallery: {source}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _tcplot_example(application, builder_name: str, facts: dict[str, object]) -> SectionContent:
    gallery = _tcplot_gallery_module()
    builder = vars(gallery)[builder_name]
    root = builder(application.document)
    if not application.document.add_root(root.handle):
        raise RuntimeError(f"failed to add restored tcplot example '{builder_name}'")
    return SectionContent(root=root.widget, facts=facts)


def _tcplot_sine(application) -> SectionContent:
    return _tcplot_example(
        application,
        "sine_plot",
        {"line_series": 3, "samples": 1500, "scenario": "trigonometric-functions"},
    )


def _tcplot_scatter(application) -> SectionContent:
    return _tcplot_example(
        application,
        "scatter_plot",
        {"scatter_series": 3, "points": 300, "line_series": 1},
    )


def _tcplot_multi(application) -> SectionContent:
    return _tcplot_example(
        application,
        "multi_plot",
        {"plots": 2, "line_series": 7, "layout": "side-by-side"},
    )


def _tcplot_marker(application) -> SectionContent:
    return _tcplot_example(
        application,
        "marker_plot",
        {"line_series": 1, "annotation": "draggable-data-marker", "snap": "nearest-sample"},
    )


def _tcplot_helix(application) -> SectionContent:
    return _tcplot_example(
        application,
        "helix_plot",
        {"line_series": 2, "scatter_series": 1, "scenario": "double-helix"},
    )


def _tcplot_surface(application) -> SectionContent:
    return _tcplot_example(
        application,
        "surface_plot",
        {
            "surfaces": 2,
            "colormap": "Viridis",
            "colorbar": True,
            "wireframe": True,
            "z_scale": 5.0,
        },
    )


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


def _visual_scene_gallery(application) -> SectionContent:
    from termin.geombase import SrgbColor

    scene = tc_visual_scene_create()
    view = application.document.create_scene_view(scene)
    view.widget.stable_id = "graphics-showcase.visual-scene"
    view.widget.preferred_size = Size(1160.0, 700.0)
    view.set_scene_colors(
        SrgbColor(0.035, 0.045, 0.065, 1.0),
        SrgbColor(0.09, 0.12, 0.17, 0.8),
        SrgbColor(0.25, 0.32, 0.42, 0.9),
    )
    view.offset = Point(80.0, 70.0)

    cards = scene.create_group()
    cards.position = (30.0, 25.0)
    scene.create_rounded_rect(
        (0.0, 0.0, 300.0, 190.0),
        22.0,
        SrgbColor(0.08, 0.16, 0.29, 1.0),
        SrgbColor(0.25, 0.62, 1.0, 1.0),
        3.0,
        cards,
    )
    scene.create_text(
        "Retained hierarchy",
        (24.0, 38.0),
        22.0,
        SrgbColor(0.92, 0.96, 1.0, 1.0),
        (20.0, 12.0, 250.0, 40.0),
        cards,
    )
    child = scene.create_group(cards)
    child.set_transform(0.94, 0.22, -0.22, 0.94, 75.0, 78.0)
    scene.create_rect(
        (0.0, 0.0, 150.0, 72.0),
        SrgbColor(0.16, 0.55, 0.95, 0.9),
        SrgbColor(0.72, 0.88, 1.0, 1.0),
        2.0,
        child,
    )
    scene.create_ellipse(
        (105.0, -22.0, 84.0, 84.0),
        SrgbColor(1.0, 0.46, 0.23, 0.88),
        SrgbColor(1.0, 0.82, 0.52, 1.0),
        3.0,
        child,
    )

    waves = scene.create_group()
    waves.position = (390.0, 40.0)
    scene.create_text(
        "Polyline geometry",
        (0.0, 22.0),
        20.0,
        SrgbColor(0.90, 0.95, 1.0, 1.0),
        (0.0, 0.0, 280.0, 36.0),
        waves,
    )
    for index, color in enumerate(
        (
            SrgbColor(0.25, 0.78, 1.0, 1.0),
            SrgbColor(0.55, 0.92, 0.48, 1.0),
            SrgbColor(1.0, 0.62, 0.24, 1.0),
        )
    ):
        points = [
            (float(x), 92.0 + index * 52.0 + 24.0 * np.sin(x * 0.035 + index))
            for x in range(0, 330, 10)
        ]
        scene.create_polyline(points, color, 3.0 + index, False, waves)

    layering = scene.create_group()
    layering.position = (115.0, 330.0)
    scene.create_text(
        "Z-order · opacity · transforms",
        (0.0, 25.0),
        20.0,
        SrgbColor(0.90, 0.95, 1.0, 1.0),
        (0.0, 0.0, 380.0, 36.0),
        layering,
    )
    for index in range(7):
        tile = scene.create_rounded_rect(
            (0.0, 0.0, 150.0, 96.0),
            18.0,
            SrgbColor(0.18 + index * 0.08, 0.35, 0.78 - index * 0.06, 1.0),
            SrgbColor(0.8, 0.9, 1.0, 0.8),
            2.0,
            layering,
        )
        tile.position = (index * 58.0, 70.0 + index * 15.0)
        tile.opacity = 0.46 + index * 0.08
        tile.z_order = index

    hit = scene.create_hit_region_rect((720.0, 330.0, 220.0, 150.0))
    hit.enabled = True
    scene.create_rounded_rect(
        (720.0, 330.0, 220.0, 150.0),
        18.0,
        SrgbColor(0.10, 0.30, 0.20, 0.88),
        SrgbColor(0.35, 0.95, 0.62, 1.0),
        3.0,
    )
    scene.create_text(
        "Invisible hit region\nshares these bounds",
        (746.0, 390.0),
        18.0,
        SrgbColor(0.84, 1.0, 0.90, 1.0),
        (735.0, 350.0, 190.0, 90.0),
    )

    if not application.document.add_root(view.widget.handle):
        tc_visual_scene_destroy(scene)
        raise RuntimeError("failed to add visual-scene showcase root")

    def cleanup() -> None:
        view.set_pointer_handler(None)
        tc_visual_scene_destroy(scene)

    return SectionContent(
        root=view.widget,
        cleanup=cleanup,
        facts={
            "items": scene.size,
            "hierarchy": len(cards.children),
            "retained_types": sorted({item.type_name for item in scene.items}),
            "hit_region": hit.type_name,
        },
    )


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
        root=view.root,
        cleanup=view.close,
        facts={"nodes": len(graph.nodes), "edges": len(graph.edges), "visual_scene": True},
    )


def _visual_scene3d_widget(application) -> SectionContent:
    from tcbase import MouseButton
    from tcbase._geom_native import LinearColor
    from termin.geombase import Mat44, SrgbColor, Vec3
    from termin.gui_native import PointerEventType, SceneView3DCamera
    from tgfx import PointCloudStyle

    scene = tc_visual_scene3d_create()
    view = application.document.create_scene_view3d(scene)
    view.widget.stable_id = "graphics-showcase.visual-scene3d"
    view.widget.preferred_size = Size(1160.0, 700.0)
    view.set_clear_color(LinearColor(0.018, 0.026, 0.045, 1.0))

    cube_vertices = (
        (-1.0, -1.0, -1.0),
        (1.0, -1.0, -1.0),
        (1.0, 1.0, -1.0),
        (-1.0, 1.0, -1.0),
        (-1.0, -1.0, 1.0),
        (1.0, -1.0, 1.0),
        (1.0, 1.0, 1.0),
        (-1.0, 1.0, 1.0),
    )
    cube_triangles = (
        (0, 2, 1), (0, 3, 2),
        (4, 5, 6), (4, 6, 7),
        (0, 1, 5), (0, 5, 4),
        (1, 2, 6), (1, 6, 5),
        (2, 3, 7), (2, 7, 6),
        (3, 0, 4), (3, 4, 7),
    )
    cube_parts = tuple(face for face in range(1, 7) for _ in range(2))
    base_colors = (
        SrgbColor(0.16, 0.46, 0.95, 1.0),
        SrgbColor(0.22, 0.74, 1.0, 1.0),
        SrgbColor(0.16, 0.88, 0.68, 1.0),
        SrgbColor(0.12, 0.56, 0.55, 1.0),
        SrgbColor(0.78, 0.36, 0.96, 1.0),
        SrgbColor(1.0, 0.44, 0.62, 1.0),
        SrgbColor(1.0, 0.72, 0.28, 1.0),
        SrgbColor(0.56, 0.36, 0.96, 1.0),
    )
    selected_colors = tuple(
        SrgbColor(1.0, 0.48 + 0.04 * index, 0.16, 1.0)
        for index in range(len(cube_vertices))
    )
    cube = scene.create_primitive(
        cube_vertices,
        cube_triangles,
        colors=base_colors,
        triangle_parts=cube_parts,
    )

    floor = scene.create_primitive(
        ((-4.5, -4.5, -1.05), (4.5, -4.5, -1.05), (4.5, 4.5, -1.05), (-4.5, 4.5, -1.05)),
        ((0, 2, 1), (0, 3, 2)),
        colors=(SrgbColor(0.08, 0.13, 0.22, 1.0),) * 4,
    )
    floor.enabled = False

    cloud_style = PointCloudStyle()
    cloud_style.size_px = 8.0
    cloud_points = [
        (
            2.5 * math.cos(index * math.tau / 32.0),
            2.5 * math.sin(index * math.tau / 32.0),
            -0.4 + 0.8 * (index % 5) / 4.0,
        )
        for index in range(32)
    ]
    cloud = scene.create_point_cloud(
        cloud_points,
        colors=[SrgbColor(0.25, 0.82, 1.0, 1.0)] * len(cloud_points),
        style=cloud_style,
        pick_radius=0.12,
    )
    cloud.enabled = False

    camera_state = {
        "azimuth": -0.78,
        "elevation": 0.52,
        "distance": 7.2,
        "dragging": False,
        "last_x": 0.0,
        "last_y": 0.0,
    }

    def camera_provider(size):
        if size.width <= 0 or size.height <= 0:
            return None
        azimuth = camera_state["azimuth"]
        elevation = camera_state["elevation"]
        distance = camera_state["distance"]
        horizontal = distance * math.cos(elevation)
        eye = Vec3(
            horizontal * math.cos(azimuth),
            horizontal * math.sin(azimuth),
            distance * math.sin(elevation),
        )
        return SceneView3DCamera(
            Mat44.look_at(eye, Vec3(0.0, 0.0, 0.0)),
            Mat44.perspective(math.radians(48.0), size.width / size.height, 0.05, 100.0),
            eye,
        )

    def update_camera() -> None:
        view.invalidate_view()
        application.request_repaint()

    def fallback_pointer(event, _ray) -> bool:
        if event.type == PointerEventType.Wheel:
            camera_state["distance"] = min(
                14.0,
                max(3.0, camera_state["distance"] * math.exp(event.wheel_y * 0.12)),
            )
            update_camera()
            return True
        if event.type == PointerEventType.Down and event.button == MouseButton.LEFT.value:
            camera_state["dragging"] = True
            camera_state["last_x"] = event.x
            camera_state["last_y"] = event.y
            return True
        if event.type == PointerEventType.Move and camera_state["dragging"]:
            dx = event.x - camera_state["last_x"]
            dy = event.y - camera_state["last_y"]
            camera_state["last_x"] = event.x
            camera_state["last_y"] = event.y
            camera_state["azimuth"] -= dx * 0.008
            camera_state["elevation"] = min(
                1.35,
                max(-1.2, camera_state["elevation"] + dy * 0.008),
            )
            update_camera()
            return True
        if event.type in (PointerEventType.Up, PointerEventType.Cancel) and camera_state["dragging"]:
            camera_state["dragging"] = False
            return True
        return False

    interaction_state = {"clicks": 0, "last_part": 0}

    def activate_cube(part: int, _action: str) -> None:
        interaction_state["clicks"] += 1
        interaction_state["last_part"] = part
        colors = selected_colors if interaction_state["clicks"] % 2 else base_colors
        cube.set_geometry(cube_vertices, cube_triangles, colors=colors, triangle_parts=cube_parts)
        view.invalidate_scene()
        application.request_repaint()

    view.set_camera_provider(camera_provider)
    view.set_fallback_pointer_handler(fallback_pointer)
    view.set_action_handler(cube.handle, activate_cube)
    if not application.document.add_root(view.widget.handle):
        view.detach_scene()
        tc_visual_scene3d_destroy(scene)
        raise RuntimeError("failed to add SceneView3D showcase root")

    def cleanup() -> None:
        view.set_action_handler(cube.handle, None)
        view.set_fallback_pointer_handler(None)
        view.set_camera_provider(None)
        view.detach_scene()
        tc_visual_scene3d_destroy(scene)

    return SectionContent(
        root=view.widget,
        cleanup=cleanup,
        facts={
            "retained_items": scene.size,
            "render_protocols": ["primitive", "point-cloud"],
            "camera": "provider+fallback-orbit",
            "interaction": "cube-face-action",
            "cube_parts": 6,
        },
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
        root=view.root,
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
            name="graphics_lines",
            description="Mesh-expanded line caps, joins, widths and 3D polylines",
            capabilities=("termin-graphics", "line-mesh", "caps", "joins"),
            build=_graphics_line_gallery,
        ),
        Section(
            name="tcplot_sine",
            description="Sine, cosine and damped-sine line families",
            capabilities=("tcplot", "plot2d", "multi-line", "axes"),
            build=_tcplot_sine,
        ),
        Section(
            name="tcplot_scatter",
            description="Three deterministic scatter clusters and a trend line",
            capabilities=("tcplot", "plot2d", "scatter", "line"),
            build=_tcplot_scatter,
        ),
        Section(
            name="tcplot_multi",
            description="Polynomial and damped-oscillation plots side by side",
            capabilities=("tcplot", "plot2d", "layout", "multiple-plots"),
            build=_tcplot_multi,
        ),
        Section(
            name="tcplot_marker",
            description="Draggable retained marker with nearest-sample snapping",
            capabilities=("tcplot", "plot2d", "annotation", "interaction"),
            build=_tcplot_marker,
        ),
        Section(
            name="tcplot_helix",
            description="Double helix lines with a deterministic 3D point cloud",
            capabilities=("tcplot", "plot3d", "line", "scatter"),
            build=_tcplot_helix,
        ),
        Section(
            name="tcplot_surface",
            description="Sinc surface with Viridis mapping, wireframe and z scaling",
            capabilities=("tcplot", "plot3d", "surface", "colormap", "wireframe"),
            build=_tcplot_surface,
        ),
        Section(
            name="visual_scene_gallery",
            description="Retained primitives, hierarchy, transforms, opacity and hit regions",
            capabilities=("termin-visual-scene", "retained-2d", "transforms", "hit-test"),
            build=_visual_scene_gallery,
        ),
        Section(
            name="visual_scene_nodegraph",
            description="Visual-scene primitives projected as a native node graph",
            capabilities=("termin-visual-scene", "termin-nodegraph", "interaction"),
            build=_visual_scene_nodegraph,
        ),
        Section(
            name="visual_scene3d_widget",
            description="Interactive retained 3D scene embedded as a native widget",
            capabilities=("termin-visual-scene", "termin-gui-native", "scene3d", "interaction"),
            build=_visual_scene3d_widget,
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
