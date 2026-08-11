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
from tcplot_gui_native import Plot3D
from termin.gui_native import OffscreenGuiComposition


def test_plot3d_widget_renders_inside_native_nodegraph_body():
    application = OffscreenGuiComposition(
        width=720,
        height=520,
        continuous_rendering=False,
    )
    document = application.document
    graph = Graph()
    schema = DictSchemaProvider(
        {
            "plot": NodeTemplate(
                kind="plot",
                title="3D trajectory",
                width=500.0,
                height=420.0,
            )
        }
    )
    controller = GraphController(graph, schema=schema)
    node = controller.create_node(
        "plot",
        title="3D trajectory",
        x=-250.0,
        y=-180.0,
    )
    plots = {}

    def body_provider(body_document, candidate):
        if candidate.id != node.id:
            return None
        plot = Plot3D(body_document)
        t = np.linspace(0.0, 8.0 * np.pi, 240)
        plot.plot(np.cos(t), np.sin(t), t * 0.05, thickness=2.0)
        plot.set_axis_labels("x", "y", "z")
        plot.fit_camera()
        plots[candidate.id] = plot
        return NodeBodyContent(plot.widget, NodeBodyLayout(height=300.0))

    view = build_native_node_graph_view(
        document,
        graph,
        request_render=application.request_repaint,
        controller=controller,
        body_content_provider=body_provider,
    )
    assert document.add_root(view.root.handle)
    assert application.render_frame()
    assert plots[node.id].texture_id != 0
    pixels = application.read_frame_rgba_float()
    clear = np.array([0.03, 0.035, 0.045], dtype=np.float32)
    changed = np.max(np.abs(pixels[..., :3] - clear), axis=-1) > 0.04
    assert np.count_nonzero(changed) > 1000

    widget_handle = plots[node.id].handle
    previous_anchor = view.body_items[node.id].handle
    view.rebuild()
    assert plots[node.id].handle == widget_handle
    assert view.body_items[node.id].handle != previous_anchor
    assert application.render_frame()

    view.close()
    assert not document.is_alive(widget_handle)
    application.close()


def test_plot3d_surface_accepts_read_only_numpy_data():
    application = OffscreenGuiComposition(
        width=480,
        height=360,
        continuous_rendering=False,
    )
    plot = Plot3D(application.document)
    coordinates = np.linspace(-2.0, 2.0, 24)
    x, y = np.meshgrid(coordinates, coordinates)
    z = np.sin(x) * np.cos(y)
    z.setflags(write=False)
    try:
        item = plot.surface(x, y, z)
        assert item.scene_id == plot.scene_id
        assert plot.item_count >= 1
    finally:
        application.document.destroy_widget_recursive(plot.handle)
        application.close()
