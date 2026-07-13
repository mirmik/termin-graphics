from __future__ import annotations

import unittest

from tcnodegraph import Graph, GraphController


class ControllerTests(unittest.TestCase):
    def test_connect_with_type_validation(self):
        g = Graph()
        c = GraphController(g)
        n1 = c.create_node("A")
        n2 = c.create_node("B")
        c.add_output_socket(n1.id, "out", "fbo")
        c.add_input_socket(n2.id, "inp", "fbo")

        res = c.connect(n1.id, "out", n2.id, "inp")
        self.assertTrue(res.ok)
        self.assertEqual(len(g.edges), 1)

    def test_connect_rejects_mismatch(self):
        g = Graph()
        c = GraphController(g)
        n1 = c.create_node("A")
        n2 = c.create_node("B")
        c.add_output_socket(n1.id, "out", "shadow")
        c.add_input_socket(n2.id, "inp", "fbo")

        res = c.connect(n1.id, "out", n2.id, "inp")
        self.assertFalse(res.ok)
        self.assertEqual(res.reason, "type mismatch")
        self.assertEqual(len(g.edges), 0)

    def test_single_input_drops_previous_edge(self):
        g = Graph()
        c = GraphController(g)
        n1 = c.create_node("N1")
        n2 = c.create_node("N2")
        dst = c.create_node("DST")
        c.add_output_socket(n1.id, "out", "fbo")
        c.add_output_socket(n2.id, "out", "fbo")
        c.add_input_socket(dst.id, "inp", "fbo", multi=False)

        self.assertTrue(c.connect(n1.id, "out", dst.id, "inp").ok)
        self.assertEqual(len(g.edges), 1)
        self.assertTrue(c.connect(n2.id, "out", dst.id, "inp").ok)
        self.assertEqual(len(g.edges), 1)
        edge = next(iter(g.edges.values()))
        self.assertEqual(edge.src_node_id, n2.id)

    def test_duplicate_ids_and_failed_connect_do_not_mutate_graph(self):
        g = Graph()
        c = GraphController(g)
        source = c.create_node("Source", node_id="source")
        target = c.create_node("Target", node_id="target")
        c.add_output_socket(source.id, "out")
        c.add_input_socket(target.id, "in", multi=False)
        first = c.connect(source.id, "out", target.id, "in", edge_id="edge")
        self.assertTrue(first.ok)

        with self.assertRaisesRegex(ValueError, "duplicate node id"):
            c.create_node("Replacement", node_id="source")
        duplicate = c.connect(source.id, "out", target.id, "in", edge_id="edge")

        self.assertFalse(duplicate.ok)
        self.assertEqual(duplicate.reason, "duplicate edge id")
        self.assertEqual(set(g.nodes), {"source", "target"})
        self.assertEqual(set(g.edges), {"edge"})


if __name__ == "__main__":
    unittest.main()
