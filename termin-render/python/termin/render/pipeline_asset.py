"""PipelineAsset - Asset for render pipelines.

Stores pipeline graph source (nodes, connections) via TcScenePipelineTemplate (C).
Compilation to RenderPipeline happens on demand via compile().
File extension: .pipeline

Supports both pass-list format (passes) and graph format (nodes).
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import TYPE_CHECKING

from tcbase import log
from termin_assets import DataAsset
from termin.render_framework import TcScenePipelineTemplate

if TYPE_CHECKING:
    from termin.visualization.render.framegraph import RenderPipeline


class PipelineAsset(DataAsset["RenderPipeline"]):
    """
    Asset for render pipelines (.pipeline files).

    IMPORTANT: Create through ResourceManager, not directly.

    Example:
        rm = ResourceManager.instance()
        pipeline = rm.get_pipeline("my_pipeline")
        asset = rm.get_pipeline_asset("my_pipeline")
    """

    _uses_binary = False  # JSON text files

    def __init__(
        self,
        data: "RenderPipeline | None" = None,
        name: str = "pipeline",
        source_path: Path | str | None = None,
        uuid: str | None = None,
    ):
        """
        Initialize PipelineAsset.

        Args:
            data: RenderPipeline instance (can be None for lazy loading)
            name: Pipeline name
            source_path: Path to .pipeline file
            uuid: Existing UUID or None to generate new one
        """
        super().__init__(data=data, name=name, source_path=source_path, uuid=uuid)

        self._template: TcScenePipelineTemplate | None = None
        self._graph_data: dict | None = None

    @property
    def pipeline(self) -> "RenderPipeline | None":
        """Get the render pipeline (lazy-loaded)."""
        return self.data

    @property
    def graph_data(self) -> dict | None:
        """Raw graph data (nodes, connections) for graph-format pipelines."""
        if not self._loaded:
            self._load()
        return self._graph_data

    @property
    def is_graph_format(self) -> bool:
        """True if this pipeline uses the graph format (nodes, connections)."""
        if not self._loaded:
            self._load()
        return self._graph_data is not None

    @property
    def external_params(self) -> list[str]:
        """List of external RT slot names defined in the pipeline graph."""
        if not self._loaded:
            self._load()
        if self._graph_data is None:
            return []
        result: list[str] = []
        for node in self._graph_data.get("nodes", []):
            if node.get("node_type") == "external_rt":
                slot = node.get("params", {}).get("slot", "")
                name = node.get("name", "")
                result.append(slot or name or "unnamed")
        return result

    def uses_material_names(self, material_names: set[str]) -> bool:
        """True if this pipeline graph references any of material_names."""
        if not self._loaded:
            return False
        from termin.render.pipeline_dependencies import uses_material_names

        return uses_material_names(self._graph_data, material_names)

    def _parse_content(self, content: str) -> "RenderPipeline | None":
        """Parse JSON content into RenderPipeline. Detects graph vs pass-list format."""
        data = json.loads(content)

        if "nodes" in data and "passes" not in data:
            return self._parse_graph(data)
        else:
            return self._parse_pass_list(data)

    def _parse_graph(self, data: dict) -> "RenderPipeline | None":
        """Parse graph-format data and compile to RenderPipeline."""
        self._graph_data = data

        if "uuid" in data:
            self._uuid = data["uuid"]
            existing = TcScenePipelineTemplate.find_by_uuid(self._uuid)
            if existing.is_valid:
                self._template = existing
            else:
                self._template = TcScenePipelineTemplate.declare(self._uuid, self._name)

        if self._template is None:
            self._template = TcScenePipelineTemplate.declare(self.uuid, self._name)

        self._template.set_graph_data(data)
        self._template.name = self._name

        try:
            pipeline = self._template.compile()
            if pipeline is not None:
                pipeline.name = self._name
            return pipeline
        except Exception as e:
            log.error(f"[PipelineAsset] Failed to compile graph: {e}")
            return None

    def _parse_pass_list(self, data: dict) -> "RenderPipeline | None":
        """Parse pass-list pipeline format directly."""
        from termin.assets.resources import ResourceManager
        from termin.visualization.render.framegraph.pipeline import RenderPipeline

        self._graph_data = None
        self._template = None

        rm = ResourceManager.instance()
        pipeline = RenderPipeline.deserialize(data, rm)
        if pipeline is not None:
            pipeline.name = self._name
        return pipeline

    def reload(self) -> bool:
        """Reload the asset and rebind live render targets using this pipeline."""
        result = super().reload()
        if result:
            self._notify_rendering_manager_reloaded()
        return result

    def _notify_rendering_manager_reloaded(self) -> None:
        try:
            from termin.engine import RenderingManager
        except Exception as e:
            log.debug(
                f"[PipelineAsset] RenderingManager is not available; "
                f"skipping live pipeline rebind for '{self._name}': {e}"
            )
            return

        try:
            rebound = RenderingManager.instance().recreate_render_target_pipelines_for_asset(
                self._name,
                self.uuid,
            )
            if rebound:
                log.info(
                    f"[PipelineAsset] Rebound {rebound} render target(s) "
                    f"after reloading pipeline '{self._name}'"
                )
        except Exception:
            log.error(
                f"[PipelineAsset] Failed to rebind render targets for pipeline '{self._name}'",
                exc_info=True,
            )

    def save_graph_to_file(self, path: Path | str | None = None) -> bool:
        """Save graph data to .pipeline file.

        Args:
            path: Target path. If None, uses source_path.

        Returns:
            True on success, False on failure.
        """
        target = Path(path) if path else self._source_path
        if target is None:
            log.error("[PipelineAsset] No path specified for save")
            return False

        if self._graph_data is None:
            log.error("[PipelineAsset] No graph data to save")
            return False

        try:
            save_data = dict(self._graph_data)
            save_data["uuid"] = self.uuid

            with open(target, "w", encoding="utf-8") as f:
                json.dump(save_data, f, indent=2, ensure_ascii=False)

            self._source_path = target
            self.mark_just_saved()

            return True

        except Exception as e:
            log.error(f"[PipelineAsset] Failed to save: {e}")
            return False

    @classmethod
    def from_pipeline(
        cls,
        pipeline: "RenderPipeline",
        name: str | None = None,
        uuid: str | None = None,
    ) -> "PipelineAsset":
        """
        Create PipelineAsset from existing RenderPipeline.

        Args:
            pipeline: RenderPipeline instance
            name: Asset name (defaults to pipeline.name)
            uuid: Optional fixed UUID

        Returns:
            PipelineAsset wrapping the pipeline
        """
        return cls(
            data=pipeline,
            name=name or pipeline.name,
            uuid=uuid,
        )
