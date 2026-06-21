import json
from pathlib import Path

from shaderc_test_helpers import _run_shaderc, _write_fake_fxc, _write_fake_slangc

def test_termin_shaderc_compiles_slang_to_d3d11_cso_with_fake_tools(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "struct MaterialParams { float4 color; };\n"
        "[[TerminScope(\"material\")]] ConstantBuffer<MaterialParams> material;\n"
        "[[TerminScope(\"material\")]] Sampler2D albedo_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return material.color * albedo_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.ps.cso"
    slang_args_path = tmp_path / "slang_args.json"
    fxc_args_path = tmp_path / "fxc_args.json"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "args_path = os.environ.get('FAKE_SLANGC_ARGS')\n"
        "if args_path:\n"
        "    pathlib.Path(args_path).write_text(json.dumps(sys.argv[1:]), encoding='utf-8')\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text('// generated hlsl\\nfloat4 main() : SV_Target0 { return 1; }\\n', encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'material',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}},\n"
        "            },\n"
        "        },\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)
    fake_fxc = _write_fake_fxc(tmp_path / "fake_fxc.py")

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "d3d11",
        "--stage",
        "fragment",
        "--input",
        str(shader),
        "--output",
        str(output),
        "--slangc",
        str(fake_slangc),
        "--fxc",
        str(fake_fxc),
    ], env={
        "FAKE_SLANGC_ARGS": str(slang_args_path),
        "FAKE_FXC_ARGS": str(fxc_args_path),
    })

    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == b"FAKE-CSO"
    assert not (tmp_path / "out.ps.cso.reflection.json").exists()
    assert not (tmp_path / "out.ps.cso.hlsl").exists()

    slang_args = json.loads(slang_args_path.read_text(encoding="utf-8"))
    assert slang_args[slang_args.index("-target") + 1] == "hlsl"
    assert slang_args[slang_args.index("-profile") + 1] == "sm_5_0"
    assert slang_args[slang_args.index("-o") + 1] == str(output) + ".hlsl"

    fxc_args = json.loads(fxc_args_path.read_text(encoding="utf-8"))
    assert fxc_args[fxc_args.index("/T") + 1] == "ps_5_0"
    assert fxc_args[fxc_args.index("/E") + 1] == "main"
    assert fxc_args[fxc_args.index("/Fo") + 1] == str(output)
    assert fxc_args[-1] == str(output) + ".hlsl"

    layout = json.loads((tmp_path / "out.ps.cso.layout.json").read_text(encoding="utf-8"))
    assert layout["version"] == 2
    assert layout["target"] == "d3d11"
    assert layout["resources"] == [
        {
            "name": "material",
            "kind": "constant_buffer",
            "scope": "material",
            "set": 0,
            "binding": 1,
            "stage_mask": 2,
            "size": 16,
            "d3d11": {
                "register_class": "b",
                "register_index": 0,
            },
        },
        {
            "name": "albedo_texture",
            "kind": "texture",
            "scope": "material",
            "set": 0,
            "binding": 4,
            "stage_mask": 2,
            "size": 0,
            "d3d11": {
                "register_class": "t",
                "register_index": 0,
            },
        },
    ]


def test_termin_shaderc_creates_nested_d3d11_output_directories(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return 1; }\n",
        encoding="utf-8",
    )
    output = tmp_path / "nested" / "shaders" / "d3d11" / "out.ps.cso"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.write_text('// generated hlsl\\nfloat4 main() : SV_Target0 { return 1; }\\n', encoding='utf-8')\n"
        "reflection.write_text(json.dumps({'parameters': []}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)
    fake_fxc = tmp_path / "fake_fxc.py"
    fake_fxc.write_text(
        "#!/usr/bin/env python3\n"
        "import pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('/Fo') + 1])\n"
        "out.write_bytes(b'FAKE-CSO')\n",
        encoding="utf-8",
    )
    fake_fxc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "d3d11",
        "--stage",
        "fragment",
        "--input",
        str(shader),
        "--output",
        str(output),
        "--slangc",
        str(fake_slangc),
        "--fxc",
        str(fake_fxc),
    ])

    assert result.returncode == 0, result.stderr
    assert output.read_bytes() == b"FAKE-CSO"
    assert Path(str(output) + ".layout.json").exists()
    assert not Path(str(output) + ".reflection.json").exists()
    assert not Path(str(output) + ".hlsl").exists()


def test_termin_shaderc_reports_missing_fxc_for_d3d11(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("void main() {}\n", encoding="utf-8")
    fake_slangc = _write_fake_slangc(tmp_path / "fake_slangc.py")

    result = _run_shaderc(
        [
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
            "--slangc",
            str(fake_slangc),
        ],
        env={"TERMIN_FXC": str(tmp_path / "missing_fxc")},
    )

    assert result.returncode == 1
    assert "TERMIN_FXC points to missing fxc" in result.stderr
