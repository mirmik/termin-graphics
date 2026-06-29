# utest/framegraph_test.py

import unittest
from typing import List, Tuple

from termin.render_framework.frame_graph_view import PipelineFrameGraphView
from termin.render_framework.python_pass import PythonFramePass
from termin.render_framework import RenderPipeline


def build_schedule(passes):
    pipeline = RenderPipeline("test")
    for frame_pass in passes:
        pipeline.add_pass(frame_pass)
    with PipelineFrameGraphView(pipeline) as graph:
        return graph.schedule()


class DummyPass(PythonFramePass):
    """Тестовый пасс без реального исполнения."""

    def __init__(self, pass_name, reads=None, writes=None, inplace=False):
        super().__init__(pass_name=pass_name)
        self._reads = set(reads or [])
        self._writes = set(writes or [])
        # Для inplace храним явную пару алиасов
        self._inplace = inplace
        if inplace and reads and writes:
            # Для простоты берём первый read и первый write
            self._inplace_src = list(reads)[0] if reads else None
            self._inplace_dst = list(writes)[0] if writes else None
        else:
            self._inplace_src = None
            self._inplace_dst = None

    def compute_reads(self):
        return self._reads

    def compute_writes(self):
        return self._writes

    def get_inplace_aliases(self) -> List[Tuple[str, str]]:
        if self._inplace and self._inplace_src and self._inplace_dst:
            return [(self._inplace_src, self._inplace_dst)]
        return []

    def execute(self, *args, **kwargs):
        # В тестах ничего не делаем
        return


