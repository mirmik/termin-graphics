from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from tcnodegraph import (
    Graph,
    GraphController,
    graph_from_dict,
    graph_to_dict,
    load_graph_json,
    save_graph_json,
)


class IoTests(unittest.TestCase):
    def test_roundtrip_json(self):
        g = Graph()
        g.data = {"layout": {"selection": ["node_1"]}}
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
        self.assertEqual(g2.data, g.data)

    def test_dict_roundtrip_does_not_alias_nested_metadata(self):
        source = {
            "data": {"layout": {"selection": ["node"]}},
            "nodes": [],
            "edges": [],
            "groups": [],
        }

        graph = graph_from_dict(source)
        updated_data = graph.data
        updated_data["layout"]["selection"].append("other")
        GraphController(graph).set_graph_data(updated_data)
        self.assertEqual(source["data"]["layout"]["selection"], ["node"])

        serialized = graph_to_dict(graph)
        serialized["data"]["layout"]["selection"].append("serialized")
        self.assertEqual(graph.data["layout"]["selection"], ["node", "other"])

    def test_load_rejects_duplicate_ids_and_dangling_endpoints(self):
        valid_node = {
            "id": "node",
            "kind": "Test",
            "title": "Test",
            "inputs": [{"name": "in"}],
            "outputs": [{"name": "out"}],
        }
        invalid_graphs = [
            {"nodes": [valid_node, valid_node], "edges": [], "groups": []},
            {
                "nodes": [
                    {
                        **valid_node,
                        "inputs": [{"name": "in"}, {"name": "in"}],
                    }
                ],
                "edges": [],
                "groups": [],
            },
            {
                "nodes": [valid_node],
                "edges": [
                    {
                        "id": "edge",
                        "src_node_id": "node",
                        "src_socket": "missing",
                        "dst_node_id": "node",
                        "dst_socket": "in",
                    }
                ],
                "groups": [],
            },
            {
                "nodes": [valid_node],
                "edges": [
                    {
                        "id": "edge",
                        "src_node_id": "node",
                        "src_socket": "out",
                        "dst_node_id": "missing",
                        "dst_socket": "in",
                    }
                ],
                "groups": [],
            },
        ]

        for data in invalid_graphs:
            with self.assertRaises(ValueError):
                graph_from_dict(data)

    def test_load_rejects_semantically_invalid_edges_with_diagnostics(self):
        def node(
            node_id: str,
            *,
            input_type: str = "color",
            output_type: str = "color",
            input_multi: bool = False,
            output_multi: bool = True,
        ) -> dict:
            return {
                "id": node_id,
                "kind": "Test",
                "inputs": [
                    {"name": "in", "socket_type": input_type, "multi": input_multi}
                ],
                "outputs": [
                    {
                        "name": "out",
                        "socket_type": output_type,
                        "multi": output_multi,
                    }
                ],
            }

        def edge(edge_id: str, source: str, target: str) -> dict:
            return {
                "id": edge_id,
                "src_node_id": source,
                "src_socket": "out",
                "dst_node_id": target,
                "dst_socket": "in",
            }

        invalid_graphs = [
            {
                "nodes": [node("same")],
                "edges": [edge("self", "same", "same")],
                "groups": [],
            },
            {
                "nodes": [node("source"), node("target", input_type="depth")],
                "edges": [edge("mismatch", "source", "target")],
                "groups": [],
            },
            {
                "nodes": [node("a"), node("b"), node("target")],
                "edges": [edge("first", "a", "target"), edge("second", "b", "target")],
                "groups": [],
            },
            {
                "nodes": [
                    node("source", output_multi=False),
                    node("a"),
                    node("b"),
                ],
                "edges": [edge("first", "source", "a"), edge("second", "source", "b")],
                "groups": [],
            },
        ]

        for data in invalid_graphs:
            with self.subTest(data=data), self.assertLogs(
                "tcnodegraph.io", level="ERROR"
            ) as logs, self.assertRaises(ValueError):
                graph_from_dict(data)
            self.assertIn("rejected invalid graph", "\n".join(logs.output))


if __name__ == "__main__":
    unittest.main()
