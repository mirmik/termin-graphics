import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[3]


def _termin_shaderc() -> Path:
    binary_names = ["termin_shaderc.exe", "termin_shaderc"] if os.name == "nt" else ["termin_shaderc"]
    candidates: list[Path] = []
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


def _resolve_slangc() -> Path:
    explicit = os.environ.get("TERMIN_SLANGC")
    if explicit and Path(explicit).exists():
        return Path(explicit)
    found = shutil.which("slangc")
    if found:
        return Path(found)
    pytest.skip("slangc is not available")


def _resolve_fxc() -> Path:
    explicit = os.environ.get("TERMIN_FXC")
    if explicit and Path(explicit).exists():
        return Path(explicit)
    found = shutil.which("fxc")
    if found:
        return Path(found)
    program_files_x86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    kit_bin = Path(program_files_x86) / "Windows Kits" / "10" / "bin"
    candidates = sorted(kit_bin.glob(r"*\x64\fxc.exe"))
    if candidates:
        return candidates[-1]
    pytest.skip("fxc.exe is not available")


def _builtin_shader_stage_jobs() -> list[tuple[Path, str, str]]:
    source_root = _repo_root() / "termin-graphics" / "resources" / "builtin_shaders"
    attr_re = re.compile(r'\[shader\("(?P<stage>vertex|fragment|geometry|compute)"\)\]')
    entry_re = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")
    jobs: list[tuple[Path, str, str]] = []
    for path in sorted(source_root.glob("*.slang")):
        text = path.read_text(encoding="utf-8")
        for match in attr_re.finditer(text):
            search_window = text[match.end():match.end() + 300]
            entry_match = entry_re.search(search_window)
            assert entry_match is not None, f"could not infer entry point for {path.name}"
            jobs.append((path, match.group("stage"), entry_match.group(1)))
    assert jobs, "no builtin Slang shader stages were discovered"
    return jobs


@pytest.mark.skipif(os.name != "nt", reason="D3D11 .cso artifact matrix is Windows-only")
def test_builtin_slang_shaders_compile_to_d3d11_cso(tmp_path: Path) -> None:
    shaderc = _termin_shaderc()
    slangc = _resolve_slangc()
    fxc = _resolve_fxc()
    source_root = _repo_root() / "termin-graphics" / "resources" / "builtin_shaders"
    stage_suffix = {
        "vertex": "vs",
        "fragment": "ps",
        "geometry": "gs",
        "compute": "cs",
    }

    failures: list[str] = []
    for source, stage, entry in _builtin_shader_stage_jobs():
        suffix = stage_suffix[stage]
        output = tmp_path / "shaders" / "d3d11" / f"{source.stem}.{entry}.{suffix}.cso"
        result = subprocess.run(
            [
                str(shaderc),
                "compile",
                "--language",
                "slang",
                "--target",
                "d3d11",
                "--stage",
                stage,
                "--entry",
                entry,
                "--input",
                str(source),
                "--output",
                str(output),
                "--slangc",
                str(slangc),
                "--fxc",
                str(fxc),
                "--include-dir",
                str(source_root),
                "--debug-name",
                f"{source.name}:{stage}:{entry}",
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0 or not output.exists() or not Path(str(output) + ".layout.json").exists():
            failures.append(
                f"{source.name}:{stage}:{entry} rc={result.returncode}\n"
                f"stdout:\n{result.stdout}\n"
                f"stderr:\n{result.stderr}"
            )

    assert not failures, "\n\n".join(failures)
