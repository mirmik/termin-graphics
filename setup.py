#!/usr/bin/env python3

from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from setuptools.command.build import build as _build
from pathlib import Path
import shutil
import subprocess
import sys
import os


def _get_sdk_prefix():
    """On Windows, return path to shared termin-sdk directory."""
    if sys.platform == "win32":
        base = os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local"))
        return Path(base) / "termin-sdk"
    return None


def _copy_dlls(lib_dir, dst_dir):
    """Copy DLL files to target directory (Windows: DLLs must be next to .pyd)."""
    for dll in lib_dir.glob("*.dll"):
        shutil.copy2(dll, dst_dir / dll.name)


class CMakeBuild(_build):
    """Run build_ext before build_py so that DLLs copied to source tree
    during compilation are picked up by package_data."""

    def run(self):
        self.run_command("build_ext")
        _build.run(self)


class CMakeBuildExt(build_ext):
    """
    Build nanobind extension via CMake.
    CMake builds termin_graphics library + _tgfx_native module.
    """

    def build_extension(self, ext):
        source_dir = Path(directory)
        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        cfg = "Debug" if self.debug else "Release"

        staging_dir = (build_temp / "install").resolve()
        staging_dir.mkdir(parents=True, exist_ok=True)

        sdk = _get_sdk_prefix()

        cmake_args = [
            str(source_dir),
            f"-DCMAKE_BUILD_TYPE={cfg}",
            f"-DTGFX_BUILD_PYTHON=ON",
            f"-DPython_EXECUTABLE={sys.executable}",
            f"-DCMAKE_INSTALL_PREFIX={staging_dir}",
            f"-DCMAKE_INSTALL_LIBDIR=lib",
        ]
        if sdk:
            cmake_args.append(f"-DCMAKE_PREFIX_PATH={sdk}")

        subprocess.check_call(["cmake", *cmake_args], cwd=build_temp)
        subprocess.check_call(
            ["cmake", "--build", ".", "--config", cfg, "--parallel"],
            cwd=build_temp,
        )
        subprocess.check_call(
            ["cmake", "--install", ".", "--config", cfg],
            cwd=build_temp,
        )

        # On Windows, also install C++ artifacts into shared SDK directory
        if sdk:
            sdk.mkdir(parents=True, exist_ok=True)
            subprocess.check_call(
                ["cmake", "--install", ".", "--config", cfg, "--prefix", str(sdk)],
                cwd=build_temp,
            )

        # Find the built native module (.so on Linux, .pyd on Windows)
        # On MSVC, outputs land in a Release/ or Debug/ subdirectory
        patterns = ["_tgfx_native.*.so", "_tgfx_native.*.pyd", "_tgfx_native.pyd"]
        built_files = []
        for pat in patterns:
            built_files.extend((build_temp / "python").glob(pat))
            built_files.extend((build_temp / "python" / cfg).glob(pat))
        if not built_files:
            raise RuntimeError("CMake build did not produce _tgfx_native module")

        built_so = built_files[0]

        # Copy to where setuptools expects it (makes install/bdist_wheel work)
        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_so, ext_path)

        # On Windows, copy DLLs next to the .pyd
        if sys.platform == "win32":
            _copy_dlls(staging_dir / "lib", ext_path.parent)
            _copy_dlls(staging_dir / "bin", ext_path.parent)

        # Also copy to source tree so build_py picks them up via package_data
        tgfx_pkg_dir = source_dir / "python" / "tgfx"
        tgfx_pkg_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(built_so, tgfx_pkg_dir / built_so.name)

        if sys.platform == "win32":
            _copy_dlls(staging_dir / "lib", tgfx_pkg_dir)
            _copy_dlls(staging_dir / "bin", tgfx_pkg_dir)


directory = os.path.dirname(os.path.realpath(__file__))

setup(
    name="tgfx",
    version="0.1.0",
    license="MIT",
    description="Graphics backend library with Python bindings",
    author="mirmik",
    author_email="mirmikns@yandex.ru",
    python_requires=">=3.8",
    packages=["tgfx"],
    package_dir={"tgfx": "python/tgfx"},
    package_data={
        "tgfx": [
            "*.dll",
            "lib/*.dll",
        ],
    },
    install_requires=["numpy"],
    ext_modules=[
        Extension("tgfx._tgfx_native", sources=[]),
    ],
    cmdclass={"build": CMakeBuild, "build_ext": CMakeBuildExt},
    zip_safe=False,
)
