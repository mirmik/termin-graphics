import os
import subprocess
import struct
from pathlib import Path

import pytest


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _termin_shaderc() -> Path:
    candidates: list[Path] = []
    binary_names = ["termin_shaderc.exe", "termin_shaderc"] if os.name == "nt" else ["termin_shaderc"]
    explicit = os.environ.get("TERMIN_SHADERC")
    if explicit:
        candidates.append(Path(explicit))
    root = _repo_root()
    candidate_dirs = [
        root / "build" / "Release" / "bin" / "Release",
        root / "build" / "Release" / "bin",
        root / "sdk" / "bin",
    ]
    candidates.extend(candidate_dir / name for candidate_dir in candidate_dirs for name in binary_names)
    sdk = os.environ.get("TERMIN_SDK")
    if sdk:
        candidates.extend(Path(sdk) / "bin" / name for name in binary_names)
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
