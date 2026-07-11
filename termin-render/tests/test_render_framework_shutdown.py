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


def test_unattached_python_created_cxx_pass_has_no_self_retain() -> None:
    script = """
import gc

from termin.bootstrap import bootstrap_player
bootstrap_player()
from termin.render_components import DepthPass

frame_pass = DepthPass()
del frame_pass
gc.collect()
print("released")
"""
    result = subprocess.run(
        [sys.executable, "-c", script],
        cwd=REPO_ROOT,
        env={**os.environ, "TERMIN_SDK": str(REPO_ROOT / "sdk")},
        check=True,
        capture_output=True,
        text=True,
    )

    assert result.stdout == "released\n"
    assert "nanobind: leaked" not in result.stderr


def test_pipeline_is_the_only_extra_owner_of_python_created_cxx_pass() -> None:
    script = """
import gc
import sys

from termin.bootstrap import bootstrap_player
bootstrap_player()
from termin.render_components import DepthPass
from termin.render_framework import RenderPipeline

pipeline = RenderPipeline("single-owner")
frame_pass = DepthPass()
baseline = sys.getrefcount(frame_pass)
pipeline.add_pass(frame_pass)
assert sys.getrefcount(frame_pass) == baseline + 1
pipeline.destroy()
assert sys.getrefcount(frame_pass) == baseline
del frame_pass
gc.collect()
print("released")
"""
    result = subprocess.run(
        [sys.executable, "-c", script],
        cwd=REPO_ROOT,
        env={**os.environ, "TERMIN_SDK": str(REPO_ROOT / "sdk")},
        check=True,
        capture_output=True,
        text=True,
    )

    assert result.stdout == "released\n"
    assert "nanobind: leaked" not in result.stderr
