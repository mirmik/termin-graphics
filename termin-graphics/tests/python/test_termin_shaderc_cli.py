import json
import os
import subprocess
import sys
from pathlib import Path

import pytest


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _termin_shaderc() -> Path:
    candidates: list[Path] = []
    sdk = os.environ.get("TERMIN_SDK")
    if sdk:
        candidates.append(Path(sdk) / "bin" / "termin_shaderc")
    root = _repo_root()
    candidates.extend([
        root / "sdk" / "bin" / "termin_shaderc",
        root / "build" / "Release" / "bin" / "termin_shaderc",
    ])
    for candidate in candidates:
        if candidate.exists():
            return candidate
    pytest.skip("termin_shaderc binary is not available in this test environment")


def _write_fake_slangc(path: Path, *, exit_code: int = 0) -> Path:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "args_path = os.environ.get('FAKE_SLANGC_ARGS')\n"
        "if args_path:\n"
        "    pathlib.Path(args_path).write_text(json.dumps(sys.argv[1:]), encoding='utf-8')\n"
        f"exit_code = {exit_code}\n"
        "if exit_code != 0:\n"
        "    sys.stderr.write('fake slangc failure\\n')\n"
        "    raise SystemExit(exit_code)\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "target = sys.argv[sys.argv.index('-target') + 1]\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(('FAKE-' + target).encode('ascii'))\n",
        encoding="utf-8",
    )
    path.chmod(0o755)
    return path


def _run_shaderc(args: list[str], *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        [str(_termin_shaderc()), *args],
        text=True,
        capture_output=True,
        check=False,
        env=merged_env,
    )


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_invokes_fake_slangc_for_vulkan(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("[shader(\"vertex\")] void main() {}\n", encoding="utf-8")
    output = tmp_path / "out.spv"
    args_path = tmp_path / "slang_args.json"
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "vulkan",
            "--stage",
            "vertex",
            "--entry",
            "main",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--slangc",
            str(fake_slangc),
        ],
        env={"FAKE_SLANGC_ARGS": str(args_path)},
    )

    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == b"FAKE-spirv"
    slang_args = json.loads(args_path.read_text(encoding="utf-8"))
    assert slang_args == [
        str(shader),
        "-entry",
        "main",
        "-stage",
        "vertex",
        "-target",
        "spirv",
        "-matrix-layout-column-major",
        "-profile",
        "spirv_1_5",
        "-fvk-b-shift",
        "0",
        "all",
        "-fvk-t-shift",
        "0",
        "all",
        "-fvk-s-shift",
        "0",
        "all",
        "-o",
        str(output),
    ]


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_writes_slang_resource_layout_sidecar(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float u_strength; };\n"
        "ConstantBuffer<MaterialParams> material : register(b7, space2);\n"
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return float4(material.u_strength); }\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "vulkan",
            "--stage",
            "fragment",
            "--entry",
            "main",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--slangc",
            str(fake_slangc),
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert layout["version"] == 1
    assert layout["language"] == "slang"
    assert layout["target"] == "vulkan"
    assert layout["stage"] == "fragment"
    assert layout["resources"] == [
        {
            "name": "material",
            "kind": "constant_buffer",
            "set": 2,
            "binding": 7,
            "stage_mask": 2,
            "size": 0,
        }
    ]


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_invokes_fake_slangc_for_opengl(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("[shader(\"fragment\")] void main() {}\n", encoding="utf-8")
    output = tmp_path / "out.glsl"
    args_path = tmp_path / "slang_args.json"
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
        "--stage",
        "fragment",
        "--input",
        str(shader),
        "--output",
        str(output),
        "--slangc",
        str(fake_slangc),
    ], env={"FAKE_SLANGC_ARGS": str(args_path)})

    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == b"FAKE-glsl"
    slang_args = json.loads(args_path.read_text(encoding="utf-8"))
    assert "-fvk-b-shift" in slang_args
    assert "-fvk-t-shift" in slang_args
    assert "-fvk-s-shift" in slang_args


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_can_override_slang_matrix_layout(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("[shader(\"vertex\")] void main() {}\n", encoding="utf-8")
    output = tmp_path / "out.spv"
    args_path = tmp_path / "slang_args.json"
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "vulkan",
            "--stage",
            "vertex",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--matrix-layout",
            "row",
            "--slangc",
            str(fake_slangc),
        ],
        env={"FAKE_SLANGC_ARGS": str(args_path)},
    )

    assert result.returncode == 0, result.stderr
    slang_args = json.loads(args_path.read_text(encoding="utf-8"))
    assert "-matrix-layout-row-major" in slang_args
    assert "-matrix-layout-column-major" not in slang_args


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_rejects_unknown_slang_matrix_layout(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("[shader(\"vertex\")] void main() {}\n", encoding="utf-8")
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "vulkan",
        "--stage",
        "vertex",
        "--input",
        str(shader),
        "--output",
        str(tmp_path / "out.spv"),
        "--matrix-layout",
        "diagonal",
        "--slangc",
        str(fake_slangc),
    ])

    assert result.returncode == 1
    assert "unsupported matrix layout" in result.stderr


def test_termin_shaderc_reports_missing_slangc(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("void main() {}\n", encoding="utf-8")

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "vulkan",
            "--stage",
            "vertex",
            "--input",
            str(shader),
            "--output",
            str(tmp_path / "out.spv"),
        ],
        env={"TERMIN_SLANGC": str(tmp_path / "missing_slangc")},
    )

    assert result.returncode == 1
    assert "TERMIN_SLANGC points to missing slangc" in result.stderr


@pytest.mark.skipif(os.name == "nt", reason="fake slangc script is POSIX executable")
def test_termin_shaderc_propagates_slangc_failure(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("void main() {}\n", encoding="utf-8")
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py", exit_code=7)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "vulkan",
        "--stage",
        "vertex",
        "--input",
        str(shader),
        "--output",
        str(tmp_path / "out.spv"),
        "--slangc",
        str(fake_slangc),
    ])

    assert result.returncode == 1
    assert "slangc failed with exit code 7" in result.stderr


def test_termin_shaderc_rejects_glsl_opengl_until_generation_exists(tmp_path: Path) -> None:
    shader = tmp_path / "test.glsl"
    shader.write_text("#version 450\nvoid main() {}\n", encoding="utf-8")

    result = _run_shaderc([
        "compile",
        "--language",
        "glsl",
        "--target",
        "opengl",
        "--stage",
        "vertex",
        "--input",
        str(shader),
        "--output",
        str(tmp_path / "out.glsl"),
    ])

    assert result.returncode == 1
    assert "GLSL input currently supports only --target vulkan" in result.stderr


def test_termin_shaderc_reserves_d3d11_for_windows_backend_phase(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("void main() {}\n", encoding="utf-8")

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "d3d11",
        "--stage",
        "vertex",
        "--input",
        str(shader),
        "--output",
        str(tmp_path / "out.cso"),
    ])

    assert result.returncode == 1
    assert "Windows FXC/DXBC path" in result.stderr
