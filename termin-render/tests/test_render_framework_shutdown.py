from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def test_render_pipeline_shutdown_cleans_python_pass_refs() -> None:
    script = """
from termin.render_framework import RenderPipeline
from termin.render_framework.frame_graph_view import PipelineFrameGraphView
from termin.render_framework.python_pass import PythonFramePass


class TestPass(PythonFramePass):
    def compute_writes(self):
        return {"color"}


frame_pass = TestPass("writer")
pipeline = RenderPipeline("leak-regression")
pipeline.add_pass(frame_pass)
with PipelineFrameGraphView(pipeline) as graph:
    assert [item.pass_name for item in graph.schedule()] == ["writer"]

print("scheduled")
"""
    result = subprocess.run(
        [sys.executable, "-c", script],
        cwd=REPO_ROOT,
        env={
            **os.environ,
            "TERMIN_SDK": str(REPO_ROOT / "sdk"),
        },
        check=True,
        capture_output=True,
        text=True,
    )

    assert result.stdout == "scheduled\n"
    assert "nanobind: leaked" not in result.stderr
