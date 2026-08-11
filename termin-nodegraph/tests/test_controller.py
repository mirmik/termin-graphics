from __future__ import annotations

import unittest

from tcnodegraph import DictSchemaProvider, Graph, GraphController, NodeTemplate


class ControllerTests(unittest.TestCase):
    def test_replace_graph_preserves_schema_and_validator(self):
        class RejectingValidator:
            def validate(self, *_args, **_kwargs):
                return False

        template = NodeTemplate(kind="Typed", title="Typed")
        schema = DictSchemaProvider({template.kind: template})
        validator = RejectingValidator()
        controller = GraphController(schema=schema, validator=validator)
        replacement = Graph()

        controller.replace_graph(replacement)

        self.assertIs(controller.graph, replacement)
        self.assertIs(controller.schema, schema)
        self.assertIs(controller.validator, validator)

    def test_rejected_implicit_connection_does_not_consume_edge_id(self):
        graph = Graph()
        controller = GraphController(graph)
        source = controller.create_node("Source")
        target = controller.create_node("Target")
        controller.add_output_socket(source.id, "out", "color")
        controller.add_input_socket(target.id, "in", "depth")

        self.assertFalse(controller.connect(source.id, "out", target.id, "in").ok)
        controller.add_input_socket(target.id, "compatible", "color")

        result = controller.connect(source.id, "out", target.id, "compatible")
        self.assertTrue(result.ok)
        self.assertEqual(result.edge_id, "edge_1")

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

    def test_single_output_drops_previous_edge(self):
        g = Graph()
        c = GraphController(g)
        source = c.create_node("Source")
        first_target = c.create_node("First")
        second_target = c.create_node("Second")
        c.add_output_socket(source.id, "out", multi=False)
        c.add_input_socket(first_target.id, "in")
        c.add_input_socket(second_target.id, "in")

        self.assertTrue(c.connect(source.id, "out", first_target.id, "in").ok)
        self.assertTrue(c.connect(source.id, "out", second_target.id, "in").ok)

        self.assertEqual(len(g.edges), 1)
        edge = next(iter(g.edges.values()))
        self.assertEqual(edge.dst_node_id, second_target.id)

    def test_rejected_connection_is_logged_and_does_not_replace_edges(self):
        g = Graph()
        c = GraphController(g)
        source = c.create_node("Source")
        other_source = c.create_node("OtherSource")
        target = c.create_node("Target")
        other_target = c.create_node("OtherTarget")
        c.add_output_socket(source.id, "out", "color", multi=False)
        c.add_output_socket(other_source.id, "out", "depth", multi=False)
        c.add_input_socket(target.id, "in", "color", multi=False)
        c.add_input_socket(other_target.id, "in", "depth", multi=False)
        self.assertTrue(c.connect(source.id, "out", target.id, "in", edge_id="first").ok)
        self.assertTrue(
            c.connect(
                other_source.id,
                "out",
                other_target.id,
                "in",
                edge_id="second",
            ).ok
        )

        with self.assertLogs("tcnodegraph.controller", level="ERROR") as logs:
            result = c.connect(
                source.id,
                "out",
                other_target.id,
                "in",
                edge_id="rejected",
            )

        self.assertFalse(result.ok)
        self.assertEqual(result.reason, "type mismatch")
        self.assertIn("type mismatch", "\n".join(logs.output))
        self.assertEqual(set(g.edges), {"first", "second"})

    def test_self_link_is_rejected_before_mutation(self):
        g = Graph()
        c = GraphController(g)
        node = c.create_node("Node")
        c.add_output_socket(node.id, "out")
        c.add_input_socket(node.id, "in")

        with self.assertLogs("tcnodegraph.controller", level="ERROR"):
            result = c.connect(node.id, "out", node.id, "in")

        self.assertFalse(result.ok)
        self.assertEqual(result.reason, "self-link")
        self.assertEqual(g.edges, {})

    def test_template_nested_defaults_are_independent(self):
        template = NodeTemplate(
            kind="Nested",
            title="Nested",
            defaults={"settings": {"levels": [1, 2]}},
        )
        c = GraphController(schema=DictSchemaProvider({template.kind: template}))

        first = c.create_node(template.kind)
        second = c.create_node(template.kind)
        first.params["settings"]["levels"].append(3)

        self.assertEqual(c.graph.nodes[first.id].params["settings"], {"levels": [1, 2]})
        self.assertEqual(second.params["settings"], {"levels": [1, 2]})
        self.assertEqual(template.defaults["settings"], {"levels": [1, 2]})

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

    def test_snapshot_update_is_transactional(self):
        graph = Graph()
        controller = GraphController(graph)
        source = controller.create_node("Source")
        target = controller.create_node("Target")
        controller.add_output_socket(source.id, "out", "color")
        controller.add_input_socket(target.id, "in", "color")
        self.assertTrue(controller.connect(source.id, "out", target.id, "in").ok)

        with self.assertRaisesRegex(ValueError, "socket not found"):
            controller.update_node(source.id, outputs=[])

        self.assertEqual([socket.name for socket in graph.nodes[source.id].outputs], ["out"])
        self.assertEqual(len(graph.edges), 1)


if __name__ == "__main__":
    unittest.main()
