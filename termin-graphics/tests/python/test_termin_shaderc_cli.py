import json
from pathlib import Path

from shaderc_test_helpers import (
    _run_shaderc,
    _spirv_decoration_value,
    _write_fake_fxc,
    _write_fake_slangc,
)

def test_termin_shaderc_help_describes_compile_debug_options() -> None:
    result = _run_shaderc(["--help"])

    assert result.returncode == 0
    assert result.stderr == ""
    assert "termin_shaderc - Termin shader artifact compiler" in result.stdout
    assert "termin_shaderc compile [options]" in result.stdout
    assert "--slangc <path>" in result.stdout
    assert "--fxc <path>" in result.stdout
    assert "--include-dir <dir>" in result.stdout
    assert "--default-scope <scope>" in result.stdout
    assert "<output>.layout.json" in result.stdout
    assert "<output>.artifact" in result.stdout
    assert '[[TerminScope("frame|pass|material|draw|transient")]]' in result.stdout


def test_termin_shaderc_compile_help_is_successful() -> None:
    direct = _run_shaderc(["compile", "--help"])
    topic = _run_shaderc(["help", "compile"])

    assert direct.returncode == 0
    assert topic.returncode == 0
    assert direct.stdout == topic.stdout
    assert "Compile options:" in direct.stdout
    assert "Examples:" in direct.stdout
    assert "--layout-scheme" not in direct.stdout


def test_termin_shaderc_rejects_invalid_default_scope(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text("[shader(\"fragment\")] float4 main() : SV_Target0 { return 1; }\n", encoding="utf-8")
    output = tmp_path / "out.spv"

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "slang",
            "--target",
            "vulkan",
            "--stage",
            "fragment",
            "--input",
            str(shader),
            "--output",
            str(output),
            "--default-scope",
            "materialish",
        ]
    )

    assert result.returncode == 2
    assert "invalid --default-scope value 'materialish'" in result.stderr
    assert not output.exists()


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
    assert not (tmp_path / "out.spv.reflection.json").exists()
    slang_args = json.loads(args_path.read_text(encoding="utf-8"))
    assert slang_args[0] == str(shader)
    assert slang_args[slang_args.index("-entry") + 1] == "main"
    assert slang_args[slang_args.index("-stage") + 1] == "vertex"
    assert slang_args[slang_args.index("-target") + 1] == "spirv"
    assert "-matrix-layout-column-major" in slang_args
    assert slang_args[slang_args.index("-reflection-json") + 1] == str(output) + ".reflection.json"
    assert slang_args[slang_args.index("-profile") + 1] == "spirv_1_5"
    assert slang_args[slang_args.index("-o") + 1] == str(output)
    include_values = [
        slang_args[i + 1]
        for i, value in enumerate(slang_args[:-1])
        if value == "-I"
    ]
    assert str(tmp_path) in include_values


