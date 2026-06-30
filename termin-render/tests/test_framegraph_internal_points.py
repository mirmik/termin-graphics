from __future__ import annotations

from termin.render_framework.python_pass import PythonFramePass

from framegraph_test_helpers import DummyFramePass
from framegraph_test_helpers import build_alias_groups, build_schedule


class DummyPass(DummyFramePass):
    def __init__(self, name: str, reads=None, writes=None, inplace: bool = False):
        super().__init__(
            name,
            reads=reads,
            writes=writes,
            inplace=inplace,
            internal_symbols=["a", "b", "c"],
        )


def test_framepass_has_no_internal_symbols_by_default():
    p = PythonFramePass(pass_name="simple")
    assert p.get_internal_symbols() == []


def test_debug_internal_point_configuration_is_mutable():
    p = DummyPass(name="p", reads={"in"}, writes={"out"})
    # по умолчанию точки дебага не заданы
    assert p.debug_internal_symbol is None
    assert p.get_debug_internal_point() is None

    # можно задать и прочитать конфигурацию
    p.set_debug_internal_point("foo")
    assert p.debug_internal_symbol == "foo"
    assert p.get_debug_internal_point() == "foo"

    # и сбросить её обратно
    p.set_debug_internal_point(None)
    assert p.debug_internal_symbol is None
    assert p.get_debug_internal_point() is None


def test_debugger_window_configuration():
    """Тест для set_debugger_window / get_debugger_window."""
    p = PythonFramePass(pass_name="p")

    # По умолчанию окно не задано
    assert p.get_debugger_window() is None

    # Можно задать окно и callback
    mock_window = object()
    mock_callback = lambda x: None
    p.set_debugger_window(mock_window, mock_callback)
    assert p.get_debugger_window() is mock_window
    assert p._depth_capture_callback is mock_callback

    # Можно сбросить
    p.set_debugger_window(None)
    assert p.get_debugger_window() is None


def test_framegraph_builds_with_and_without_debug_internal_point():
    """
    Граф должен:
    * корректно строиться, когда у пассов есть внутренние символы,
      но точка дебага не выбрана;
    * так же корректно строиться, когда точка дебага выбрана.
    """
    p1 = DummyPass(name="A", reads=set(), writes={"a"})
    p2 = DummyPass(name="B", reads={"a"}, writes={"b"})

    # 1) Без конфигурации точки дебага
    schedule1 = [p.pass_name for p in build_schedule([p1, p2])]
    assert schedule1 == ["A", "B"]

    groups1 = build_alias_groups([p1, p2])
    # оба ресурса независимы и имеют свои канонические имена
    assert groups1["a"] == {"a"}
    assert groups1["b"] == {"b"}

    # 2) С установленной точкой дебага на втором пассе
    p2.set_debug_internal_point("b")

    schedule2 = [p.pass_name for p in build_schedule([p1, p2])]
    # порядок выполнения не должен измениться
    assert schedule2 == ["A", "B"]

    groups2 = build_alias_groups([p1, p2])
    # основные alias-группы сохраняются
    assert groups2["a"] == {"a"}
    assert groups2["b"] == {"b"}


def test_inplace_pass_with_debug_internal_point():
    """
    Inplace-пасс с установленным debug_internal_point должен
    корректно строиться (валидация не должна упасть).
    """
    # Inplace пасс: 1 read, 1 write
    p_inplace = DummyPass(
        name="InplaceWithDebug",
        reads={"input"},
        writes={"output"},
        inplace=True,
    )
    p_inplace.set_debug_internal_point("some_symbol")

    # Проверяем, что writes остаётся как есть (debug больше не добавляется в writes)
    assert "output" in p_inplace.writes
    assert len(p_inplace.writes) == 1

    # Граф должен корректно строиться
    p_source = DummyPass(name="Source", writes={"input"})
    schedule = [p.pass_name for p in build_schedule([p_source, p_inplace])]
    assert schedule == ["Source", "InplaceWithDebug"]

    # Alias группы
    groups = build_alias_groups([p_source, p_inplace])
    # input и output — синонимы (inplace)
    assert "input" in groups


# ---- Тесты для enabled флага ----

def test_framepass_enabled_by_default():
    """По умолчанию пасс включён."""
    p = PythonFramePass(pass_name="test")
    assert p.enabled is True


def test_disabled_pass_not_in_schedule():
    """Отключённый пасс не попадает в расписание."""
    p1 = DummyPass(name="A", writes={"a"})
    p2 = DummyPass(name="B", reads={"a"}, writes={"b"})
    p3 = DummyPass(name="C", reads={"b"}, writes={"c"})

    # Все пассы включены — все в расписании
    schedule1 = [p.pass_name for p in build_schedule([p1, p2, p3])]
    assert schedule1 == ["A", "B", "C"]

    # Отключаем средний пасс
    p2.enabled = False
    schedule2 = [p.pass_name for p in build_schedule([p1, p2, p3])]
    # B отключён, его нет в расписании
    # C читает из b, но b никто не пишет — C всё равно выполнится
    # (просто получит пустой/внешний ресурс)
    assert "B" not in schedule2
    assert "A" in schedule2
    assert "C" in schedule2


def test_disabled_pass_does_not_conflict_writes():
    """
    Отключённый пасс не участвует в проверке конфликтов writes.
    Два пасса могут писать в один ресурс, если один из них отключён.
    """
    p1 = DummyPass(name="Writer1", writes={"shared"})
    p2 = DummyPass(name="Writer2", writes={"shared"})

    # Оба включены — конфликт
    import pytest

    with pytest.raises(RuntimeError):
        build_schedule([p1, p2])

    # Отключаем один — конфликта нет
    p2.enabled = False
    schedule = [p.pass_name for p in build_schedule([p1, p2])]
    assert schedule == ["Writer1"]


def test_disabled_pass_not_in_alias_groups():
    """Отключённый пасс не добавляет свои ресурсы в alias-группы."""
    p1 = DummyPass(name="A", writes={"a"})
    p2 = DummyPass(name="B", writes={"b"})
    p2.enabled = False

    groups = build_alias_groups([p1, p2])

    assert "a" in groups
    assert "b" not in groups  # B отключён, его ресурс не учитывается
