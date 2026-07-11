from __future__ import annotations

from typing import Iterable

from termin.render_framework import RenderPipeline
from termin.render_framework.frame_graph_view import PipelineFrameGraphView
from termin.render_framework.python_pass import PythonFramePass


def _resource_set(values: Iterable[str] | None) -> set[str]:
    return set(values or ())


def build_pipeline(passes: Iterable[PythonFramePass]) -> RenderPipeline:
    pipeline = RenderPipeline("test")
    for frame_pass in passes:
        pipeline.add_pass(frame_pass)
    return pipeline


def build_schedule(passes: Iterable[PythonFramePass]) -> list[PythonFramePass]:
    pipeline = build_pipeline(passes)
    graph = PipelineFrameGraphView(pipeline)
    try:
        with graph:
            return graph.schedule()
    finally:
        # RenderPipeline is deliberately a non-owning handle wrapper. Destroy
        # the temporary pool object explicitly so its passes become unowned
        # before a test builds another temporary pipeline from the same doubles.
        graph.close()
        pipeline.destroy()


def build_alias_groups(passes: Iterable[PythonFramePass]) -> dict[str, set[str]]:
    pipeline = build_pipeline(passes)
    graph = PipelineFrameGraphView(pipeline)
    try:
        with graph:
            graph.schedule()
            return {
                canonical: set(aliases)
                for canonical, aliases in graph.alias_groups().items()
            }
    finally:
        graph.close()
        pipeline.destroy()


class DummyFramePass(PythonFramePass):
    """Small PythonFramePass double for framegraph scheduling tests."""

    def __init__(
        self,
        name: str | None = None,
        *,
        pass_name: str | None = None,
        reads: Iterable[str] | None = None,
        writes: Iterable[str] | None = None,
        inplace: bool = False,
        internal_symbols: Iterable[str] | None = None,
    ) -> None:
        super().__init__(pass_name=pass_name or name or "Dummy")
        self._reads = _resource_set(reads)
        self._writes = _resource_set(writes)
        self._internal_symbols = list(internal_symbols or ())
        self._inplace = inplace
        if inplace and self._reads and self._writes:
            self._inplace_src = sorted(self._reads)[0]
            self._inplace_dst = sorted(self._writes)[0]
        else:
            self._inplace_src = None
            self._inplace_dst = None

    def compute_reads(self) -> set[str]:
        return set(self._reads)

    def compute_writes(self) -> set[str]:
        return set(self._writes)

    def get_inplace_aliases(self) -> list[tuple[str, str]]:
        if self._inplace and self._inplace_src and self._inplace_dst:
            return [(self._inplace_src, self._inplace_dst)]
        return []

    def get_internal_symbols(self) -> list[str]:
        return list(self._internal_symbols)

    def execute(self, *args, **kwargs) -> None:
        return None
