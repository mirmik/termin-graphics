import json
import os
import subprocess
from pathlib import Path

import pytest

from shaderc_test_helpers import _run_shaderc


def _write_fake_webgpu_slangc(path: Path) -> Path:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'struct Params_std140_0 { @align(16) value_0: vec4<f32>, };\\n'\n"
        "    '@binding(0) @group(0) var<uniform> params_0: Params_std140_0;\\n'\n"
        "    '@binding(1) @group(0) var image_texture_0: texture_2d<f32>;\\n'\n"
        "    '@binding(2) @group(0) var image_sampler_0: sampler;\\n'\n"
        "    '@fragment fn fs_main() -> @location(0) vec4<f32> {\\n'\n"
        "    '  return textureSample(image_texture_0, image_sampler_0, vec2<f32>(0.0));\\n'\n"
        "    '}\\n', encoding='utf-8')\n"
        "reflection.write_text(json.dumps({'parameters': [\n"
        "  {'name': 'params', 'userAttribs': [{'name': 'TerminScope', 'arguments': ['pass']}],\n"
        "   'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "   'type': {'kind': 'constantBuffer', 'elementVarLayout': {\n"
        "     'binding': {'kind': 'uniform', 'size': 16}, 'type': {'kind': 'struct', 'fields': []}}}},\n"
        "  {'name': 'image', 'userAttribs': [{'name': 'TerminScope', 'arguments': ['transient']}],\n"
        "   'binding': {'kind': 'descriptorTableSlot', 'index': 1, 'count': 2},\n"
        "   'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}}\n"
        "]}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def _write_fake_naga(path: Path, *, exit_code: int = 0) -> Path:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "assert sys.argv[1:3] == ['--input-kind', 'wgsl']\n"
        "source = pathlib.Path(sys.argv[3]).read_text(encoding='utf-8')\n"
        "assert '@group(0)' in source\n"
        f"raise SystemExit({exit_code})\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def test_webgpu_target_patches_and_validates_wgsl_with_versioned_layout(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        'struct Params { float4 value; };\n'
        '[[TerminScope("pass")]] ConstantBuffer<Params> params;\n'
        '[[TerminScope("transient")]] Sampler2D image;\n'
        '[shader("fragment")] float4 fs_main() : SV_Target { return image.Sample(float2(0)); }\n',
        encoding="utf-8",
    )
    output = tmp_path / "out.frag.wgsl"

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "webgpu",
            "--stage",
            "fragment",
            "--entry",
            "fs_main",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--slangc",
            str(_write_fake_webgpu_slangc(tmp_path / "slangc.py")),
            "--wgsl-validator",
            str(_write_fake_naga(tmp_path / "naga.py")),
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads(Path(f"{output}.layout.json").read_text(encoding="utf-8"))
    assert layout["version"] == 3
    assert layout["target"] == "webgpu"
    assert layout["stage"] == "fragment"
    assert [resource["name"] for resource in layout["resources"]] == ["params", "image"]
    texture = layout["resources"][1]
    assert texture["webgpu"]["binding"] == texture["binding"]
    assert texture["webgpu"]["sampler_binding"] != texture["binding"]
    wgsl = output.read_text(encoding="utf-8")
    assert f'@binding({texture["binding"]}) @group(0) var image_texture_0' in wgsl
    assert (
        f'@binding({texture["webgpu"]["sampler_binding"]}) @group(0) '
        "var image_sampler_0"
    ) in wgsl
    assert not Path(f"{output}.reflection.json").exists()


def test_webgpu_target_rejects_geometry_before_running_slang(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text('[shader("geometry")] void gs_main() {}\n', encoding="utf-8")
    output = tmp_path / "out.geom.wgsl"

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "webgpu",
            "--stage",
            "geometry",
            "--entry",
            "gs_main",
            "--input",
            str(shader),
            "--output",
            str(output),
        ]
    )

    assert result.returncode == 1
    assert "WebGPU does not support geometry shaders" in result.stderr
    assert not output.exists()


def test_webgpu_target_removes_artifacts_when_validation_fails(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text('[shader("fragment")] float4 fs_main() : SV_Target { return 1; }\n', encoding="utf-8")
    output = tmp_path / "out.frag.wgsl"

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "webgpu",
            "--stage",
            "fragment",
            "--entry",
            "fs_main",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--slangc",
            str(_write_fake_webgpu_slangc(tmp_path / "slangc.py")),
            "--wgsl-validator",
            str(_write_fake_naga(tmp_path / "naga.py", exit_code=9)),
        ]
    )

    assert result.returncode == 1
    assert not output.exists()
    assert not Path(f"{output}.layout.json").exists()


def _pinned_webgpu_tools() -> tuple[Path, Path]:
    root = Path(__file__).resolve().parents[3]
    web_lock = json.loads(
        (root / "build-system/web-shader-toolchain-lock.json").read_text(
            encoding="utf-8"
        )
    )
    from tcbase import Settings

    slangc = Path(Settings("termin").get("Shader/slangCompiler", ""))
    naga = root / "build/toolchains" / f"naga-{web_lock['naga_cli']['version']}" / "bin/naga"
    if not slangc.is_file() or not naga.is_file():
        pytest.skip("pinned WebGPU shader tools are not installed")
    return slangc, naga


def test_builtin_catalog_compiles_to_validated_webgpu_artifacts(tmp_path: Path) -> None:
    root = Path(__file__).resolve().parents[3]
    source_root = root / "termin-graphics/resources/builtin_shaders"
    catalog = json.loads(
        (source_root / "engine-shader-catalog.json").read_text(encoding="utf-8")
    )
    slangc, naga = _pinned_webgpu_tools()
    shaderc = Path(os.environ["TERMIN_SHADERC"])
    placements: dict[str, tuple[int, int | None]] = {}
    compiled = 0

    for shader in catalog["shaders"]:
        if shader["language"] != "slang":
            continue
        for stage, stage_spec in shader["stages"].items():
            output = tmp_path / f"{shader['uuid']}.{stage}.wgsl"
            result = subprocess.run(
                [
                    str(shaderc),
                    "compile",
                    "--language",
                    "slang",
                    "--target",
                    "webgpu",
                    "--stage",
                    stage,
                    "--entry",
                    stage_spec["entry"],
                    "--input",
                    str(source_root / stage_spec["path"]),
                    "--output",
                    str(output),
                    "--slangc",
                    str(slangc),
                    "--wgsl-validator",
                    str(naga),
                    "-I",
                    str(source_root),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            assert result.returncode == 0, (
                f"{shader['uuid']}:{stage}\n{result.stdout}\n{result.stderr}"
            )
            layout = json.loads(
                Path(f"{output}.layout.json").read_text(encoding="utf-8")
            )
            assert layout["version"] == 3
            for resource in layout["resources"]:
                placement = (
                    resource["binding"],
                    resource["webgpu"].get("sampler_binding"),
                )
                previous = placements.setdefault(resource["name"], placement)
                assert previous == placement, resource["name"]
            compiled += 1

    assert compiled == 95