class FrameGraphTests(unittest.TestCase):
    def test_simple_linear_chain(self):
        # A -> B -> C
        a = DummyPass("A", writes={"g1"})
        b = DummyPass("B", reads={"g1"}, writes={"g2"})
        c = DummyPass("C", reads={"g2"})

        schedule = build_schedule([a, b, c])
        names = [p.pass_name for p in schedule]

        self.assertEqual(set(names), {"A", "B", "C"})
        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("B"), names.index("C"))

    def test_branching(self):
        #   A
        #  / \
        # B   C
        #  \ /
        #   D
        a = DummyPass("A", writes={"r1"})
        b = DummyPass("B", reads={"r1"}, writes={"r2"})
        c = DummyPass("C", reads={"r1"}, writes={"r3"})
        d = DummyPass("D", reads={"r2", "r3"})

        schedule = build_schedule([a, b, c, d])
        names = [p.pass_name for p in schedule]

        # A должен быть до B и C
        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("A"), names.index("C"))
        # B и C должны быть до D
        self.assertLess(names.index("B"), names.index("D"))
        self.assertLess(names.index("C"), names.index("D"))

    def test_external_resource(self):
        # Ресурс, который никто не пишет, но кто-то читает — считаем внешним входом
        a = DummyPass("A", reads={"external_x"}, writes={"r1"})
        b = DummyPass("B", reads={"r1"})

        schedule = build_schedule([a, b])
        names = [p.pass_name for p in schedule]

        self.assertLess(names.index("A"), names.index("B"))
        # Никаких исключений не бросается

    def test_multi_writer_error(self):
        # Один и тот же ресурс пишет два пасса — это запрещаем.
        a = DummyPass("A", writes={"r1"})
        b = DummyPass("B", writes={"r1"})

        with self.assertRaises(RuntimeError):
            build_schedule([a, b])

    def test_cycle_detection(self):
        # A пишет r1, читает r2
        # B пишет r2, читает r1
        a = DummyPass("A", reads={"r2"}, writes={"r1"})
        b = DummyPass("B", reads={"r1"}, writes={"r2"})

        with self.assertRaises(RuntimeError):
            build_schedule([a, b])

    def test_pass_without_resources(self):
        # Пассы без reads/writes ни от чего не зависят
        a = DummyPass("A")
        b = DummyPass("B")
        c = DummyPass("C")

        schedule = build_schedule([a, b, c])
        # Любой порядок валиден, главное — все на месте
        names = [p.pass_name for p in schedule]
        self.assertEqual(set(names), {"A", "B", "C"})

    def test_empty_graph(self):
        schedule = build_schedule([])
        self.assertEqual(schedule, [])
    
    def test_inplace_d_last(self):
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"x"})
        c = DummyPass("C", reads={"r"}, writes={"y"})
        d = DummyPass("D", reads={"r"}, writes={"r_mod"}, inplace=True)

        schedule = build_schedule([a, b, c, d])

        names = [p.pass_name for p in schedule]
        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("A"), names.index("C"))
        self.assertLess(names.index("A"), names.index("D"))
        self.assertGreater(names.index("D"), names.index("B"))
        self.assertGreater(names.index("D"), names.index("C"))

    def test_inplace_d_last_another_order(self):
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"x"})
        c = DummyPass("C", reads={"r"}, writes={"y"})
        d = DummyPass("D", reads={"r"}, writes={"r_mod"}, inplace=True)

        schedule = build_schedule([a, d, c, b])

        names = [p.pass_name for p in schedule]
        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("A"), names.index("C"))
        self.assertLess(names.index("A"), names.index("D"))
        self.assertGreater(names.index("D"), names.index("B"))
        self.assertGreater(names.index("D"), names.index("C"))

    def test_inplace_chain(self):
        a = DummyPass("A", writes={"a"})
        b = DummyPass("B", reads={"a"}, writes={"b"}, inplace=True)
        c = DummyPass("C", reads={"b"}, writes={"c"}, inplace=True)
        d = DummyPass("D", reads={"c"}, writes={"d"}, inplace=True)

        schedule = build_schedule([a, b, c, d])

        names = [p.pass_name for p in schedule]
        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("B"), names.index("C"))
        self.assertLess(names.index("C"), names.index("D"))

    
    def test_inplace_must_fault(self):
        a = DummyPass("A", writes={"a"})
        b = DummyPass("B", reads={"a"}, writes={"b"}, inplace=True)
        c = DummyPass("C", reads={"b"}, writes={"c"}, inplace=True)
        d = DummyPass("D", reads={"c"}, writes={"d"}, inplace=True)
        e = DummyPass("E", reads={"b"}, writes={"e"}, inplace=True)

        with self.assertRaises(RuntimeError):
            build_schedule([a, b, c, d, e])

    def test_inplace_after_all_readers(self):
        """
        A пишет r,
        B и C читают r,
        D inplace читает r и пишет r_mod.

        Ожидаем:
        - A перед B, C, D;
        - D после B и C (т.е. сначала все читатели исходного состояния ресурса, потом модификатор).
        """
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"x"})
        c = DummyPass("C", reads={"r"}, writes={"y"})
        d = DummyPass("D", reads={"r"}, writes={"r_mod"}, inplace=True)

        schedule = build_schedule([a, b, c, d])
        names = [p.pass_name for p in schedule]

        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("A"), names.index("C"))
        self.assertLess(names.index("A"), names.index("D"))

        # D (модифицирующий) должен быть самым поздним потребителем r
        self.assertGreater(names.index("D"), names.index("B"))
        self.assertGreater(names.index("D"), names.index("C"))

    def test_inplace_with_readers_of_modified_resource(self):
        """
        A пишет r,
        B inplace: r -> r_mod,
        C и D читают r_mod.

        Здесь важно, что:
        - B идёт после A (по зависимости),
        - C и D идут после B (потребляют модифицированное состояние).
        """
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"r_mod"}, inplace=True)
        c = DummyPass("C", reads={"r_mod"})
        d = DummyPass("D", reads={"r_mod"})

        schedule = build_schedule([a, b, c, d])
        names = [p.pass_name for p in schedule]

        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("B"), names.index("C"))
        self.assertLess(names.index("B"), names.index("D"))

    def test_inplace_between_two_readers_on_same_resource(self):
        """
        A пишет r,
        B читает r,
        C inplace: r -> r_mod,
        D читает r_mod,
        E читает r.

        Ожидаем:
        - A перед всеми;
        - C (inplace) после всех читателей r (B и E),
          но до D, который потребляет r_mod.
        """
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"})
        c = DummyPass("C", reads={"r"}, writes={"r_mod"}, inplace=True)
        d = DummyPass("D", reads={"r_mod"})
        e = DummyPass("E", reads={"r"})

        schedule = build_schedule([a, b, c, d, e])
        names = [p.pass_name for p in schedule]

        self.assertLess(names.index("A"), names.index("B"))
        self.assertLess(names.index("A"), names.index("C"))
        self.assertLess(names.index("A"), names.index("D"))
        self.assertLess(names.index("A"), names.index("E"))

        # C должен быть самым поздним читателем r
        self.assertGreater(names.index("C"), names.index("B"))
        self.assertGreater(names.index("C"), names.index("E"))

        # D уже читает r_mod, поэтому после C
        self.assertLess(names.index("C"), names.index("D"))

    def test_inplace_from_external_resource(self):
        """
        Внешний ресурс ext никто не пишет, но inplace-пасс его читает и пишет новый ресурс r.

        Это должно считаться валидным (ext — внешний вход),
        главное, чтобы порядок зависимости ext->r не ломал граф.
        """
        a = DummyPass("A", reads={"ext"}, writes={"r"}, inplace=True)
        b = DummyPass("B", reads={"r"})

        schedule = build_schedule([a, b])
        names = [p.pass_name for p in schedule]

        # Просто проверяем, что порядок зависимостей соблюдён
        self.assertLess(names.index("A"), names.index("B"))

    def test_inplace_and_normal_writer_conflict(self):
        """
        Проверяем конфликт между обычным writer и inplace-writerом,
        пишущими одно и то же имя ресурса.
        """
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"r"}, inplace=True)  # пишет тот же ресурс

        with self.assertRaises(RuntimeError):
            build_schedule([a, b])

    def test_cycle_with_inplace(self):
        """
        A inplace: читает r, пишет r2
        B: читает r2, пишет r

        Это явный цикл по данным, его должен поймать детектор циклов.
        """
        a = DummyPass("A", reads={"r"}, writes={"r2"}, inplace=True)
        b = DummyPass("B", reads={"r2"}, writes={"r"})

        with self.assertRaises(RuntimeError):
            build_schedule([a, b])

    def test_two_inplace_on_same_input_must_fail(self):
        """
        Два inplace пасса, висящих на одном и том же входном ресурсе, — запрещены.

        A пишет r
        B inplace: r -> r1
        C inplace: r -> r2   (второй модификатор того же источника)
        """
        a = DummyPass("A", writes={"r"})
        b = DummyPass("B", reads={"r"}, writes={"r1"}, inplace=True)
        c = DummyPass("C", reads={"r"}, writes={"r2"}, inplace=True)

        with self.assertRaises(RuntimeError):
            build_schedule([a, b, c])


    def test_frame_debugger_before_inplace_color_pass(self):
        """
        Реальный сценарий из framegraph debugger:

        SkyboxPass пишет skybox
        ColorPass читает skybox, пишет color_scene (inplace — один FBO)
        FrameDebugger читает skybox

        Проблема: ColorPass портит skybox при выполнении.
        Решение: FrameDebugger должен читать skybox ДО ColorPass.

        Ожидаемый порядок: Skybox -> FrameDebugger -> Color
        """
        skybox = DummyPass("Skybox", writes={"skybox"})
        color = DummyPass("Color", reads={"skybox"}, writes={"color_scene"}, inplace=True)
        debugger = DummyPass("FrameDebugger", reads={"skybox"})

        schedule = build_schedule([skybox, color, debugger])
        names = [p.pass_name for p in schedule]

        # Skybox должен быть первым (пишет ресурс)
        self.assertEqual(names[0], "Skybox")

        # FrameDebugger должен быть ДО Color (читает skybox до того, как Color его испортит)
        self.assertLess(names.index("FrameDebugger"), names.index("Color"))

    def test_frame_debugger_order_independent_of_pass_list_order(self):
        """
        Порядок пассов в списке не должен влиять на результат.
        FrameDebugger должен быть до ColorPass независимо от порядка добавления.
        """
        skybox = DummyPass("Skybox", writes={"skybox"})
        color = DummyPass("Color", reads={"skybox"}, writes={"color_scene"}, inplace=True)
        debugger = DummyPass("FrameDebugger", reads={"skybox"})

        # Порядок 1: debugger в конце
        names1 = [p.pass_name for p in build_schedule([skybox, color, debugger])]
        self.assertLess(names1.index("FrameDebugger"), names1.index("Color"))

        # Порядок 2: debugger перед color
        names2 = [p.pass_name for p in build_schedule([skybox, debugger, color])]
        self.assertLess(names2.index("FrameDebugger"), names2.index("Color"))

        # Порядок 3: debugger первый после skybox
        names3 = [p.pass_name for p in build_schedule([debugger, skybox, color])]
        self.assertLess(names3.index("FrameDebugger"), names3.index("Color"))


if __name__ == "__main__":
    unittest.main()
