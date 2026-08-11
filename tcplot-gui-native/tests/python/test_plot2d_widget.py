import numpy as np

from tcnodegraph import (
    Graph,
    GraphController,
    NodeBodyContent,
    NodeBodyLayout,
    build_native_node_graph_view,
)
from tcplot_gui_native import Plot2D
from termin.gui_native import OffscreenGuiComposition


def test_plot2d_widget_renders_and_updates_inside_native_nodegraph_body():
    application = OffscreenGuiComposition(
        width=720,
        height=520,
        continuous_rendering=False,
    )
    document = application.document
    graph = Graph()
    controller = GraphController(graph)
    node = controller.create_node("plot", title="Signal", x=-220.0, y=-160.0)
    plots = {}

    def body_provider(body_document, candidate):
        if candidate.id != node.id:
            return None
        plot = Plot2D(body_document)
        x = np.linspace(0.0, 2.0 * np.pi, 240)
        plot.plot(x, np.sin(x), thickness=2.0)
        plot.set_axis_labels("time", "value")
        plots[candidate.id] = plot
        return NodeBodyContent(plot.widget, NodeBodyLayout(height=240.0))

    view = build_native_node_graph_view(
        document,
        graph,
        request_render=application.request_repaint,
        controller=controller,
        body_content_provider=body_provider,
    )
    assert document.add_root(view.root.handle)
    assert application.render_frame()
    assert plots[node.id].line_count == 1

    x = np.linspace(0.0, 2.0 * np.pi, 120)
    plots[node.id].set_line_data(0, x, np.cos(x))
    assert application.render_frame()

    pixels = application.read_frame_rgba_float()
    clear = np.array([0.03, 0.035, 0.045], dtype=np.float32)
    changed = np.max(np.abs(pixels[..., :3] - clear), axis=-1) > 0.04
    assert np.count_nonzero(changed) > 1000

    view.close()
    application.close()
