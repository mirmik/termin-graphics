from __future__ import annotations

from tcbase import log

from termin.render_framework._render_framework_native import (
    TcFrameGraphError,
    tc_frame_graph_build,
    tc_frame_graph_destroy,
    tc_frame_graph_get_alias_groups,
    tc_frame_graph_get_error,
    tc_frame_graph_get_error_message,
    tc_frame_graph_get_schedule,
)


class PipelineFrameGraphView:
    def __init__(self, pipeline) -> None:
        self._pipeline = pipeline
        self._fg_ptr: int = 0

    def __enter__(self) -> "PipelineFrameGraphView":
        self.build()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def close(self) -> None:
        if self._fg_ptr:
            tc_frame_graph_destroy(self._fg_ptr)
            self._fg_ptr = 0

    def build(self) -> None:
        self.close()
        handle = self._pipeline._pipeline_handle
        self._fg_ptr = tc_frame_graph_build((handle.index, handle.generation))
        if not self._fg_ptr:
            log.error("[PipelineFrameGraphView] tc_frame_graph_build returned null")
            raise RuntimeError("tc_frame_graph_build returned null")

        error = tc_frame_graph_get_error(self._fg_ptr)
        if error != TcFrameGraphError.OK:
            message = tc_frame_graph_get_error_message(self._fg_ptr)
            log.error(f"[PipelineFrameGraphView] frame graph build failed: {message}")
            raise RuntimeError(message)

    def schedule(self) -> list:
        if not self._fg_ptr:
            self.build()
        return list(tc_frame_graph_get_schedule(self._fg_ptr))

    def alias_groups(self) -> dict[str, list[str]]:
        if not self._fg_ptr:
            self.build()
        return {
            str(canonical): [str(alias) for alias in aliases]
            for canonical, aliases in tc_frame_graph_get_alias_groups(self._fg_ptr).items()
        }
