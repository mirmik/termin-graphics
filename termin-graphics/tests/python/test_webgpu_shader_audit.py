import importlib.util
from pathlib import Path


def _audit_module():
    path = Path(__file__).resolve().parents[3] / "scripts" / "audit_webgpu_shaders.py"
    spec = importlib.util.spec_from_file_location("audit_webgpu_shaders", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_wgsl_inspection_accepts_explicit_separate_resource_bindings() -> None:
    source = """
struct Frame_std140_0 { @align(16) value: vec4<f32>, };
@binding(0) @group(0) var<uniform> frame: Frame_std140_0;
@group(0) @binding(1) var image: texture_2d<f32>;
@binding(2) @group(0) var image_sampler: sampler;
"""

    inspected = _audit_module().inspect_wgsl(source)

    assert inspected["errors"] == []
    assert inspected["resource_counts"] == {
        "uniform_buffer": 1,
        "storage_buffer": 0,
        "texture": 1,
        "sampler": 1,
    }
    assert [(item["group"], item["binding"]) for item in inspected["resources"]] == [
        (0, 0),
        (0, 1),
        (0, 2),
    ]


def test_wgsl_inspection_rejects_duplicate_or_implicit_resource_placement() -> None:
    source = """
@group(0) @binding(1) var first: texture_2d<f32>;
@binding(1) @group(0) var second: sampler;
@group(0) var<uniform> implicit: Frame_std140_0;
var bare_texture: texture_2d<f32>;
"""

    errors = _audit_module().inspect_wgsl(source)["errors"]

    assert any("duplicate @group(0) @binding(1)" in error for error in errors)
    assert "resource implicit has no explicit @group/@binding" in errors
    assert "resource bare_texture has no explicit @group/@binding" in errors


def test_reflection_metrics_cover_constant_buffers_fields_and_matrices() -> None:
    reflection = {
        "parameters": [
            {
                "type": {
                    "kind": "constantBuffer",
                    "fields": [
                        {
                            "type": {"kind": "matrix"},
                            "binding": {"kind": "uniform", "offset": 0, "size": 64},
                        }
                    ],
                }
            }
        ]
    }

    assert _audit_module().reflection_metrics(reflection) == {
        "constant_buffers": 1,
        "matrix_types": 1,
        "uniform_fields": 1,
    }
