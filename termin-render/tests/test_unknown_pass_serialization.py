from __future__ import annotations

from termin.render_framework import RenderPipeline


def test_missing_pass_roundtrip_preserves_original_envelope_and_graph_contract() -> None:
    serialized = {
        "name": "degraded",
        "passes": [
            {
                "type": "MissingModulePass",
                "pass_name": "post",
                "enabled": False,
                "passthrough": True,
                "viewport_name": "main",
                "data": {"exposure": 23},
                "_unknown_graph": {
                    "reads": ["source"],
                    "writes": ["output"],
                    "inplace_aliases": [["source", "output"]],
                    "internal_symbols": ["histogram"],
                    "resource_specs": [
                        {
                            "resource": "output",
                            "resource_type": "color_texture",
                            "samples": 1,
                            "viewport_name": "main",
                            "scale": 1.0,
                            "filter": 0,
                        }
                    ],
                },
            }
        ],
        "pipeline_specs": [],
    }

    pipeline = RenderPipeline.deserialize(serialized, object())
    try:
        assert pipeline.pass_count == 1
        placeholder = pipeline.get_pass_at(0)
        assert placeholder.type_name == "UnknownPass"
        assert placeholder.pass_name == "post"
        assert placeholder.is_placeholder is True
        assert placeholder.original_type == "MissingModulePass"
        assert placeholder.serialize()["type"] == "MissingModulePass"

        roundtrip = pipeline.serialize()
        restored = roundtrip["passes"][0]
        assert restored["type"] == "MissingModulePass"
        assert restored["pass_name"] == "post"
        assert restored["enabled"] is False
        assert restored["passthrough"] is True
        assert restored["viewport_name"] == "main"
        assert restored["data"] == {"exposure": 23}
        assert restored["_unknown_graph"] == serialized["passes"][0]["_unknown_graph"]
    finally:
        pipeline.destroy()
