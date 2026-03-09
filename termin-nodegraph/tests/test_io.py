from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tcnodegraph import Graph, GraphController, load_graph_json, save_graph_json


class IoTests(unittest.TestCase):
    def test_roundtrip_json(self):
        g = Graph()
        c = GraphController(g)
        n1 = c.create_node("ColorPass", x=12, y=34)
        n2 = c.create_node("BloomPass", x=300, y=40)
        c.add_output_socket(n1.id, "output_res", "fbo")
        c.add_input_socket(n2.id, "input_res", "fbo")
        c.set_node_param(n1.id, "quality", 2)
        self.assertTrue(c.connect(n1.id, "output_res", n2.id, "input_res").ok)
        c.add_group("Viewport", 0, 0, 500, 300)

        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "graph.json"
            save_graph_json(g, p)
            g2 = load_graph_json(p)

        self.assertEqual(set(g2.nodes.keys()), set(g.nodes.keys()))
        self.assertEqual(set(g2.edges.keys()), set(g.edges.keys()))
        self.assertEqual(set(g2.groups.keys()), set(g.groups.keys()))
        self.assertEqual(g2.nodes[n1.id].params.get("quality"), 2)


if __name__ == "__main__":
    unittest.main()
