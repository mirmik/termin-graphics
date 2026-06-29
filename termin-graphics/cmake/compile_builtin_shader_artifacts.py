import argparse
import json
import pathlib
import subprocess
import sys


STAGE_EXTENSIONS = {
    "vertex": "vert",
    "fragment": "frag",
    "geometry": "geom",
    "compute": "comp",
}

D3D11_STAGE_EXTENSIONS = {
    "vertex": "vs",
    "fragment": "ps",
    "geometry": "gs",
    "compute": "cs",
}

TARGET_EXTENSIONS = {
    "vulkan": "spv",
    "opengl": "glsl",
    "d3d11": "cso",
}


def artifact_name(uuid: str, target: str, stage: str) -> str:
    if target == "d3d11":
        stage_ext = D3D11_STAGE_EXTENSIONS[stage]
    else:
        stage_ext = STAGE_EXTENSIONS[stage]
    return f"{uuid}.{stage_ext}.{TARGET_EXTENSIONS[target]}"


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile Termin built-in Slang shaders to backend artifacts."
    )
    parser.add_argument("--shaderc", required=True)
    parser.add_argument("--slangc")
    parser.add_argument("--fxc")
    parser.add_argument("--source-dir", required=True)
    parser.add_argument("--output-root", required=True)
    parser.add_argument("--target", action="append", required=True)
    args = parser.parse_args()

    shaderc = pathlib.Path(args.shaderc)
    source_dir = pathlib.Path(args.source_dir)
    output_root = pathlib.Path(args.output_root)
    catalog_path = source_dir / "engine-shader-catalog.json"

    if not shaderc.exists():
        print(f"termin-graphics: termin_shaderc does not exist: {shaderc}", file=sys.stderr)
        return 2
    if not catalog_path.exists():
        print(f"termin-graphics: shader catalog does not exist: {catalog_path}", file=sys.stderr)
        return 2

    targets = []
    for target in args.target:
        normalized = target.strip().lower()
        if normalized not in TARGET_EXTENSIONS:
            print(f"termin-graphics: unsupported shader target: {target}", file=sys.stderr)
            return 2
        if normalized not in targets:
            targets.append(normalized)

    with catalog_path.open("r", encoding="utf-8") as f:
        catalog = json.load(f)

    compiled = 0
    skipped = 0
    failures: list[tuple[str, str, str, int]] = []
    for shader in catalog.get("shaders", []):
        if shader.get("language") != "slang":
            skipped += 1
            continue

        uuid = shader.get("uuid")
        if not uuid:
            print("termin-graphics: catalog shader entry is missing uuid", file=sys.stderr)
            return 2

        stages = shader.get("stages", {})
        if not isinstance(stages, dict):
            print(f"termin-graphics: catalog shader '{uuid}' has invalid stages", file=sys.stderr)
            return 2

        for stage, spec in stages.items():
            if stage not in STAGE_EXTENSIONS:
                skipped += 1
                continue
            if not isinstance(spec, dict):
                print(f"termin-graphics: catalog shader '{uuid}' stage '{stage}' is invalid", file=sys.stderr)
                return 2

            source_path = source_dir / spec["path"]
            entry = spec.get("entry", "vs_main" if stage == "vertex" else "fs_main")
            for target in targets:
                output_dir = output_root / "shaders" / target
                output_dir.mkdir(parents=True, exist_ok=True)
                output_path = output_dir / artifact_name(uuid, target, stage)
                cmd = [
                    str(shaderc),
                    "compile",
                    "--language",
                    "slang",
                    "--target",
                    target,
                    "--stage",
                    stage,
                    "--entry",
                    entry,
                    "--input",
                    str(source_path),
                    "--output",
                    str(output_path),
                    "-I",
                    str(source_dir),
                    "--debug-name",
                    f"{uuid}:{stage}",
                ]
                if args.slangc:
                    cmd.extend(["--slangc", args.slangc])
                if args.fxc:
                    cmd.extend(["--fxc", args.fxc])
                result = subprocess.run(cmd)
                compiled += 1
                if result.returncode != 0:
                    failures.append((uuid, target, stage, result.returncode))

    if failures:
        for uuid, target, stage, rc in failures:
            print(
                f"termin-graphics: failed to compile {uuid}:{stage} for {target}, rc={rc}",
                file=sys.stderr,
            )
        return 1

    print(
        f"termin-graphics: compiled {compiled} built-in shader artifacts; skipped {skipped} catalog entries"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