def test_termin_shaderc_writes_slang_resource_layout_sidecar(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float u_strength; };\n"
        "ConstantBuffer<MaterialParams> material;\n"
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return float4(material.u_strength); }\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, struct, sys\n"
        "OP_NAME = 5\n"
        "OP_DECORATE = 71\n"
        "DECORATION_BINDING = 33\n"
        "DECORATION_DESCRIPTOR_SET = 34\n"
        "def string_words(value):\n"
        "    raw = value.encode('utf-8') + b'\\0'\n"
        "    raw += b'\\0' * ((4 - len(raw) % 4) % 4)\n"
        "    return [struct.unpack('<I', raw[i:i + 4])[0] for i in range(0, len(raw), 4)]\n"
        "def inst(op, operands):\n"
        "    return [((len(operands) + 1) << 16) | op] + operands\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "resource_id = 17\n"
        "words = [0x07230203, 0x00010500, 0, 32, 0]\n"
        "words += inst(OP_NAME, [resource_id] + string_words('material'))\n"
        "words += inst(OP_DECORATE, [resource_id, DECORATION_BINDING, 1])\n"
        "words += inst(OP_DECORATE, [resource_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "out.write_bytes(struct.pack('<' + 'I' * len(words), *words))\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [{\n"
        "        'name': 'material',\n"
        "        'binding': {'kind': 'constantBuffer', 'index': 1},\n"
        "        'type': {\n"
        "            'kind': 'constantBuffer',\n"
        "            'elementVarLayout': {\n"
        "                'binding': {'kind': 'uniform', 'size': 16},\n"
        "                'type': {\n"
        "                    'kind': 'struct',\n"
        "                    'fields': [{\n"
        "                        'name': 'u_strength',\n"
        "                        'type': {'kind': 'scalar', 'scalarType': 'float32'},\n"
        "                        'binding': {'kind': 'uniform', 'offset': 0, 'size': 4},\n"
        "                    }],\n"
        "                },\n"
        "            },\n"
        "        },\n"
        "    }]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert not (tmp_path / "out.spv.reflection.json").exists()
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert layout["version"] == 1
    assert layout["language"] == "slang"
    assert layout["target"] == "vulkan"
    assert layout["stage"] == "fragment"
    assert layout["resources"] == [
        {
            "name": "material",
            "kind": "constant_buffer",
            "scope": "unscoped",
            "set": 0,
            "binding": 1,
            "stage_mask": 2,
            "size": 16,
            "fields": [
                {
                    "name": "u_strength",
                    "type": "Float",
                    "offset": 0,
                    "size": 4,
                }
            ],
        }
    ]


def test_termin_shaderc_writes_glsl_bone_block_resource_layout(tmp_path: Path) -> None:
    shader = tmp_path / "skinned.vert.glsl"
    shader.write_text(
        "#version 450 core\n"
        "layout(location = 0) in vec3 a_position;\n"
        "layout(location = 6) in vec4 a_joints;\n"
        "layout(location = 7) in vec4 a_weights;\n"
        "layout(std140, binding = 16) uniform BoneBlock {\n"
        "    mat4 u_bone_matrices[128];\n"
        "    int u_bone_count;\n"
        "};\n"
        "void main() { gl_Position = vec4(a_position, 1.0); }\n",
        encoding="utf-8",
    )
    output = tmp_path / "skinned.vert.spv"

    result = _run_shaderc(
        [
            "compile",
            "--language",
            "glsl",
            "--target",
            "vulkan",
            "--stage",
            "vertex",
            "--input",
            str(shader),
            "--output",
            str(output),
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "skinned.vert.spv.layout.json").read_text(encoding="utf-8"))
    assert {
        "name": "BoneBlock",
        "kind": "constant_buffer",
        "scope": "unscoped",
        "set": 0,
        "binding": 16,
        "stage_mask": 1,
        "size": 0,
    } in layout["resources"]


def test_termin_shaderc_drops_dead_slang_reflection_resources(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct PerFrame { float4x4 view; };\n"
        "struct PushData { float4 color; };\n"
        "ConstantBuffer<PerFrame> u_per_frame;\n"
        "[[TerminScope(\"draw\")]]\n"
        "ConstantBuffer<PushData> u_push;\n"
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return u_push.color; }\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, struct, sys\n"
        "OP_NAME = 5\n"
        "OP_DECORATE = 71\n"
        "DECORATION_BINDING = 33\n"
        "DECORATION_DESCRIPTOR_SET = 34\n"
        "def string_words(value):\n"
        "    raw = value.encode('utf-8') + b'\\0'\n"
        "    raw += b'\\0' * ((4 - len(raw) % 4) % 4)\n"
        "    return [struct.unpack('<I', raw[i:i + 4])[0] for i in range(0, len(raw), 4)]\n"
        "def inst(op, operands):\n"
        "    return [((len(operands) + 1) << 16) | op] + operands\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "push_id = 17\n"
        "words = [0x07230203, 0x00010500, 0, 32, 0]\n"
        "words += inst(OP_NAME, [push_id] + string_words('u_push'))\n"
        "words += inst(OP_DECORATE, [push_id, DECORATION_BINDING, 13])\n"
        "words += inst(OP_DECORATE, [push_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "out.write_bytes(struct.pack('<' + 'I' * len(words), *words))\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {'name': 'u_per_frame', 'binding': {'kind': 'constantBuffer', 'index': 2},\n"
        "         'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}}}},\n"
        "        {'name': 'u_push', 'binding': {'kind': 'constantBuffer', 'index': 13},\n"
        "         'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "         'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}}},\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert [resource["name"] for resource in layout["resources"]] == ["u_push"]
    assert layout["resources"][0]["scope"] == "draw"
    assert layout["resources"][0]["binding"] == 24
    assert _spirv_decoration_value(output, 17, 33) == 24
    assert _spirv_decoration_value(output, 17, 34) == 0


def test_termin_shaderc_reads_slang_scope_user_attributes(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "[[TerminScope(\"transient\")]] Sampler2D albedo_texture;\n"
        "[[Scope(\"material\")]] ConstantBuffer<float4> custom_params;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return albedo_texture.Sample(uv) + custom_params;\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, struct, sys\n"
        "OP_NAME = 5\n"
        "OP_DECORATE = 71\n"
        "DECORATION_BINDING = 33\n"
        "DECORATION_DESCRIPTOR_SET = 34\n"
        "def string_words(value):\n"
        "    raw = value.encode('utf-8') + b'\\0'\n"
        "    raw += b'\\0' * ((4 - len(raw) % 4) % 4)\n"
        "    return [struct.unpack('<I', raw[i:i + 4])[0] for i in range(0, len(raw), 4)]\n"
        "def inst(op, operands):\n"
        "    return [((len(operands) + 1) << 16) | op] + operands\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "albedo_id = 17\n"
        "custom_id = 18\n"
        "words = [0x07230203, 0x00010500, 0, 32, 0]\n"
        "words += inst(OP_NAME, [albedo_id] + string_words('albedo_texture'))\n"
        "words += inst(OP_DECORATE, [albedo_id, DECORATION_BINDING, 3])\n"
        "words += inst(OP_DECORATE, [albedo_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "words += inst(OP_NAME, [custom_id] + string_words('custom_params'))\n"
        "words += inst(OP_DECORATE, [custom_id, DECORATION_BINDING, 4])\n"
        "words += inst(OP_DECORATE, [custom_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "out.write_bytes(struct.pack('<' + 'I' * len(words), *words))\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'userAttribs': [\n"
        "                {'name': 'TerminScope', 'arguments': ['transient']},\n"
        "            ],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 3},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "        {\n"
        "            'name': 'custom_params',\n"
        "            'userAttribs': [\n"
        "                {'name': 'Scope', 'arguments': ['material']},\n"
        "            ],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 4},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}},\n"
        "            },\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert layout["resources"] == [
        {
            "name": "albedo_texture",
            "kind": "texture",
            "scope": "transient",
            "set": 0,
            "binding": 32,
            "stage_mask": 2,
            "size": 0,
        },
        {
            "name": "custom_params",
            "kind": "constant_buffer",
            "scope": "material",
            "set": 0,
            "binding": 4,
            "stage_mask": 2,
            "size": 16,
        },
    ]


def test_termin_shaderc_marks_missing_scope_as_unscoped(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float4 color; };\n"
        "ConstantBuffer<MaterialParams> material;\n"
        "Sampler2D albedo_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return material.color * albedo_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'material',\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
        "        },\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert "resource 'material' has no scope" in result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("material", "unscoped", 0),
        ("albedo_texture", "unscoped", 1),
    ]


def test_termin_shaderc_default_scope_material_resolves_unscoped_resources(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float4 color; };\n"
        "ConstantBuffer<MaterialParams> material;\n"
        "Sampler2D albedo_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return material.color * albedo_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'material',\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
        "        },\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
            "--default-scope",
            "material",
        ]
    )

    assert result.returncode == 0, result.stderr
    assert "has no scope" not in result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("material", "material", 1),
        ("albedo_texture", "material", 4),
    ]


def test_termin_shaderc_rejects_invalid_slang_scope_user_attribute(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "[[TerminScope(\"materail\")]] ConstantBuffer<float4> material;\n"
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return material; }\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [{\n"
        "        'name': 'material',\n"
        "        'userAttribs': [\n"
        "            {'name': 'TerminScope', 'arguments': ['materail']},\n"
        "        ],\n"
        "        'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "        'type': {\n"
        "            'kind': 'constantBuffer',\n"
        "            'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}},\n"
        "        },\n"
        "    }]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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

    assert result.returncode == 1
    assert "invalid TerminScope value 'materail'" in result.stderr
    assert not output.exists()
    assert not (tmp_path / "out.spv.layout.json").exists()
    assert not (tmp_path / "out.spv.reflection.json").exists()


def test_termin_shaderc_normalizes_slang_engine_constant_buffer_reflection_placement(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct PerFrame { float4x4 u_view; };\n"
        "struct SlangDrawData { float4x4 u_model; };\n"
        "[[TerminScope(\"frame\")]]\n"
        "ConstantBuffer<PerFrame> per_frame;\n"
        "[[TerminScope(\"draw\")]]\n"
        "ConstantBuffer<SlangDrawData> draw_data;\n"
        "[shader(\"vertex\")] float4 main(float3 position : POSITION) : SV_Position {\n"
        "    return mul(per_frame.u_view, mul(draw_data.u_model, float4(position, 1.0)));\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'per_frame',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['frame']}],\n"
        "            'binding': {'kind': 'constantBuffer', 'index': 1},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}},\n"
        "            },\n"
        "        },\n"
        "        {\n"
        "            'name': 'draw_data',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'constantBuffer', 'index': 2},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}},\n"
        "            },\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert layout["resources"] == [
        {
            "name": "per_frame",
            "kind": "constant_buffer",
            "scope": "frame",
            "set": 0,
            "binding": 2,
            "stage_mask": 1,
            "size": 64,
        },
        {
            "name": "draw_data",
            "kind": "constant_buffer",
            "scope": "draw",
            "set": 0,
            "binding": 24,
            "stage_mask": 1,
            "size": 64,
        },
    ]


def test_termin_shaderc_writes_slang_texture_resources_from_reflection(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "Sampler2D albedo_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return albedo_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert layout["resources"] == [
        {
            "name": "albedo_texture",
            "kind": "texture",
            "scope": "unscoped",
            "set": 0,
            "binding": 0,
            "stage_mask": 2,
            "size": 0,
        },
    ]


def test_termin_shaderc_default_scope_transient_resolves_framebuffer_inputs(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "Sampler2D u_input_tex;\n"
        "Sampler2D u_depth_texture;\n"
        "Sampler2D u_fov;\n"
        "Sampler2D u_normal_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_input_tex.Sample(uv) + u_depth_texture.Sample(uv) + u_fov.Sample(uv) + u_normal_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {'name': 'u_input_tex', 'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "        {'name': 'u_depth_texture', 'binding': {'kind': 'descriptorTableSlot', 'index': 2},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "        {'name': 'u_fov', 'binding': {'kind': 'descriptorTableSlot', 'index': 3},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "        {'name': 'u_normal_texture', 'binding': {'kind': 'descriptorTableSlot', 'index': 4},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
            "--default-scope",
            "transient",
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("u_input_tex", "transient", 32),
        ("u_depth_texture", "transient", 33),
        ("u_fov", "transient", 34),
        ("u_normal_texture", "transient", 35),
    ]


def test_termin_shaderc_does_not_infer_framebuffer_inputs_from_names(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "Sampler2D u_input_tex;\n"
        "Sampler2D u_depth_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_input_tex.Sample(uv) + u_depth_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {'name': 'u_input_tex', 'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "        {'name': 'u_depth_texture', 'binding': {'kind': 'descriptorTableSlot', 'index': 2},\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("u_input_tex", "unscoped", 1),
        ("u_depth_texture", "unscoped", 2),
    ]


def test_termin_shaderc_separates_slang_transient_textures_from_material_ubo(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float strength; };\n"
        "[[TerminScope(\"material\")]] ConstantBuffer<MaterialParams> material;\n"
        "[[TerminScope(\"transient\")]] Sampler2D u_input_tex;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_input_tex.Sample(uv) * material.strength;\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'material',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
        "        },\n"
        "        {\n"
        "            'name': 'u_input_tex',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['transient']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("material", "material", 1),
        ("u_input_tex", "transient", 32),
    ]


def test_termin_shaderc_keeps_standalone_normal_texture_material_scoped(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "[[TerminScope(\"material\")]]\n"
        "Sampler2D u_normal_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_normal_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {'name': 'u_normal_texture', 'binding': {'kind': 'descriptorTableSlot', 'index': 9},\n"
        "         'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "         'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True}},\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("u_normal_texture", "material", 4),
    ]


def test_termin_shaderc_preserves_slang_draw_storage_buffer_reflection_placement(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "struct FoliageInstance { float3 position; float yaw; float3 normal; float seed; };\n"
        "[[TerminScope(\"draw\")]] StructuredBuffer<FoliageInstance> foliage_instances;\n"
        "[shader(\"vertex\")] float4 main(uint instance_id : SV_InstanceID) : SV_Position {\n"
        "    return float4(foliage_instances[instance_id].position, 1.0);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, struct, sys\n"
        "OP_NAME = 5\n"
        "OP_DECORATE = 71\n"
        "DECORATION_BINDING = 33\n"
        "DECORATION_DESCRIPTOR_SET = 34\n"
        "def string_words(value):\n"
        "    raw = value.encode('utf-8') + b'\\0'\n"
        "    raw += b'\\0' * ((4 - len(raw) % 4) % 4)\n"
        "    return [struct.unpack('<I', raw[i:i + 4])[0] for i in range(0, len(raw), 4)]\n"
        "def inst(op, operands):\n"
        "    return [((len(operands) + 1) << 16) | op] + operands\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "resource_id = 17\n"
        "words = [0x07230203, 0x00010500, 0, 32, 0]\n"
        "words += inst(OP_NAME, [resource_id] + string_words('foliage_instances'))\n"
        "words += inst(OP_DECORATE, [resource_id, DECORATION_BINDING, 0])\n"
        "words += inst(OP_DECORATE, [resource_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "out.write_bytes(struct.pack('<' + 'I' * len(words), *words))\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'foliage_instances',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'structuredBuffer'},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert layout["resources"] == [
        {
            "name": "foliage_instances",
            "kind": "storage_buffer",
            "scope": "draw",
            "set": 0,
            "binding": 25,
            "stage_mask": 1,
            "size": 0,
        }
    ]


def test_termin_shaderc_normalizes_slang_draw_scope_reflection_placement(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "struct DrawData { float4x4 model; };\n"
        "struct BoneBlock { float4x4 bones[128]; int count; };\n"
        "[[TerminScope(\"draw\")]] ConstantBuffer<DrawData> draw_data;\n"
        "[[TerminScope(\"draw\")]] ConstantBuffer<BoneBlock> bone_block;\n"
        "[shader(\"vertex\")] float4 main(float3 position : POSITION) : SV_Position {\n"
        "    return mul(draw_data.model, float4(position, 1.0));\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, struct, sys\n"
        "OP_NAME = 5\n"
        "OP_DECORATE = 71\n"
        "DECORATION_BINDING = 33\n"
        "DECORATION_DESCRIPTOR_SET = 34\n"
        "def string_words(value):\n"
        "    raw = value.encode('utf-8') + b'\\0'\n"
        "    raw += b'\\0' * ((4 - len(raw) % 4) % 4)\n"
        "    return [struct.unpack('<I', raw[i:i + 4])[0] for i in range(0, len(raw), 4)]\n"
        "def inst(op, operands):\n"
        "    return [((len(operands) + 1) << 16) | op] + operands\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "words = [0x07230203, 0x00010500, 0, 64, 0]\n"
        "for resource_id, name, binding in [(17, 'draw_data', 3), (18, 'bone_block', 4)]:\n"
        "    words += inst(OP_NAME, [resource_id] + string_words(name))\n"
        "    words += inst(OP_DECORATE, [resource_id, DECORATION_BINDING, binding])\n"
        "    words += inst(OP_DECORATE, [resource_id, DECORATION_DESCRIPTOR_SET, 0])\n"
        "out.write_bytes(struct.pack('<' + 'I' * len(words), *words))\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'draw_data',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 3},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}},\n"
        "            },\n"
        "        },\n"
        "        {\n"
        "            'name': 'bone_block',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 4},\n"
        "            'type': {\n"
        "                'kind': 'constantBuffer',\n"
        "                'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 8208}},\n"
        "            },\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
        ]
    )

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.spv.layout.json").read_text(encoding="utf-8"))
    assert layout["resources"] == [
        {
            "name": "draw_data",
            "kind": "constant_buffer",
            "scope": "draw",
            "set": 0,
            "binding": 24,
            "stage_mask": 1,
            "size": 64,
        },
        {
            "name": "bone_block",
            "kind": "constant_buffer",
            "scope": "draw",
            "set": 0,
            "binding": 16,
            "stage_mask": 1,
            "size": 8208,
        },
    ]


def test_termin_shaderc_normalizes_slang_material_texture_reflection_placement(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct MaterialParams { float4 color; };\n"
        "[[TerminScope(\"material\")]] ConstantBuffer<MaterialParams> material;\n"
        "[[TerminScope(\"material\")]] Sampler2D albedo_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return material.color * albedo_texture.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
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
    assert layout["resources"][0]["name"] == "material"
    assert layout["resources"][0]["binding"] == 1
    assert layout["resources"][1]["name"] == "albedo_texture"
    assert layout["resources"][1]["binding"] == 4


def test_termin_shaderc_normalizes_slang_pass_shadow_texture_reflection_placement(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "[[TerminScope(\"pass\")]] Sampler2DShadow shadow_maps[16];\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return float4(shadow_maps[0].SampleCmp(uv, 0.5));\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'shadow_maps',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['pass']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 3},\n"
        "            'type': {\n"
        "                'kind': 'array',\n"
        "                'elementType': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "            },\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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
    assert layout["resources"] == [
        {
            "name": "shadow_maps",
            "kind": "texture",
            "scope": "pass",
            "set": 0,
            "binding": 8,
            "stage_mask": 2,
            "size": 0,
        },
    ]


def test_termin_shaderc_rejects_split_slang_texture_resources_for_vulkan(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "Texture2D<float4> albedo_texture;\n"
        "SamplerState linear_sampler;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return albedo_texture.Sample(linear_sampler, uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.spv"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-spirv')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'albedo_texture',\n"
        "            'binding': {'kind': 'shaderResource', 'index': 0},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D'},\n"
        "        },\n"
        "        {\n"
        "            'name': 'linear_sampler',\n"
        "            'binding': {'kind': 'samplerState', 'index': 0},\n"
        "            'type': {'kind': 'samplerState'},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

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

    assert result.returncode == 1
    assert "does not support split Slang" in result.stderr
    assert not (tmp_path / "out.spv.reflection.json").exists()


def test_termin_shaderc_normalizes_slang_sampler2d_register_resource_layout(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "Sampler2D albedo_texture : register(t7, space2);\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return albedo_texture.Sample(uv);\n"
        "}\n",
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
    assert layout["resources"] == [
        {
            "name": "albedo_texture",
            "kind": "texture",
            "scope": "unscoped",
            "set": 0,
            "binding": 7,
            "stage_mask": 2,
            "size": 0,
        },
    ]


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


def test_termin_shaderc_merges_bare_slang_sampler2d_when_reflection_is_partial(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "struct DrawParams { float4 color; };\n"
        "[[TerminScope(\"draw\")]] ConstantBuffer<DrawParams> canvas_draw;\n"
        "[[TerminScope(\"transient\")]] Sampler2D u_texture;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_texture.Sample(uv) * canvas_draw.color;\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.ps.cso"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text('// generated hlsl\\nfloat4 main() : SV_Target0 { return 1; }\\n', encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [{\n"
        "        'name': 'canvas_draw',\n"
        "        'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "        'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "        'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
        "    }]\n"
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
    ])

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.ps.cso.layout.json").read_text(encoding="utf-8"))
    assert [
        (resource["name"], resource["kind"], resource["scope"], resource["binding"], resource["d3d11"])
        for resource in layout["resources"]
    ] == [
        ("canvas_draw", "constant_buffer", "draw", 24, {"register_class": "b", "register_index": 0}),
        ("u_texture", "texture", "transient", 32, {"register_class": "t", "register_index": 0}),
    ]


def test_termin_shaderc_d3d11_patches_helper_and_imported_hlsl_resources(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_prelude;\n"
        "struct MaterialParams { float4 color; };\n"
        "[[TerminScope(\"material\")]] ConstantBuffer<MaterialParams> material;\n"
        "[[TerminScope(\"material\")]] Sampler2D albedo_texture;\n"
        "[[TerminScope(\"material\")]] Sampler2D normal_texture;\n"
        "float4 sample_normal(float2 uv) { return normal_texture.Sample(uv); }\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return material.color * albedo_texture.Sample(uv) * sample_normal(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.ps.cso"
    patched_hlsl = tmp_path / "patched.hlsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'cbuffer material_0 : register(b0) { float4 color; }\\n'\n"
        "    'Texture2D<float4 > albedo_texture_texture_0 : register(t0);\\n'\n"
        "    'SamplerState albedo_texture_sampler_0 : register(s0);\\n'\n"
        "    'Texture2D<float4 > normal_texture_texture_0 : register(t0);\\n'\n"
        "    'SamplerState normal_texture_sampler_0 : register(s0);\\n'\n"
        "    'Texture2D<float > shadow_maps_texture_0[int(16)] : register(t5);\\n'\n"
        "    'SamplerComparisonState shadow_maps_sampler_0[int(16)] : register(s5);\\n'\n"
        "    'float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\\n'\n"
        "    '    return normal_texture_texture_0.Sample(normal_texture_sampler_0, uv) +\\n'\n"
        "    '        shadow_maps_texture_0[int(3)].SampleCmp(shadow_maps_sampler_0[int(3)], uv, 0.5);\\n'\n"
        "    '}\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'material',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
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
    fake_fxc = tmp_path / "fake_fxc.py"
    fake_fxc.write_text(
        "#!/usr/bin/env python3\n"
        "import os, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('/Fo') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "capture = os.environ.get('FAKE_FXC_HLSL_CAPTURE')\n"
        "if capture:\n"
        "    pathlib.Path(capture).write_text(pathlib.Path(sys.argv[-1]).read_text(encoding='utf-8'), encoding='utf-8')\n"
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
    ], env={"FAKE_FXC_HLSL_CAPTURE": str(patched_hlsl)})

    assert result.returncode == 0, result.stderr
    hlsl = patched_hlsl.read_text(encoding="utf-8")
    assert "normal_texture_texture_0 : register(t1)" in hlsl
    assert "normal_texture_sampler_0 : register(s1)" in hlsl
    assert "shadow_maps_texture_0[int(16)] : register(t2)" in hlsl
    assert "shadow_maps_sampler_0 : register(s2)" in hlsl
    assert "shadow_maps_sampler_0[int" not in hlsl

    layout = json.loads((tmp_path / "out.ps.cso.layout.json").read_text(encoding="utf-8"))
    assert [
        (resource["name"], resource["kind"], resource["scope"], resource["d3d11"])
        for resource in layout["resources"]
    ] == [
        ("material", "constant_buffer", "material", {"register_class": "b", "register_index": 0}),
        ("albedo_texture", "texture", "material", {"register_class": "t", "register_index": 0}),
        ("normal_texture", "texture", "material", {"register_class": "t", "register_index": 1}),
        ("shadow_maps", "texture", "pass", {"register_class": "t", "register_index": 2}),
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
