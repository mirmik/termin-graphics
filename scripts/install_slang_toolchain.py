#!/usr/bin/env python3
"""Install the pinned Slang compiler and register it in Termin settings."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOCK = REPO_ROOT / "build-system" / "slang-toolchain-lock.json"
SETTINGS_KEY = "Shader/slangCompiler"


def platform_key() -> str:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if system == "linux" and machine in {"x86_64", "amd64"}:
        return "linux-x86_64"
    raise RuntimeError(
        f"unsupported Slang installer platform: {platform.system()} {platform.machine()}"
    )


def default_install_root() -> Path:
    configured = os.environ.get("TERMIN_SLANG_TOOLCHAIN_DIR")
    if configured:
        return Path(configured).expanduser()
    if os.name == "nt":
        local_app_data = os.environ.get("LOCALAPPDATA")
        if not local_app_data:
            raise RuntimeError("LOCALAPPDATA is unavailable")
        return Path(local_app_data) / "Termin" / "Toolchains"
    data_home = os.environ.get("XDG_DATA_HOME")
    base = Path(data_home) if data_home else Path.home() / ".local" / "share"
    return base / "termin" / "toolchains"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        prefix=f".{destination.name}.", dir=destination.parent, delete=False
    ) as temporary:
        temporary_path = Path(temporary.name)
    try:
        print(f"Downloading {url}", file=sys.stderr)
        with urllib.request.urlopen(url) as response, temporary_path.open("wb") as output:
            shutil.copyfileobj(response, output)
        temporary_path.replace(destination)
    finally:
        temporary_path.unlink(missing_ok=True)


def verify_version(executable: Path, expected: str) -> None:
    result = subprocess.run(
        [str(executable), "-version"],
        text=True,
        capture_output=True,
        check=False,
    )
    actual = (result.stdout or result.stderr).strip()
    if result.returncode != 0 or actual != expected:
        raise RuntimeError(
            f"expected slangc {expected}, got {actual or '<no version output>'}"
        )


def install(lock_path: Path, root: Path, *, require_installed: bool) -> tuple[Path, str]:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    version = str(lock["version"])
    descriptor = lock["platforms"][platform_key()]
    install_dir = root / f"slang-{version}"
    executable = install_dir / descriptor["executable"]

    if executable.is_file():
        verify_version(executable, version)
        return executable.resolve(), version
    if require_installed:
        raise RuntimeError(
            f"Slang {version} is not installed at {install_dir}; run setup-slang-toolchain.sh"
        )
    if install_dir.exists():
        raise RuntimeError(
            f"incomplete Slang installation exists at {install_dir}; move it aside and retry"
        )

    archive = root / "downloads" / descriptor["archive"]
    if not archive.is_file():
        download(descriptor["url"], archive)
    actual_digest = sha256(archive)
    if actual_digest != descriptor["sha256"]:
        raise RuntimeError(
            f"Slang archive checksum mismatch: expected {descriptor['sha256']}, "
            f"got {actual_digest} ({archive})"
        )

    root.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=f".slang-{version}.", dir=root))
    try:
        with tarfile.open(archive, "r:gz") as bundle:
            bundle.extractall(staging, filter="data")
        staged_executable = staging / descriptor["executable"]
        if not staged_executable.is_file():
            raise RuntimeError(
                f"Slang archive does not contain {descriptor['executable']}"
            )
        verify_version(staged_executable, version)
        staging.replace(install_dir)
    finally:
        if staging.exists():
            shutil.rmtree(staging)

    return executable.resolve(), version


def configure_settings(executable: Path) -> Path:
    try:
        from tcbase import Settings
    except ImportError as exc:
        raise RuntimeError(
            "tcbase is unavailable; run this through sdk/bin/termin_python "
            "after building or installing the Termin SDK"
        ) from exc
    settings = Settings("termin")
    settings.set(SETTINGS_KEY, str(executable))
    return Path(settings.path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lock-file", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--install-root", type=Path, default=default_install_root())
    parser.add_argument(
        "--require-installed",
        action="store_true",
        help="do not download; fail unless the pinned compiler is already installed",
    )
    parser.add_argument(
        "--no-configure",
        action="store_true",
        help=f"do not write {SETTINGS_KEY} to the common Termin settings",
    )
    parser.add_argument("--print-path", action="store_true")
    args = parser.parse_args()

    try:
        executable, version = install(
            args.lock_file.resolve(),
            args.install_root.expanduser().resolve(),
            require_installed=args.require_installed,
        )
        settings_path = None if args.no_configure else configure_settings(executable)
    except (KeyError, OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    if args.print_path:
        print(executable)
    else:
        print(f"Slang {version} ready: {executable}")
        if settings_path is not None:
            print(f"Configured {SETTINGS_KEY} in {settings_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
