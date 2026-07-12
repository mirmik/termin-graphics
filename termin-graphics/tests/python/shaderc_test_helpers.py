import os
import subprocess
import struct
from pathlib import Path

import pytest


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _termin_shaderc() -> Path:
    explicit = os.environ.get("TERMIN_SHADERC")
    if not explicit:
        pytest.fail("TERMIN_SHADERC must point to the compiler built by the current C++ test graph")
    compiler = Path(explicit)
    if not compiler.is_file() or not os.access(compiler, os.X_OK):
        pytest.fail(f"TERMIN_SHADERC is not an executable file: {compiler}")
    return compiler


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


def _write_fake_fxc(path: Path, *, exit_code: int = 0) -> Path:
    path.write_text(
        "#!/usr/bin/env python3\n"
        "import json, os, pathlib, sys\n"
        "args_path = os.environ.get('FAKE_FXC_ARGS')\n"
        "if args_path:\n"
        "    pathlib.Path(args_path).write_text(json.dumps(sys.argv[1:]), encoding='utf-8')\n"
        f"exit_code = {exit_code}\n"
        "if exit_code != 0:\n"
        "    sys.stderr.write('fake fxc failure\\n')\n"
        "    raise SystemExit(exit_code)\n"
        "out = pathlib.Path(sys.argv[sys.argv.index('/Fo') + 1])\n"
        "out.parent.mkdir(parents=True, exist_ok=True)\n"
        "out.write_bytes(b'FAKE-CSO')\n",
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


def _stable_resource_name_hash(value: str) -> int:
    result = 2166136261
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 16777619) & 0xFFFFFFFF
    return result


def _resource_binding_class(kind: str, target: str) -> str:
    if target != "opengl":
        return "descriptor"
    if kind in {"constant_buffer", "uniform_buffer"}:
        return "constant_buffer"
    if kind in {"storage_buffer", "storage_texture", "sampler", "texture"}:
        return kind
    return kind


def _scoped_binding_range(kind: str, scope: str, target: str = "vulkan") -> tuple[int, int]:
    if kind in {"constant_buffer", "uniform_buffer"}:
        return {
            "frame": (0, 4),
            "pass": (4, 4),
            "material": (8, 8),
            "draw": (16, 16),
            "transient": (32, 8),
        }.get(scope, (0, 0))
    if kind == "storage_buffer":
        return {
            "draw": (40, 16),
            "pass": (56, 8),
            "material": (64, 8),
            "frame": (72, 4),
            "transient": (76, 8),
        }.get(scope, (0, 0))
    if kind in {"texture", "storage_texture", "sampler"}:
        if target == "opengl":
            return {
                "frame": (0, 4),
                "material": (4, 16),
                "pass": (20, 8),
                "draw": (28, 4),
                "transient": (32, 16),
            }.get(scope, (0, 0))
        return {
            "material": (80, 32),
            "pass": (112, 16),
            "draw": (128, 16),
            "transient": (144, 48),
            "frame": (192, 8),
        }.get(scope, (0, 0))
    return (0, 0)


def _expected_scoped_bindings(
    resources: list[tuple[str, str, str]],
    *,
    target: str = "vulkan",
) -> dict[str, int]:
    result: dict[str, int] = {}
    occupied: set[tuple[int, int, str]] = set()
    for name, kind, scope in resources:
        base, size = _scoped_binding_range(kind, scope, target)
        assert size > 0, (name, kind, scope, target)
        binding = base + (_stable_resource_name_hash(name) % size)
        binding_class = _resource_binding_class(kind, target)
        for _ in range(size):
            key = (0, binding, binding_class)
            if key not in occupied:
                break
            binding = base + ((binding - base + 1) % size)
        result[name] = binding
        occupied.add((0, binding, binding_class))
    return result


def _expected_scoped_binding(
    name: str,
    kind: str,
    scope: str,
    *,
    target: str = "vulkan",
) -> int:
    return _expected_scoped_bindings([(name, kind, scope)], target=target)[name]


def _spirv_decoration_value(path: Path, resource_id: int, decoration: int) -> int | None:
    data = path.read_bytes()
    if len(data) % 4 != 0:
        return None
    words = struct.unpack("<" + "I" * (len(data) // 4), data)
    index = 5
    while index < len(words):
        instruction = words[index]
        word_count = instruction >> 16
        opcode = instruction & 0xFFFF
        if word_count == 0 or index + word_count > len(words):
            return None
        if opcode == 71 and word_count >= 4 and words[index + 1] == resource_id:
            if words[index + 2] == decoration:
                return words[index + 3]
        index += word_count
    return None
