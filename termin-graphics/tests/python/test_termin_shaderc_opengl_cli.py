import json
from pathlib import Path

from shaderc_test_helpers import (
    _expected_scoped_binding,
    _expected_scoped_bindings,
    _run_shaderc,
    _write_fake_slangc,
)

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
    assert "-fvk-b-shift" not in slang_args
    assert "-fvk-t-shift" not in slang_args
    assert "-fvk-s-shift" not in slang_args


def test_termin_shaderc_patches_opengl_slang_constant_buffer_bindings(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct PerFrame { float4x4 view_projection; };\n"
        "struct DrawData { float4x4 model; };\n"
        "[[TerminScope(\"frame\")]] ConstantBuffer<PerFrame> per_frame;\n"
        "[[TerminScope(\"draw\")]] ConstantBuffer<DrawData> draw_data;\n"
        "[shader(\"vertex\")] float4 main(float3 position : POSITION) : SV_Position {\n"
        "    return mul(per_frame.view_projection, mul(draw_data.model, float4(position, 1.0)));\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.vert.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'layout(binding = 0)\\n'\n"
        "    'layout(std140) uniform block_PerFrame_0 { mat4 view_projection; };\\n'\n"
        "    'layout(binding = 1)\\n'\n"
        "    'layout(std140) uniform block_DrawData_0 { mat4 model; };\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'per_frame',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['frame']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}}},\n"
        "        },\n"
        "        {\n"
        "            'name': 'draw_data',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 1},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}}},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    per_frame_binding = _expected_scoped_binding(
        "per_frame", "constant_buffer", "frame", target="opengl"
    )
    draw_data_binding = _expected_scoped_binding(
        "draw_data", "constant_buffer", "draw", target="opengl"
    )
    assert f"layout(binding = {per_frame_binding})\nlayout(std140) uniform block_PerFrame_0" in glsl
    assert f"layout(binding = {draw_data_binding})\nlayout(std140) uniform block_DrawData_0" in glsl
    layout = json.loads((tmp_path / "out.vert.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["binding"]) for r in layout["resources"]] == [
        ("per_frame", per_frame_binding),
        ("draw_data", draw_data_binding),
    ]


def test_termin_shaderc_patches_opengl_slang_transient_texture_bindings(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "[[TerminScope(\"transient\")]] Sampler2D u_font_atlas;\n"
        "[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        "    return u_font_atlas.Sample(uv);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.frag.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'layout(binding = 0) uniform sampler2D u_font_atlas_0;\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'u_font_atlas',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['transient']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    font_binding = _expected_scoped_binding("u_font_atlas", "texture", "transient", target="opengl")
    assert f"layout(binding = {font_binding}) uniform sampler2D u_font_atlas_0" in glsl
    layout = json.loads((tmp_path / "out.frag.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        ("u_font_atlas", "transient", font_binding),
    ]

def test_termin_shaderc_patches_opengl_slang_material_texture_bindings(tmp_path: Path) -> None:
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
    output = tmp_path / "out.frag.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'layout(binding = 0)\\n'\n"
        "    'layout(std140) uniform block_MaterialParams_0 { vec4 color; };\\n'\n"
        "    'layout(binding = 1) uniform sampler2D albedo_texture_0;\\n',\n"
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

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    material_binding = _expected_scoped_binding(
        "material", "constant_buffer", "material", target="opengl"
    )
    albedo_binding = _expected_scoped_binding(
        "albedo_texture", "texture", "material", target="opengl"
    )
    assert f"layout(binding = {material_binding})\nlayout(std140) uniform block_MaterialParams_0" in glsl
    assert f"layout(binding = {albedo_binding}) uniform sampler2D albedo_texture_0" in glsl
    layout = json.loads((tmp_path / "out.frag.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["binding"]) for r in layout["resources"]] == [
        ("material", material_binding),
        ("albedo_texture", albedo_binding),
    ]


def test_termin_shaderc_allocates_opengl_material_range_for_pbr_texture_set(tmp_path: Path) -> None:
    texture_names = [
        "u_albedo_texture",
        "u_normal_texture",
        "u_metallic_roughness_texture",
        "u_occlusion_texture",
        "u_emissive_texture",
    ]
    shader = tmp_path / "test.slang"
    shader.write_text(
        "\n".join(f'[[TerminScope("material")]] Sampler2D {name};' for name in texture_names)
        + "\n[shader(\"fragment\")] float4 main(float2 uv : TEXCOORD0) : SV_Target0 {\n"
        + "    return "
        + " + ".join(f"{name}.Sample(uv)" for name in texture_names)
        + ";\n}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.frag.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "names = [\n"
        + "".join(f"    '{name}',\n" for name in texture_names)
        + "]\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(''.join(\n"
        "    f'layout(binding = {index}) uniform sampler2D {name}_0;\\n'\n"
        "    for index, name in enumerate(names)\n"
        "), encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': name,\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['material']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': index},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'texture2D', 'combined': True},\n"
        "        }\n"
        "        for index, name in enumerate(names)\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    expected_bindings = _expected_scoped_bindings(
        [(name, "texture", "material") for name in texture_names],
        target="opengl",
    )
    assert len(set(expected_bindings.values())) == len(texture_names)
    glsl = output.read_text(encoding="utf-8")
    for name in texture_names:
        binding = expected_bindings[name]
        assert f"layout(binding = {binding}) uniform sampler2D {name}_0" in glsl

    layout = json.loads((tmp_path / "out.frag.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["scope"], r["binding"]) for r in layout["resources"]] == [
        (name, "material", expected_bindings[name])
        for name in texture_names
    ]


def test_termin_shaderc_filters_inactive_opengl_slang_reflection_resources(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct PerFrame { float4x4 view_projection; };\n"
        "[[TerminScope(\"frame\")]] ConstantBuffer<PerFrame> per_frame;\n"
        "[shader(\"fragment\")] void main() {}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.frag.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text('void main() {}\\n', encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'per_frame',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['frame']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 0},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 64}}},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    layout = json.loads((tmp_path / "out.frag.glsl.layout.json").read_text(encoding="utf-8"))
    assert layout["resources"] == []


def test_termin_shaderc_patches_imported_opengl_slang_constant_buffer_by_instance(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "import termin_lighting;\n"
        "[shader(\"fragment\")] float4 main() : SV_Target0 { return float4(get_light_count()); }\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.frag.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'layout(binding = 6)\\n'\n"
        "    'layout(std140) uniform block_LightingBlock_0 { vec4 data; } lighting_0;\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'lighting',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['pass']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 6},\n"
        "            'type': {'kind': 'constantBuffer', 'elementVarLayout': {'binding': {'kind': 'uniform', 'size': 16}}},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    lighting_binding = _expected_scoped_binding(
        "lighting", "constant_buffer", "pass", target="opengl"
    )
    assert f"layout(binding = {lighting_binding})\nlayout(std140) uniform block_LightingBlock_0" in glsl
    layout = json.loads((tmp_path / "out.frag.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["binding"]) for r in layout["resources"]] == [
        ("lighting", lighting_binding)
    ]


def test_termin_shaderc_legalizes_opengl_slang_instance_index_builtin(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "[shader(\"vertex\")] float4 main(uint instance_id : SV_InstanceID) : SV_Position {\n"
        "    return float4(float(instance_id), 0.0, 0.0, 1.0);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.vert.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    '#version 460\\n'\n"
        "    '#extension GL_ARB_shader_draw_parameters : require\\n'\n"
        "    'void main() { uint instance_id = uint(gl_InstanceIndex - gl_BaseInstance); }\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({'parameters': []}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    assert "gl_InstanceIndex" not in glsl
    assert "uint(gl_InstanceID)" in glsl


def test_termin_shaderc_patches_opengl_slang_storage_buffer_bindings(tmp_path: Path) -> None:
    shader = tmp_path / "test.slang"
    shader.write_text(
        "struct InstanceData { float3 position; };\n"
        "[[TerminScope(\"draw\")]] StructuredBuffer<InstanceData> foliage_instances;\n"
        "[shader(\"vertex\")] float4 main(uint instance_id : SV_InstanceID) : SV_Position {\n"
        "    return float4(foliage_instances[instance_id].position, 1.0);\n"
        "}\n",
        encoding="utf-8",
    )
    output = tmp_path / "out.vert.glsl"
    fake_slangc = tmp_path / "fake_slangc.py"
    fake_slangc.write_text(
        "#!/usr/bin/env python3\n"
        "import json, pathlib, sys\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1])\n"
        "reflection = pathlib.Path(sys.argv[sys.argv.index('-reflection-json') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_text(\n"
        "    'layout(std430, binding = 2) readonly buffer StructuredBuffer_InstanceData_t_0 {\\n'\n"
        "    '    vec4 _data[];\\n'\n"
        "    '} foliage_instances_0;\\n',\n"
        "    encoding='utf-8')\n"
        "reflection.write_text(json.dumps({\n"
        "    'parameters': [\n"
        "        {\n"
        "            'name': 'foliage_instances',\n"
        "            'userAttribs': [{'name': 'TerminScope', 'arguments': ['draw']}],\n"
        "            'binding': {'kind': 'descriptorTableSlot', 'index': 2},\n"
        "            'type': {'kind': 'resource', 'baseShape': 'structuredBuffer'},\n"
        "        },\n"
        "    ]\n"
        "}), encoding='utf-8')\n",
        encoding="utf-8",
    )
    fake_slangc.chmod(0o755)

    result = _run_shaderc([
        "compile",
        "--language",
        "slang",
        "--target",
        "opengl",
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
    ])

    assert result.returncode == 0, result.stderr
    glsl = output.read_text(encoding="utf-8")
    foliage_binding = _expected_scoped_binding(
        "foliage_instances", "storage_buffer", "draw", target="opengl"
    )
    assert f"layout(std430, binding = {foliage_binding}) readonly buffer" in glsl
    layout = json.loads((tmp_path / "out.vert.glsl.layout.json").read_text(encoding="utf-8"))
    assert [(r["name"], r["binding"]) for r in layout["resources"]] == [
        ("foliage_instances", foliage_binding),
    ]
